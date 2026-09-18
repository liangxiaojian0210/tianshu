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
#include <thread>
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
using tianshu::dsl::SourceEntry;

// Thread-state warmer (compiler.md §6.1 protocol v2): one pthread_create
// here permanently clears glibc's __libc_single_threaded, putting every
// std::mutex and shared_ptr refcount in this process on the atomic path —
// the state a production host (source threads, watchers) always runs in.
// Warming up front makes the state uniform across all benchmarks, so the
// verdict no longer depends on benchmark registration order.
const bool kThreadStateWarmed = [] {
  std::thread warmer([] {});
  warmer.join();
  return true;
}();

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
//
// Concurrency equivalence (2026-09-17 ruling: the comparison premise is
// equivalence): the DSL declares one thread per source (ADR-0021), so the
// handwritten rig must pay real synchronization too. Each join state
// transition runs under a std::mutex; roots are handed over through
// pending slots under the same lock (the root and the message are staged
// separately, so an unlocked window would let a concurrent pairing
// consume a stale root). Lock order is j1 -> j2 (a join's dispatch fires
// downstream joins synchronously); no path takes them in reverse.
struct HandJoin {
  CacheBuffer<BenchMsg> in_a{16};
  CacheBuffer<BenchMsg> in_b{16};
  std::optional<BenchMsg> slot_a;
  std::optional<BenchMsg> slot_b;
  Lineage lin_a;
  Lineage lin_b;
  Lineage pending_a;
  Lineage pending_b;
  const std::string* out_ch{nullptr};
  std::uint64_t out_seq{0};
  std::mutex mutex;

  void stage_root_a(Lineage root) {
    const std::scoped_lock lock(mutex);
    pending_a = std::move(root);
  }

  void stage_root_b(Lineage root) {
    const std::scoped_lock lock(mutex);
    pending_b = std::move(root);
  }

  template <typename Emit>
  void step_a(Emit&& emit) {
    const std::scoped_lock lock(mutex);
    while (const BenchMsg* p = in_a.try_fetch()) {
      slot_a = *p;
      lin_a = std::move(pending_a);
      pair_check(emit);
    }
  }

  template <typename Emit>
  void step_b(Emit&& emit) {
    const std::scoped_lock lock(mutex);
    while (const BenchMsg* p = in_b.try_fetch()) {
      slot_b = *p;
      lin_b = std::move(pending_b);
      pair_check(emit);
    }
  }

  template <typename Emit>
  void pair_check(Emit&& emit) {
    if (!slot_a.has_value() || !slot_b.has_value()) {
      return;
    }
    BenchMsg out = fuse(*slot_a, *slot_b);
    Lineage merged = std::move(lin_a);
    merged.merge(lin_b);
    merged.add_hop({.channel = *out_ch, .seq = ++out_seq});
    slot_a.reset();
    slot_b.reset();
    emit(std::move(out), std::move(merged));
  }
};

// Fan-in sink capture: the fused output can fire from any producer
// thread, so the latency vector append is guarded (both sides pay this).
struct FanInSink {
  explicit FanInSink(std::vector<std::uint64_t>* lats) : latencies(lats) {}

  void record(std::uint64_t lat) {
    const std::scoped_lock lock(mutex);
    latencies->push_back(lat);
  }

  std::mutex mutex;
  std::vector<std::uint64_t>* latencies;
};

// Handwritten fan-in: J1(A,B) -> J2(J1,C) -> sink, driven with one
// thread per source — the declared semantics (ADR-0021; equivalence
// ruling 2026-09-17). Emit callbacks run under the emitting join's
// lock; the j1 -> j2 root hand-off and downstream dispatch therefore
// follow the fixed lock order j1 -> j2 -> sink.
class HandFanIn {
 public:
  HandFanIn(const std::string& prefix, FanInSink* sink) : sink_(*sink), owner_(this) {
    auto& dispatcher = DataDispatcher::instance();
    j1_id_ = tianshu::core::channel_id_for(prefix + "/j1");
    out_id_ = tianshu::core::channel_id_for(prefix + "/out");
    j1_out_ch_ = prefix + "/j1";
    out_ch_ = prefix + "/out";
    j1_.out_ch = &j1_out_ch_;
    j2_.out_ch = &out_ch_;
    a_id_ = tianshu::core::channel_id_for(prefix + "/a");
    b_id_ = tianshu::core::channel_id_for(prefix + "/b");
    c_id_ = tianshu::core::channel_id_for(prefix + "/c");
    ch_a_ = prefix + "/a";
    ch_b_ = prefix + "/b";
    ch_c_ = prefix + "/c";
    const auto j1_emit = [this](BenchMsg out, Lineage merged) {
      j2_.stage_root_a(std::move(merged));
      DataDispatcher::instance().dispatch(j1_id_, &out, sizeof(out));
    };
    const auto j2_emit = [this](BenchMsg out, const Lineage& /*merged*/) {
      DataDispatcher::instance().dispatch(out_id_, &out, sizeof(out));
    };
    dispatcher.add_buffer(a_id_, &j1_.in_a, [this, j1_emit] { j1_.step_a(j1_emit); }, owner_);
    dispatcher.add_buffer(b_id_, &j1_.in_b, [this, j1_emit] { j1_.step_b(j1_emit); }, owner_);
    dispatcher.add_buffer(j1_id_, &j2_.in_a, [this, j2_emit] { j2_.step_a(j2_emit); }, owner_);
    dispatcher.add_buffer(c_id_, &j2_.in_b, [this, j2_emit] { j2_.step_b(j2_emit); }, owner_);
    dispatcher.add_buffer(out_id_, &sink_buf_,
                          [this] {
                            while (const BenchMsg* msg = sink_buf_.try_fetch()) {
                              sink_.record(now_ns() - msg->born_ns);
                            }
                          },
                          owner_);
  }

  ~HandFanIn() { DataDispatcher::instance().remove_owner(owner_); }

  void drive(std::size_t messages) {
    const auto loop = [](const std::string& ch, std::uint64_t id, auto&& stage, std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) {
        const BenchMsg msg{.born_ns = now_ns(), .seq = static_cast<std::uint64_t>(i)};
        stage(Lineage::rooted(ch, static_cast<std::uint64_t>(i)));
        DataDispatcher::instance().dispatch(id, &msg, sizeof(msg));
      }
    };
    std::vector<std::thread> sources;
    sources.emplace_back([&] { loop(ch_a_, a_id_, [this](Lineage r) { j1_.stage_root_a(std::move(r)); }, messages); });
    sources.emplace_back([&] { loop(ch_b_, b_id_, [this](Lineage r) { j1_.stage_root_b(std::move(r)); }, messages); });
    sources.emplace_back([&] { loop(ch_c_, c_id_, [this](Lineage r) { j2_.stage_root_b(std::move(r)); }, messages); });
    for (auto& thread : sources) {
      thread.join();
    }
  }

 private:
  std::string ch_a_;
  std::string ch_b_;
  std::string ch_c_;
  std::string j1_out_ch_;
  std::string out_ch_;
  std::uint64_t j1_id_{0};
  std::uint64_t out_id_{0};
  std::uint64_t a_id_{0};
  std::uint64_t b_id_{0};
  std::uint64_t c_id_{0};
  HandJoin j1_;
  HandJoin j2_;
  CacheBuffer<BenchMsg> sink_buf_{16};
  FanInSink& sink_;
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

tianshu::dsl::Flow make_fanin_flow(const std::string& name, FanInSink* sink) {
  tianshu::dsl::FlowBuilder b(name);
  const auto emit = [](std::uint64_t t) { return BenchMsg{.born_ns = 0, .seq = t}; };
  auto sa = b.source<BenchMsg>("a", std::chrono::milliseconds(1), emit);
  auto sb = b.source<BenchMsg>("b", std::chrono::milliseconds(1), emit);
  auto sc = b.source<BenchMsg>("c", std::chrono::milliseconds(1), emit);
  auto j1 = b.join<BenchMsg, BenchMsg, BenchMsg>(sa, sb, fuse);
  auto j2 = b.join<BenchMsg, BenchMsg, BenchMsg>(j1, sc, fuse);
  j2.sink([sink](const BenchMsg& msg, const Lineage&) { sink->record(now_ns() - msg.born_ns); });
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

// Fan-in drives run one thread per source (declared semantics,
// ADR-0021; equivalence ruling 2026-09-17) on every implementation.
void drive_fanin(tianshu::dsl::FlowRuntime& rt, const tianshu::dsl::Flow& flow, int messages) {
  rt.wire(flow);
  std::vector<std::thread> sources;
  for (const auto& source : flow.sources()) {
    sources.emplace_back([&rt, channel = source.channel, messages] {
      for (int i = 0; i < messages; ++i) {
        const auto seq = static_cast<std::uint64_t>(i);
        const BenchMsg msg{.born_ns = now_ns(), .seq = seq};
        rt.publish_bytes(channel, &msg, sizeof(msg), Lineage::rooted(channel, seq));
      }
    });
  }
  for (auto& thread : sources) {
    thread.join();
  }
}

// Compiled fan-in drive: typed source entries (ADR-0033), one thread per
// source — the driver-role mirror of the handwritten rig.
void publish_entries_fanin(tianshu::dsl::FlowRuntime& rt, const tianshu::dsl::Flow& flow,
                           int messages) {
  std::vector<std::thread> sources;
  for (const auto& source : flow.sources()) {
    sources.emplace_back([&rt, &flow, channel = source.channel, messages] {
      SourceEntry entry(rt, flow, channel);
      for (int i = 0; i < messages; ++i) {
        const auto seq = static_cast<std::uint64_t>(i);
        entry.publish(BenchMsg{.born_ns = now_ns(), .seq = seq}, seq);
      }
    });
  }
  for (auto& thread : sources) {
    thread.join();
  }
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
    SourceEntry entry(rt, flow, flow.sources().front().channel);
    for (int i = 0; i < kMessages; ++i) {
      entry.publish(BenchMsg{.born_ns = now_ns(), .seq = static_cast<std::uint64_t>(i)},
                    static_cast<std::uint64_t>(i));
    }
    report_percentiles(state, latencies, hops);
  }
}

void run_handwritten_fanin(benchmark::State& state) {
  for (auto _ : state) {
    std::vector<std::uint64_t> latencies;
    latencies.reserve(kMessages);
    FanInSink sink(&latencies);
    HandFanIn fanin("hwfi", &sink);
    fanin.drive(kMessages);
    report_percentiles(state, latencies, 2);
  }
}

void run_interpreted_fanin(benchmark::State& state) {
  for (auto _ : state) {
    std::vector<std::uint64_t> latencies;
    latencies.reserve(kMessages);
    FanInSink sink(&latencies);
    auto flow = make_fanin_flow("itpfi", &sink);
    tianshu::dsl::FlowRuntime rt;
    drive_fanin(rt, flow, kMessages);
    report_percentiles(state, latencies, 2);
  }
}

void run_compiled_fanin(benchmark::State& state) {
  for (auto _ : state) {
    std::vector<std::uint64_t> latencies;
    latencies.reserve(kMessages);
    FanInSink sink(&latencies);
    auto flow = make_fanin_flow("cmpfi", &sink);
    tianshu::compiler::CompileOptions opts;
    opts.cache_dir = "/tmp/tianshu-h2-cache";
    auto compiled = tianshu::compiler::Pipeline::compile(flow, opts);
    tianshu::dsl::FlowRuntime rt;
    compiled.run(rt, flow, std::chrono::milliseconds(0));
    publish_entries_fanin(rt, flow, kMessages);
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
    SourceEntry entry(rt, flow, flow.sources().front().channel);
    for (int i = 0; i < kMessages; ++i) {
      entry.publish(BenchMsg{.born_ns = now_ns(), .seq = static_cast<std::uint64_t>(i)},
                    static_cast<std::uint64_t>(i));
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
