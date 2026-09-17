// Copyright 2026 Pride Leong.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// H2 verdict rig (ADR-0030 D5, roadmap 1.3): the same chain shape run
// three ways, one million messages each —
//   handwritten : CacheBuffer + DataDispatcher wired by hand, no
//                 FlowRuntime machinery (the gold standard). Carries
//                 the same per-message lineage semantics as the DSL
//                 (ADR-0032 gate recalibration): root at the source,
//                 one hop per stage on the stage's output channel
//                 with a per-channel seq, branch merge at joins, and
//                 delivery into the sink callback — so the gate
//                 measures the cost of the DECLARATION layer over a
//                 handwritten implementation of the same semantics,
//                 not the cost of lineage itself.
//   interpreted : FlowBuilder -> FlowRuntime wiring (publish_bytes)
//   compiled    : the pipeline's .so artifact installing its wiring
// Per-message e2e is stamped into the payload at the source and
// measured at the terminal sink; P50/P99/P999 reported as counters.
// Shapes (roadmap 1.3): short/medium/long linear chains, fan-in
// (three sources fused by two nested binary joins — DSL v0 join is
// binary, so the roadmap's "(A,B,C)->D" converges via J1(A,B) and
// J2(J1,C), five stages), and fan-out (one source feeding four branch
// maps, each with its own sink; every delivery is one latency sample).

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include "tianshu/base/cache_buffer.h"
#include "tianshu/compiler/pipeline.h"
#include "tianshu/core/data_dispatcher.h"
#include "tianshu/core/data_visitor.h"
#include "tianshu/core/lineage.h"
#include "tianshu/core/message_traits.h"
#include "tianshu/dsl/dsl_runtime.h"
#include "tianshu/dsl/flow.h"

namespace {

using tianshu::base::CacheBuffer;
using tianshu::compiler::Pipeline;
using tianshu::core::DataDispatcher;
using tianshu::core::Lineage;

struct BenchMsg {
  std::uint64_t born_ns{0};
  std::uint64_t seq{0};
};

}  // namespace

TIANSHU_TRAITS_POD(BenchMsg, "bench.BenchMsg");

namespace {

constexpr int kMessages = 1'000'000;

[[nodiscard]] std::uint64_t now_ns() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count());
}

BenchMsg transform(const BenchMsg& in) { return BenchMsg{.born_ns = in.born_ns, .seq = in.seq + 1}; }

// Fan-in fuse: born_ns keeps the latest of the parents (the message is
// "complete" when its last parent arrived); seq fuses both parents.
BenchMsg fuse(const BenchMsg& a, const BenchMsg& b) {
  return BenchMsg{.born_ns = std::max(a.born_ns, b.born_ns), .seq = a.seq + b.seq + 1};
}

void report_percentiles(benchmark::State& state, const std::vector<std::uint64_t>& latencies,
                        int hops) {
  std::vector<std::uint64_t> sorted(latencies);
  std::ranges::sort(sorted);
  const auto pick = [&sorted](double f) {
    const auto rank = static_cast<std::size_t>(f * static_cast<double>(sorted.size()));
    return sorted[std::min(sorted.size() - 1, rank)];
  };
  state.counters["hops"] = hops;
  state.counters["p50_ns"] = static_cast<double>(pick(0.50));
  state.counters["p99_ns"] = static_cast<double>(pick(0.99));
  state.counters["p999_ns"] = static_cast<double>(pick(0.999));
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(sorted.size()));
}

// ---------------------------------------------------------------------------
// Handwritten: CacheBuffer + dispatcher per hop, wired by hand.
// ---------------------------------------------------------------------------

struct HandHop {
  CacheBuffer<BenchMsg> buf{16};
  std::function<void()> notify;
  // Single-slot lineage inbox + per-output-channel seq counter: the
  // handwritten mirror of the DSL's lineage discipline on the same
  // synchronous-cascade assumption (consumer runs inside the
  // producer's dispatch call).
  Lineage slot;
  std::uint64_t out_seq{0};
};

class HandChain {
 public:
  HandChain(const std::string& prefix, int hops,
            std::vector<std::uint64_t>* sink_latencies)
      : sink_latencies_(*sink_latencies), owner_(this) {
    auto& dispatcher = DataDispatcher::instance();
    const auto hop_count = static_cast<std::size_t>(hops);
    for (std::size_t i = 0; i < hop_count; ++i) {
      channels_.push_back(prefix + "/hop" + std::to_string(i));
    }
    channels_.push_back(prefix + "/out");
    for (std::size_t i = 0; i < hop_count; ++i) {
      hops_.push_back(std::make_unique<HandHop>());
    }
    sink_ = std::make_unique<HandHop>();

    for (std::size_t i = 0; i < hop_count; ++i) {
      const std::uint64_t next = tianshu::core::channel_id_for(channels_[i + 1]);
      const std::string& out_ch = channels_[i + 1];
      HandHop* dst = i + 1 < hop_count ? hops_[i + 1].get() : sink_.get();
      hops_[i]->notify = [this, i, next, &out_ch, dst] {
        while (const BenchMsg* msg = hops_[i]->buf.try_fetch()) {
          const BenchMsg out = transform(*msg);
          Lineage lin = std::move(hops_[i]->slot);
          lin.add_hop({.channel = out_ch, .seq = ++hops_[i]->out_seq});
          dst->slot = std::move(lin);
          DataDispatcher::instance().dispatch(next, &out, sizeof(out));
        }
      };
      dispatcher.add_buffer(tianshu::core::channel_id_for(channels_[i]), &hops_[i]->buf,
                            hops_[i]->notify, owner_);
    }
    sink_->notify = [this] {
      while (const BenchMsg* msg = sink_->buf.try_fetch()) {
        const Lineage& received = sink_->slot;
        static_cast<void>(received);
        sink_latencies_.push_back(now_ns() - msg->born_ns);
      }
    };
    dispatcher.add_buffer(tianshu::core::channel_id_for(channels_.back()), &sink_->buf,
                          sink_->notify, owner_);
  }

  ~HandChain() { DataDispatcher::instance().remove_owner(owner_); }

  void drive(std::size_t messages) {
    const std::uint64_t first = tianshu::core::channel_id_for(channels_.front());
    const std::string& first_ch = channels_.front();
    for (std::size_t i = 0; i < messages; ++i) {
      const BenchMsg msg{.born_ns = now_ns(), .seq = static_cast<std::uint64_t>(i)};
      hops_.front()->slot = Lineage::rooted(first_ch, static_cast<std::uint64_t>(i));
      DataDispatcher::instance().dispatch(first, &msg, sizeof(msg));
    }
  }

 private:
  std::vector<std::string> channels_;
  std::vector<std::unique_ptr<HandHop>> hops_;
  std::unique_ptr<HandHop> sink_;
  std::vector<std::uint64_t>& sink_latencies_;
  const void* owner_;
};

// Handwritten fan-in joiner: a staging slot per input, paired on arrival.
// try_fetch() consumes, so an input is only fetched after its notify fired
// (the buffer is non-empty then) and messages wait in slots, never
// re-queued. Lineage mirrors the DSL join: pop both input branches
// (a then b), merge, then one hop on the output channel.
struct HandJoin {
  CacheBuffer<BenchMsg> in_a{16};
  CacheBuffer<BenchMsg> in_b{16};
  std::optional<BenchMsg> slot_a;
  std::optional<BenchMsg> slot_b;
  Lineage lin_a;
  Lineage lin_b;
  Lineage out_slot;
  std::uint64_t out_seq{0};

  void step_a(std::uint64_t next, Lineage* dst_slot, const std::string* out_ch) {
    while (const BenchMsg* p = in_a.try_fetch()) {
      slot_a = *p;
      try_emit(next, dst_slot, out_ch);
    }
  }

  void step_b(std::uint64_t next, Lineage* dst_slot, const std::string* out_ch) {
    while (const BenchMsg* p = in_b.try_fetch()) {
      slot_b = *p;
      try_emit(next, dst_slot, out_ch);
    }
  }

  void try_emit(std::uint64_t next, Lineage* dst_slot, const std::string* out_ch) {
    if (!slot_a.has_value() || !slot_b.has_value()) {
      return;
    }
    const BenchMsg out = fuse(*slot_a, *slot_b);
    Lineage merged = std::move(lin_a);
    merged.merge(lin_b);
    merged.add_hop({.channel = *out_ch, .seq = ++out_seq});
    if (dst_slot != nullptr) {
      *dst_slot = std::move(merged);
    }
    slot_a.reset();
    slot_b.reset();
    DataDispatcher::instance().dispatch(next, &out, sizeof(out));
  }
};

// Handwritten fan-in: J1(A,B) -> J2(J1,C) -> sink.
class HandFanIn {
 public:
  HandFanIn(const std::string& prefix, std::vector<std::uint64_t>* sink_latencies)
      : sink_latencies_(*sink_latencies), owner_(this) {
    auto& dispatcher = DataDispatcher::instance();
    const std::uint64_t j1_id = tianshu::core::channel_id_for(prefix + "/j1");
    const std::uint64_t out_id = tianshu::core::channel_id_for(prefix + "/out");
    j1_out_ch_ = prefix + "/j1";
    out_ch_ = prefix + "/out";
    dispatcher.add_buffer(tianshu::core::channel_id_for(prefix + "/a"), &j1_.in_a,
                          [this, j1_id] { j1_.step_a(j1_id, &j2_.lin_a, &j1_out_ch_); }, owner_);
    dispatcher.add_buffer(tianshu::core::channel_id_for(prefix + "/b"), &j1_.in_b,
                          [this, j1_id] { j1_.step_b(j1_id, &j2_.lin_a, &j1_out_ch_); }, owner_);
    dispatcher.add_buffer(j1_id, &j2_.in_a, [this, out_id] { j2_.step_a(out_id, nullptr, &out_ch_); },
                          owner_);
    dispatcher.add_buffer(tianshu::core::channel_id_for(prefix + "/c"), &j2_.in_b,
                          [this, out_id] { j2_.step_b(out_id, &sink_lin_, &out_ch_); }, owner_);
    dispatcher.add_buffer(out_id, &sink_buf_,
                          [this] {
                            while (const BenchMsg* msg = sink_buf_.try_fetch()) {
                              const Lineage& received = sink_lin_;
                              static_cast<void>(received);
                              sink_latencies_.push_back(now_ns() - msg->born_ns);
                            }
                          },
                          owner_);
    ch_a_ = prefix + "/a";
    ch_b_ = prefix + "/b";
    ch_c_ = prefix + "/c";
  }

  ~HandFanIn() { DataDispatcher::instance().remove_owner(owner_); }

  void drive(std::size_t messages) {
    const std::uint64_t a = tianshu::core::channel_id_for(ch_a_);
    const std::uint64_t b = tianshu::core::channel_id_for(ch_b_);
    const std::uint64_t c = tianshu::core::channel_id_for(ch_c_);
    for (std::size_t i = 0; i < messages; ++i) {
      const std::uint64_t born = now_ns();
      const auto seq = static_cast<std::uint64_t>(i);
      const BenchMsg ma{.born_ns = born, .seq = seq};
      const BenchMsg mb{.born_ns = born, .seq = seq};
      const BenchMsg mc{.born_ns = born, .seq = seq};
      j1_.lin_a = Lineage::rooted(ch_a_, seq);
      j1_.lin_b = Lineage::rooted(ch_b_, seq);
      j2_.lin_b = Lineage::rooted(ch_c_, seq);
      DataDispatcher::instance().dispatch(a, &ma, sizeof(ma));
      DataDispatcher::instance().dispatch(b, &mb, sizeof(mb));
      DataDispatcher::instance().dispatch(c, &mc, sizeof(mc));
    }
  }

 private:
  std::string ch_a_;
  std::string ch_b_;
  std::string ch_c_;
  std::string j1_out_ch_;
  std::string out_ch_;
  HandJoin j1_;
  HandJoin j2_;
  CacheBuffer<BenchMsg> sink_buf_{16};
  Lineage sink_lin_;
  std::vector<std::uint64_t>& sink_latencies_;
  const void* owner_;
};

// Handwritten fan-out: the source channel feeds four consumer buffers, each
// branching into its own hop channel and sink.
class HandFanOut {
 public:
  HandFanOut(const std::string& prefix, std::vector<std::uint64_t>* sink_latencies)
      : sink_latencies_(*sink_latencies), owner_(this) {
    auto& dispatcher = DataDispatcher::instance();
    const std::string src = prefix + "/src";
    for (std::size_t i = 0; i < branches_.size(); ++i) {
      const std::string hop = prefix + "/hop" + std::to_string(i);
      const std::uint64_t hop_id = tianshu::core::channel_id_for(hop);
      branches_[i].in = std::make_unique<CacheBuffer<BenchMsg>>(16);
      branches_[i].sink = std::make_unique<CacheBuffer<BenchMsg>>(16);
      // Four consumers of one source channel: each branch gets its own
      // lineage copy (the DSL fans a copy to every consumer).
      dispatcher.add_buffer(tianshu::core::channel_id_for(src), branches_[i].in.get(),
                            [this, i, hop_id, hop] {
                              while (const BenchMsg* msg = branches_[i].in->try_fetch()) {
                                const BenchMsg out = transform(*msg);
                                Lineage lin = branches_[i].src_lin;
                                lin.add_hop({.channel = hop, .seq = ++branches_[i].out_seq});
                                branches_[i].sink_lin = std::move(lin);
                                DataDispatcher::instance().dispatch(hop_id, &out, sizeof(out));
                              }
                            },
                            owner_);
      dispatcher.add_buffer(hop_id, branches_[i].sink.get(),
                            [this, i] {
                              while (const BenchMsg* msg = branches_[i].sink->try_fetch()) {
                                const Lineage& received = branches_[i].sink_lin;
                                static_cast<void>(received);
                                sink_latencies_.push_back(now_ns() - msg->born_ns);
                              }
                            },
                            owner_);
    }
    ch_src_ = src;
  }

  ~HandFanOut() { DataDispatcher::instance().remove_owner(owner_); }

  void drive(std::size_t messages) {
    const std::uint64_t src = tianshu::core::channel_id_for(ch_src_);
    for (std::size_t i = 0; i < messages; ++i) {
      const auto seq = static_cast<std::uint64_t>(i);
      const BenchMsg msg{.born_ns = now_ns(), .seq = seq};
      const Lineage root = Lineage::rooted(ch_src_, seq);
      for (auto& branch : branches_) {
        branch.src_lin = root;
      }
      DataDispatcher::instance().dispatch(src, &msg, sizeof(msg));
    }
  }

 private:
  struct Branch {
    std::unique_ptr<CacheBuffer<BenchMsg>> in;
    std::unique_ptr<CacheBuffer<BenchMsg>> sink;
    Lineage src_lin;
    Lineage sink_lin;
    std::uint64_t out_seq{0};
  };
  std::array<Branch, 4> branches_;
  std::string ch_src_;
  std::vector<std::uint64_t>& sink_latencies_;
  const void* owner_;
};

// ---------------------------------------------------------------------------
// DSL flow with the same shape; interpreted and compiled share it.
// ---------------------------------------------------------------------------

tianshu::dsl::Flow make_flow(const std::string& name, int hops,
                             std::vector<std::uint64_t>* sink_latencies) {
  tianshu::dsl::FlowBuilder b(name);
  auto chain = b.source<BenchMsg>("src", std::chrono::milliseconds(1), [](std::uint64_t t) {
    return BenchMsg{.born_ns = 0, .seq = t};
  });
  for (int i = 0; i < hops; ++i) {
    chain = chain.map<BenchMsg>([](const BenchMsg& in) { return transform(in); });
  }
  static_cast<void>(hops);
  chain.sink([sink_latencies](const BenchMsg& msg, const Lineage&) {
    sink_latencies->push_back(now_ns() - msg.born_ns);
  });
  return b.build();
}

void drive_runtime(tianshu::dsl::FlowRuntime& rt, const tianshu::dsl::Flow& flow, int messages) {
  const std::string& channel = flow.sources().front().channel;
  rt.wire(flow);
  for (int i = 0; i < messages; ++i) {
    const BenchMsg msg{.born_ns = now_ns(), .seq = static_cast<std::uint64_t>(i)};
    rt.publish_bytes(channel, &msg, sizeof(msg),
                      Lineage::rooted(channel, static_cast<std::uint64_t>(i)));
  }
}

tianshu::dsl::Flow make_fanin_flow(const std::string& name,
                                   std::vector<std::uint64_t>* sink_latencies) {
  tianshu::dsl::FlowBuilder b(name);
  const auto emit = [](std::uint64_t t) { return BenchMsg{.born_ns = 0, .seq = t}; };
  auto sa = b.source<BenchMsg>("a", std::chrono::milliseconds(1), emit);
  auto sb = b.source<BenchMsg>("b", std::chrono::milliseconds(1), emit);
  auto sc = b.source<BenchMsg>("c", std::chrono::milliseconds(1), emit);
  auto j1 = b.join<BenchMsg, BenchMsg, BenchMsg>(sa, sb, fuse);
  auto j2 = b.join<BenchMsg, BenchMsg, BenchMsg>(j1, sc, fuse);
  j2.sink([sink_latencies](const BenchMsg& msg, const Lineage&) {
    sink_latencies->push_back(now_ns() - msg.born_ns);
  });
  return b.build();
}

tianshu::dsl::Flow make_fanout_flow(const std::string& name,
                                    std::vector<std::uint64_t>* sink_latencies) {
  tianshu::dsl::FlowBuilder b(name);
  auto src = b.source<BenchMsg>("src", std::chrono::milliseconds(1), [](std::uint64_t t) {
    return BenchMsg{.born_ns = 0, .seq = t};
  });
  const auto sink_fn = [sink_latencies](const BenchMsg& msg, const Lineage&) {
    sink_latencies->push_back(now_ns() - msg.born_ns);
  };
  for (int i = 0; i < 4; ++i) {
    src.map<BenchMsg>(transform).sink(sink_fn);
  }
  return b.build();
}

// Publish-only drive (compiled artifacts install their own wiring via run(),
// so wire() must not run twice).
void publish_fanin(tianshu::dsl::FlowRuntime& rt, const tianshu::dsl::Flow& flow, int messages) {
  const auto& sources = flow.sources();
  const std::string& ch_a = sources[0].channel;
  const std::string& ch_b = sources[1].channel;
  const std::string& ch_c = sources[2].channel;
  for (int i = 0; i < messages; ++i) {
    const std::uint64_t born = now_ns();
    const auto seq = static_cast<std::uint64_t>(i);
    const BenchMsg ma{.born_ns = born, .seq = seq};
    const BenchMsg mb{.born_ns = born, .seq = seq};
    const BenchMsg mc{.born_ns = born, .seq = seq};
    rt.publish_bytes(ch_a, &ma, sizeof(ma), Lineage::rooted(ch_a, seq));
    rt.publish_bytes(ch_b, &mb, sizeof(mb), Lineage::rooted(ch_b, seq));
    rt.publish_bytes(ch_c, &mc, sizeof(mc), Lineage::rooted(ch_c, seq));
  }
}

void drive_fanin(tianshu::dsl::FlowRuntime& rt, const tianshu::dsl::Flow& flow, int messages) {
  rt.wire(flow);
  publish_fanin(rt, flow, messages);
}

}  // namespace

// ---------------------------------------------------------------------------
// Benchmarks: shape x implementation.
// ---------------------------------------------------------------------------

void run_handwritten(benchmark::State& state, int hops) {
  for (auto _ : state) {
    std::vector<std::uint64_t> latencies;
    latencies.reserve(kMessages);
    HandChain chain("hw" + std::to_string(hops), hops, &latencies);
    chain.drive(kMessages);
    report_percentiles(state, latencies, hops);
  }
}

void run_interpreted(benchmark::State& state, int hops) {
  for (auto _ : state) {
    std::vector<std::uint64_t> latencies;
    latencies.reserve(kMessages);
    auto flow = make_flow("itp" + std::to_string(hops), hops, &latencies);
    tianshu::dsl::FlowRuntime rt;
    drive_runtime(rt, flow, kMessages);
    report_percentiles(state, latencies, hops);
  }
}

void run_compiled(benchmark::State& state, int hops) {
  for (auto _ : state) {
    std::vector<std::uint64_t> latencies;
    latencies.reserve(kMessages);
    auto flow = make_flow("cmp" + std::to_string(hops), hops, &latencies);
    tianshu::compiler::CompileOptions opts;
    opts.cache_dir = "/tmp/tianshu-h2-cache";
    auto compiled = tianshu::compiler::Pipeline::compile(flow, opts);
    tianshu::dsl::FlowRuntime rt;
    // 0ms: install the artifact's wiring without driving the timer
    // source (its emit stamps born_ns=0, which would poison percentiles).
    compiled.run(rt, flow, std::chrono::milliseconds(0));
    const std::string& channel = flow.sources().front().channel;
    for (int i = 0; i < kMessages; ++i) {
      const BenchMsg msg{.born_ns = now_ns(), .seq = static_cast<std::uint64_t>(i)};
      rt.publish_bytes(channel, &msg, sizeof(msg),
                      Lineage::rooted(channel, static_cast<std::uint64_t>(i)));
    }
    report_percentiles(state, latencies, hops);
  }
}

void run_handwritten_fanin(benchmark::State& state) {
  for (auto _ : state) {
    std::vector<std::uint64_t> latencies;
    latencies.reserve(kMessages);
    HandFanIn fanin("hwfi", &latencies);
    fanin.drive(kMessages);
    report_percentiles(state, latencies, 2);
  }
}

void run_interpreted_fanin(benchmark::State& state) {
  for (auto _ : state) {
    std::vector<std::uint64_t> latencies;
    latencies.reserve(kMessages);
    auto flow = make_fanin_flow("itpfi", &latencies);
    tianshu::dsl::FlowRuntime rt;
    drive_fanin(rt, flow, kMessages);
    report_percentiles(state, latencies, 2);
  }
}

void run_compiled_fanin(benchmark::State& state) {
  for (auto _ : state) {
    std::vector<std::uint64_t> latencies;
    latencies.reserve(kMessages);
    auto flow = make_fanin_flow("cmpfi", &latencies);
    tianshu::compiler::CompileOptions opts;
    opts.cache_dir = "/tmp/tianshu-h2-cache";
    auto compiled = tianshu::compiler::Pipeline::compile(flow, opts);
    tianshu::dsl::FlowRuntime rt;
    compiled.run(rt, flow, std::chrono::milliseconds(0));
    publish_fanin(rt, flow, kMessages);
    report_percentiles(state, latencies, 2);
  }
}

void run_handwritten_fanout(benchmark::State& state) {
  for (auto _ : state) {
    std::vector<std::uint64_t> latencies;
    latencies.reserve(4 * static_cast<std::size_t>(kMessages));
    HandFanOut fanout("hwfo", &latencies);
    fanout.drive(kMessages);
    report_percentiles(state, latencies, 1);
  }
}

void run_interpreted_fanout(benchmark::State& state) {
  for (auto _ : state) {
    std::vector<std::uint64_t> latencies;
    latencies.reserve(4 * static_cast<std::size_t>(kMessages));
    auto flow = make_fanout_flow("itpfo", &latencies);
    tianshu::dsl::FlowRuntime rt;
    drive_runtime(rt, flow, kMessages);
    report_percentiles(state, latencies, 1);
  }
}

void run_compiled_fanout(benchmark::State& state) {
  for (auto _ : state) {
    std::vector<std::uint64_t> latencies;
    latencies.reserve(4 * static_cast<std::size_t>(kMessages));
    auto flow = make_fanout_flow("cmpfo", &latencies);
    tianshu::compiler::CompileOptions opts;
    opts.cache_dir = "/tmp/tianshu-h2-cache";
    auto compiled = tianshu::compiler::Pipeline::compile(flow, opts);
    tianshu::dsl::FlowRuntime rt;
    compiled.run(rt, flow, std::chrono::milliseconds(0));
    const std::string& channel = flow.sources().front().channel;
    for (int i = 0; i < kMessages; ++i) {
      const BenchMsg msg{.born_ns = now_ns(), .seq = static_cast<std::uint64_t>(i)};
      rt.publish_bytes(channel, &msg, sizeof(msg),
                       Lineage::rooted(channel, static_cast<std::uint64_t>(i)));
    }
    report_percentiles(state, latencies, 1);
  }
}

// Roadmap 1.3 shapes: short (2 ops = 1 map + sink), medium (5), long (10);
// fan-in ((A,B,C) -> D intent; binary join -> J1(A,B) + J2(J1,C));
// fan-out (A -> (B,C,D,E), every delivery counted).
BENCHMARK_CAPTURE(run_handwritten, short, 1);
BENCHMARK_CAPTURE(run_interpreted, short, 1);
BENCHMARK_CAPTURE(run_compiled, short, 1);
BENCHMARK_CAPTURE(run_handwritten, medium, 4);
BENCHMARK_CAPTURE(run_interpreted, medium, 4);
BENCHMARK_CAPTURE(run_compiled, medium, 4);
BENCHMARK_CAPTURE(run_handwritten, long, 9);
BENCHMARK_CAPTURE(run_interpreted, long, 9);
BENCHMARK_CAPTURE(run_compiled, long, 9);
BENCHMARK(run_handwritten_fanin);
BENCHMARK(run_interpreted_fanin);
BENCHMARK(run_compiled_fanin);
BENCHMARK(run_handwritten_fanout);
BENCHMARK(run_interpreted_fanout);
BENCHMARK(run_compiled_fanout);

BENCHMARK_MAIN();
