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

// H3 WCET calibration rig (ADR-0029 H3, plan task T5/T6).
//
// One process, one flow ("h3"), three independent branches sharing one
// DslRuntime, pumped synchronously from the main thread (v0 cascade):
//
//   micro: h3/micro/src -> m1 (~50us) -> m2 (~120us)  -> sink
//   milli: h3/milli/src -> m3 (~800us) -> m4 (~5ms)   -> sink
//   fanin: h3/fanin/a-src -> a (~60us)  -> j (~150us) -> sink
//          h3/fanin/b-src -> b (~600us) /
//
// Stage work is a deterministic fixed-iteration FNV-style integer mix
// seeded from the payload; the accumulator is written into the output
// payload, so the work is externally observable (no dead-code
// elimination) and no timing API is touched inside the kernels. The
// record file is then fed to `ti-info <file.trec> --calibrate`, which
// derives per-stage e2e percentiles from lineage timestamps. The H3
// verdict is ratio-based, so exact magnitudes are non-critical; the
// iteration constants below were calibrated with one unshielded dry
// run on the dev host and are then frozen.
//
// Wiring: wire_specialized() installs the declared graph (fast map and
// sink stages, ADR-0032 M-C shipping path). FlowBuilder::join() cannot
// name its output channel, and the calibration table keys stages by
// OUTPUT channel name, so the join is installed through the public
// attach_join()/attach_sink() entry points with explicit channels
// BEFORE wire_specialized() — the specialize plan's finalize() then
// binds the join's lineage queues into the fast stages' fan-outs.
//
// Startup discipline: one dummy std::thread is created and joined
// before any measurement (protocol v2 thread warmer — flips glibc
// __libc_single_threaded to the production-habitual state).

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <map>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "tianshu/core/lineage.h"
#include "tianshu/core/message_traits.h"
#include "tianshu/dsl/dsl_runtime.h"
#include "tianshu/dsl/flow.h"
#include "tianshu/sla/sla_analyzer.h"

// NOLINTNEXTLINE(misc-use-internal-linkage)  // traits must precede template use
struct H3Msg {
  std::uint64_t seq;   // pump-assigned sequence number
  std::uint64_t spin;  // spin-kernel accumulator (externally observable)
  std::uint8_t pad[48];
};

TIANSHU_TRAITS_POD(H3Msg, "h3.H3Msg");

namespace {

using tianshu::core::Lineage;
using tianshu::dsl::Flow;
using tianshu::dsl::FlowBuilder;
using tianshu::dsl::FlowRuntime;

// ---------------------------------------------------------------------------
// Deterministic spin kernels.
//
// Realized stage costs (calibrated 2026-09-18, one unshielded dry run on
// the dev host — heterogeneous AMD, desktop-release build, big-core
// scheduling as-run; constants frozen since — the H3 verdict is
// ratio-based, magnitudes only need to stay within ~2x):
//
//   stage  target(us)  iters      realized p50(us)
//   m1        50       38000      47.1
//   m2       120       90000     111.3
//   m3       800      600000     745.6
//   m4      5000     3750000    4656.2
//   a         60       45000      56.7
//   b        600      450000     561.1
//   j        150      112000     140.6
// ---------------------------------------------------------------------------
constexpr std::uint32_t kM1Iters = 38000;
constexpr std::uint32_t kM2Iters = 90000;
constexpr std::uint32_t kM3Iters = 600000;
constexpr std::uint32_t kM4Iters = 3750000;
constexpr std::uint32_t kAIters = 45000;
constexpr std::uint32_t kBIters = 450000;
constexpr std::uint32_t kJIters = 112000;

// Dry-run measured WCET defaults (us) for --sla when a stage has no
// --declare entry; realized p50s from the table above, rounded.
const std::map<std::string, std::chrono::microseconds> kDryRunWcetDefaults = {
    {"h3/micro/m1", std::chrono::microseconds{50}},
    {"h3/micro/m2", std::chrono::microseconds{110}},
    {"h3/milli/m3", std::chrono::microseconds{750}},
    {"h3/milli/m4", std::chrono::microseconds{4650}},
    {"h3/fanin/a", std::chrono::microseconds{55}},
    {"h3/fanin/b", std::chrono::microseconds{560}},
    {"h3/fanin/j", std::chrono::microseconds{140}},
};

// FNV-style integer mix: fixed iteration count, serial dependence chain
// (not vectorizable), accumulator returned for embedding into the
// output payload. No timing calls, no allocations.
[[nodiscard]] inline std::uint64_t spin_mix(std::uint64_t seed, std::uint32_t iters) {
  std::uint64_t acc = seed ^ 0x9e3779b97f4a7c15ULL;
  for (std::uint32_t i = 0; i < iters; ++i) {
    acc = (acc ^ (acc >> 29)) * 0x100000001b3ULL;
    acc += i;
  }
  return acc;
}

H3Msg make_msg(std::uint64_t seq, std::uint64_t spin) {
  H3Msg msg{};
  msg.seq = seq;
  msg.spin = spin;
  return msg;
}

H3Msg stage_op(const H3Msg& in, std::uint32_t iters, std::uint64_t salt) {
  return make_msg(in.seq, spin_mix(in.seq ^ salt, iters));
}

H3Msg join_op(const H3Msg& a, const H3Msg& b) {
  return make_msg(a.seq, spin_mix(a.spin ^ b.spin, kJIters));
}

struct BranchCounts {
  std::uint64_t micro = 0;
  std::uint64_t milli = 0;
  std::uint64_t fanin = 0;
};

struct PumpConfig {
  std::uint64_t micro_n = 200000;
  std::uint64_t milli_n = 12000;
  std::uint64_t fanin_n = 40000;
  std::string out_path;
  bool sla = false;
  std::map<std::string, std::chrono::microseconds> declared_wcet;
};

[[nodiscard]] std::chrono::microseconds wcet_of(
    const std::map<std::string, std::chrono::microseconds>& declared, const std::string& channel) {
  const auto it = declared.find(channel);
  if (it != declared.end()) {
    return it->second;
  }
  return kDryRunWcetDefaults.at(channel);
}

Flow build_h3_flow(const std::map<std::string, std::chrono::microseconds>& declared,
                   BranchCounts* counts) {
  FlowBuilder b("h3");

  const auto noop_emit = [](std::uint64_t) {
    return H3Msg{};
  };

  // micro branch: src -> m1 -> m2 -> sink
  auto micro_src = b.source<H3Msg>("micro/src", std::chrono::milliseconds(1), noop_emit);
  auto m1 = micro_src.map_to<H3Msg>("micro/m1",
                                    [](const H3Msg& in) { return stage_op(in, kM1Iters, 0x51); });
  m1.with_wcet(wcet_of(declared, "h3/micro/m1"));
  auto m2 =
      m1.map_to<H3Msg>("micro/m2", [](const H3Msg& in) { return stage_op(in, kM2Iters, 0x52); });
  m2.with_wcet(wcet_of(declared, "h3/micro/m2"));
  m2.sink([counts](const H3Msg& /*msg*/, const Lineage& /*lin*/) { ++counts->micro; });

  // milli branch: src -> m3 -> m4 -> sink
  auto milli_src = b.source<H3Msg>("milli/src", std::chrono::milliseconds(10), noop_emit);
  auto m3 = milli_src.map_to<H3Msg>("milli/m3",
                                    [](const H3Msg& in) { return stage_op(in, kM3Iters, 0x53); });
  m3.with_wcet(wcet_of(declared, "h3/milli/m3"));
  auto m4 =
      m3.map_to<H3Msg>("milli/m4", [](const H3Msg& in) { return stage_op(in, kM4Iters, 0x54); });
  m4.with_wcet(wcet_of(declared, "h3/milli/m4"));
  m4.sink([counts](const H3Msg& /*msg*/, const Lineage& /*lin*/) { ++counts->milli; });

  // fanin branch: a-src -> a -> j -> sink and b-src -> b -> j. The map
  // halves are declared here; the join cannot be (FlowBuilder::join()
  // mints anonymous channels, and the calibration table keys stages by
  // output channel name), so it is installed explicitly on the runtime
  // after build().
  auto a_src = b.source<H3Msg>("fanin/a-src", std::chrono::milliseconds(1), noop_emit);
  auto a =
      a_src.map_to<H3Msg>("fanin/a", [](const H3Msg& in) { return stage_op(in, kAIters, 0x55); });
  a.with_wcet(wcet_of(declared, "h3/fanin/a"));
  auto b_src = b.source<H3Msg>("fanin/b-src", std::chrono::milliseconds(1), noop_emit);
  auto bb =
      b_src.map_to<H3Msg>("fanin/b", [](const H3Msg& in) { return stage_op(in, kBIters, 0x56); });
  bb.with_wcet(wcet_of(declared, "h3/fanin/b"));

  return b.build();
}

void publish_one(FlowRuntime& rt, const std::string& channel, std::uint64_t seq) {
  const H3Msg msg = make_msg(seq, seq * 0x9e3779b97f4a7c15ULL);
  rt.publish_bytes(channel, &msg, sizeof(H3Msg), Lineage::rooted(channel, seq));
}

constexpr std::uint64_t kWarmupMessages = 2000;

void warm_branch(FlowRuntime& rt, const std::string& channel, std::uint64_t n) {
  const std::uint64_t warm = n < kWarmupMessages ? n : kWarmupMessages;
  for (std::uint64_t seq = 1; seq <= warm; ++seq) {
    publish_one(rt, channel, seq);
  }
}

int usage() {
  static_cast<void>(std::fprintf(
      stderr,
      "usage: h3_wcet_rig --out <path.trec> [--micro-n N] [--milli-n N] [--fanin-n N]\n"
      "                    [--declare out=us[,out=us...]] [--sla]\n"
      "  --out        record file path (required)\n"
      "  --micro-n    micro branch messages   (default 200000)\n"
      "  --milli-n    milli branch messages   (default 12000)\n"
      "  --fanin-n    fanin messages per source (default 40000)\n"
      "  --declare    declared WCETs by out channel, e.g. h3/micro/m1=50\n"
      "  --sla        run load-time SLA analysis and print the report\n"));
  return 1;
}

// Parses "out=us" pairs into the declared map. Strict: an unknown name
// is a hard error listing the valid stage keys (rc 1).
bool parse_declare(const std::string& spec, std::map<std::string, std::chrono::microseconds>* out) {
  const auto& valid = kDryRunWcetDefaults;
  std::size_t pos = 0;
  while (pos < spec.size()) {
    const std::size_t comma = spec.find(',', pos);
    const std::string pair =
        spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
    pos = comma == std::string::npos ? spec.size() : comma + 1;
    const std::size_t eq = pair.find('=');
    if (eq == std::string::npos || eq == 0 || eq + 1 >= pair.size()) {
      static_cast<void>(
          std::fprintf(stderr, "error: --declare: malformed pair '%s'\n", pair.c_str()));
      static_cast<void>(usage());
      return false;
    }
    const std::string name = pair.substr(0, eq);
    if (!valid.contains(name)) {
      static_cast<void>(
          std::fprintf(stderr, "error: --declare: unknown stage '%s'\n", name.c_str()));
      static_cast<void>(std::fprintf(stderr, "valid stages (out channels):\n"));
      for (const auto& [ch, us] : valid) {
        static_cast<void>(
            std::fprintf(stderr, "  %s=%lld\n", ch.c_str(), static_cast<long long>(us.count())));
      }
      return false;
    }
    const std::string value = pair.substr(eq + 1);
    const std::int64_t us = std::strtoll(value.c_str(), nullptr, 10);
    if (us <= 0) {
      static_cast<void>(
          std::fprintf(stderr, "error: --declare: '%s': us must be > 0\n", pair.c_str()));
      return false;
    }
    // A later duplicate of a name overwrites the earlier one.
    (*out)[name] = std::chrono::microseconds{us};
  }
  return true;
}

// Load-time SLA analysis (ADR-0029) over the same source/node shape
// FlowBuilder::build() lowers, with the join node included (the flow
// itself cannot declare it — see build_h3_flow). Deadline per endpoint:
// 4 x (sum of declared branch WCETs + hops x hop_cost).
int run_sla_and_report(const std::map<std::string, std::chrono::microseconds>& declared) {
  const std::vector<tianshu::sla::SlaSource> sources = {
      {.channel = "h3/micro/src", .interval = std::chrono::milliseconds(1)},
      {.channel = "h3/milli/src", .interval = std::chrono::milliseconds(10)},
      {.channel = "h3/fanin/a-src", .interval = std::chrono::milliseconds(1)},
      {.channel = "h3/fanin/b-src", .interval = std::chrono::milliseconds(1)},
  };
  const std::vector<tianshu::sla::SlaNode> nodes = {
      {.kind = "map",
       .in_channels = {"h3/micro/src"},
       .out_channel = "h3/micro/m1",
       .wcet = wcet_of(declared, "h3/micro/m1")},
      {.kind = "map",
       .in_channels = {"h3/micro/m1"},
       .out_channel = "h3/micro/m2",
       .wcet = wcet_of(declared, "h3/micro/m2")},
      {.kind = "map",
       .in_channels = {"h3/milli/src"},
       .out_channel = "h3/milli/m3",
       .wcet = wcet_of(declared, "h3/milli/m3")},
      {.kind = "map",
       .in_channels = {"h3/milli/m3"},
       .out_channel = "h3/milli/m4",
       .wcet = wcet_of(declared, "h3/milli/m4")},
      {.kind = "map",
       .in_channels = {"h3/fanin/a-src"},
       .out_channel = "h3/fanin/a",
       .wcet = wcet_of(declared, "h3/fanin/a")},
      {.kind = "map",
       .in_channels = {"h3/fanin/b-src"},
       .out_channel = "h3/fanin/b",
       .wcet = wcet_of(declared, "h3/fanin/b")},
      {.kind = "join",
       .in_channels = {"h3/fanin/a", "h3/fanin/b"},
       .out_channel = "h3/fanin/j",
       .wcet = wcet_of(declared, "h3/fanin/j")},
  };
  const auto branch_micro = wcet_of(declared, "h3/micro/m1") + wcet_of(declared, "h3/micro/m2");
  const auto branch_milli = wcet_of(declared, "h3/milli/m3") + wcet_of(declared, "h3/milli/m4");
  const auto branch_fanin = wcet_of(declared, "h3/fanin/a") + wcet_of(declared, "h3/fanin/b") +
                            wcet_of(declared, "h3/fanin/j");
  const std::chrono::microseconds hop = std::chrono::microseconds(20);  // SlaConfig default
  constexpr int kBranchHops = 3;  // src -> stage(s) -> endpoint sink edge
  // Endpoint order no longer affects correctness: the analyzer's
  // Backtracker resets its best-path state per endpoint (worst_path in
  // sla_analyzer.cc; regression SlaTest.EndpointsDoNotShareWorstPath).
  // The ascending-deadline sort is kept only for deterministic report
  // output across runs.
  std::vector<tianshu::sla::SlaEndpoint> endpoints = {
      {.channel = "h3/micro/m2", .deadline = 4 * (branch_micro + kBranchHops * hop)},
      {.channel = "h3/fanin/j", .deadline = 4 * (branch_fanin + kBranchHops * hop)},
      {.channel = "h3/milli/m4", .deadline = 4 * (branch_milli + kBranchHops * hop)},
  };
  std::ranges::sort(endpoints,
                    [](const tianshu::sla::SlaEndpoint& x, const tianshu::sla::SlaEndpoint& y) {
                      return x.deadline < y.deadline;
                    });
  try {
    const tianshu::sla::SlaReport report =
        tianshu::sla::SlaAnalyzer::analyze(sources, nodes, endpoints, tianshu::sla::SlaConfig{});
    static_cast<void>(
        std::printf("\n--- SLA report (load-time, ADR-0029) ---\n%s\n", report.format().c_str()));
    if (!report.ok) {
      return 1;
    }
  } catch (const std::exception& e) {
    static_cast<void>(std::fprintf(stderr, "error: SLA analysis failed: %s\n", e.what()));
    return 1;
  }
  return 0;
}

// Parses argv into cfg. Returns 0 on success; on any error prints the
// reason plus usage and returns 1.
int parse_args(int argc, char** argv, PumpConfig* cfg) {
  bool out_given = false;
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const char* const val = i + 1 < argc ? argv[i + 1] : nullptr;
    if (arg == "--out" && val != nullptr) {
      cfg->out_path = val;
      out_given = true;
      ++i;
    } else if (arg == "--micro-n" && val != nullptr) {
      cfg->micro_n = std::strtoull(val, nullptr, 10);
      ++i;
    } else if (arg == "--milli-n" && val != nullptr) {
      cfg->milli_n = std::strtoull(val, nullptr, 10);
      ++i;
    } else if (arg == "--fanin-n" && val != nullptr) {
      cfg->fanin_n = std::strtoull(val, nullptr, 10);
      ++i;
    } else if (arg == "--declare" && val != nullptr) {
      if (!parse_declare(val, &cfg->declared_wcet)) {
        return 1;
      }
      ++i;
    } else if (arg == "--sla") {
      cfg->sla = true;
    } else {
      static_cast<void>(
          std::fprintf(stderr, "error: unknown or incomplete argument '%s'\n", arg.c_str()));
      return usage();
    }
  }
  // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  if (!out_given || cfg->out_path.empty()) {
    static_cast<void>(std::fprintf(stderr, "error: --out is required\n"));
    return usage();
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) try {
  PumpConfig cfg;
  if (parse_args(argc, argv, &cfg) != 0) {
    return 1;
  }

  // Protocol v2 thread warmer: flip glibc __libc_single_threaded to the
  // production-habitual state BEFORE any measurement.
  {
    std::thread warmer{[] {}};
    warmer.join();
  }

  BranchCounts counts;
  const auto t0 = std::chrono::steady_clock::now();

  const Flow flow = build_h3_flow(cfg.declared_wcet, &counts);
  FlowRuntime rt;

  // Named join first (explicit channels): its lineage queues register
  // before wire_specialized() runs, so the specialize plan's finalize()
  // binds them into the fast stages' fan-outs for h3/fanin/a and
  // h3/fanin/b. Input order is (b, a) on purpose: attach_join merges
  // in_a's branch first, the calibrate tool resolves a stage's input
  // from the SECOND-TO-LAST hop of the FRONT branch, and the b input
  // is the one that completes each pair — so the j row measures the
  // join itself (~150us), not b's spin plus the join.
  rt.attach_join<H3Msg, H3Msg, H3Msg>("h3/fanin/b", "h3/fanin/a", "h3/fanin/j",
                                      [](const H3Msg& b, const H3Msg& a) { return join_op(a, b); });
  rt.attach_sink<H3Msg>(
      "h3/fanin/j", [&counts](const H3Msg& /*msg*/, const Lineage& /*lin*/) { ++counts.fanin; });
  rt.wire_specialized(flow);

  // Warmup (before start_recording, so warmup messages are unrecorded).
  // The fanin branch alternates its two sources: a sequential a-then-b
  // warmup overflows the join's bounded buffers/queues and leaves stale
  // entries that misalign lineage with payload pairing.
  warm_branch(rt, "h3/micro/src", cfg.micro_n);
  warm_branch(rt, "h3/milli/src", cfg.milli_n);
  {
    const std::uint64_t warm = cfg.fanin_n < kWarmupMessages ? cfg.fanin_n : kWarmupMessages;
    for (std::uint64_t seq = 1; seq <= warm; ++seq) {
      publish_one(rt, "h3/fanin/a-src", seq);
      publish_one(rt, "h3/fanin/b-src", seq);
    }
  }
  counts = BranchCounts{};  // report measured-pump counts only

  // Measured pump: sequential per branch; the fanin branch strictly
  // alternates A,B so the join pairs (a_i, b_i) cleanly.
  rt.start_recording(cfg.out_path, tianshu::dsl::record::Compression::kLz4);
  for (std::uint64_t seq = 1; seq <= cfg.micro_n; ++seq) {
    publish_one(rt, "h3/micro/src", seq);
  }
  for (std::uint64_t seq = 1; seq <= cfg.milli_n; ++seq) {
    publish_one(rt, "h3/milli/src", seq);
  }
  for (std::uint64_t seq = 1; seq <= cfg.fanin_n; ++seq) {
    publish_one(rt, "h3/fanin/a-src", seq);
    publish_one(rt, "h3/fanin/b-src", seq);
  }
  const bool saved = rt.stop_recording();

  const auto t1 = std::chrono::steady_clock::now();
  const double elapsed_s =
      std::chrono::duration_cast<std::chrono::duration<double>>(t1 - t0).count();

  static_cast<void>(std::printf("== H3 WCET calibration rig ==\n"));
  static_cast<void>(std::printf("[micro] %llu messages pumped, %llu sunk\n",
                                static_cast<unsigned long long>(cfg.micro_n),
                                static_cast<unsigned long long>(counts.micro)));
  static_cast<void>(std::printf("[milli] %llu messages pumped, %llu sunk\n",
                                static_cast<unsigned long long>(cfg.milli_n),
                                static_cast<unsigned long long>(counts.milli)));
  static_cast<void>(std::printf("[fanin] %llu messages per source, %llu joined\n",
                                static_cast<unsigned long long>(cfg.fanin_n),
                                static_cast<unsigned long long>(counts.fanin)));
  static_cast<void>(
      std::printf("[record] %s (saved=%s)\n", cfg.out_path.c_str(), saved ? "yes" : "no"));
  static_cast<void>(std::printf("[elapsed] %.3f s (build+warmup+pump+record)\n", elapsed_s));

  if (cfg.sla) {
    return run_sla_and_report(cfg.declared_wcet);
  }
  return 0;
} catch (const std::exception& e) {
  static_cast<void>(std::fprintf(stderr, "error: %s\n", e.what()));
  return 1;
}
