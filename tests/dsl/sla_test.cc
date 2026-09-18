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

// SLA compilation v0 acceptance tests (ADR-0029 acceptance section).

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "tianshu/core/lineage.h"
#include "tianshu/core/message_traits.h"
#include "tianshu/dsl/dsl_runtime.h"
#include "tianshu/dsl/flow.h"
#include "tianshu/sla/sla_analyzer.h"
#include "tianshu/sla/sla_stats.h"

// NOLINTNEXTLINE(misc-use-internal-linkage)  // traits must precede template use
struct TickMsg {
  std::uint64_t tick;
};
// NOLINTNEXTLINE(misc-use-internal-linkage)  // same ordering constraint
struct DetectMsg {
  std::uint64_t tick;
};
// NOLINTNEXTLINE(misc-use-internal-linkage)  // same ordering constraint
struct FuseMsg {
  std::uint64_t tick;
};

TIANSHU_TRAITS_POD(TickMsg, "sla.TickMsg");
TIANSHU_TRAITS_POD(DetectMsg, "sla.DetectMsg");
TIANSHU_TRAITS_POD(FuseMsg, "sla.FuseMsg");

namespace {

// 1. Satisfiable chain: budgets sum to the deadline and split
//    proportionally to the declared WCETs (std::chrono::milliseconds(8) :
//    std::chrono::milliseconds(3) -> 36363 : 13636).
TEST(SlaTest, SatisfiableChainAllocatesBudgets) {
  const auto flow =
      tianshu::dsl::FlowBuilder("sla_ok")
          .source<TickMsg>("cam", std::chrono::milliseconds(10),
                           [](std::uint64_t t) { return TickMsg{.tick = t}; })
          .map<DetectMsg>([](const TickMsg& in) { return DetectMsg{.tick = in.tick}; })
          .with_wcet(std::chrono::milliseconds(8))
          .map<FuseMsg>([](const DetectMsg& in) { return FuseMsg{.tick = in.tick}; })
          .with_wcet(std::chrono::milliseconds(3))
          .sink([](const FuseMsg&, const tianshu::core::Lineage&) {})
          .with_sla(tianshu::sla::Sla{.deadline = std::chrono::milliseconds(50)})
          .build();

  ASSERT_TRUE(flow.sla_report().ok);
  ASSERT_EQ(flow.sla_report().budgets.size(), 2U);
  EXPECT_TRUE(flow.sla_report().default_wcet_notes.empty());

  std::int64_t total = 0;
  std::int64_t detect_budget = 0;
  for (const auto& b : flow.sla_report().budgets) {
    total += b.planned.count();
    if (b.planned.count() > 30000) {
      detect_budget = b.planned.count();
    }
  }
  // Integer truncation in the proportional split may lose 1us.
  EXPECT_NEAR(static_cast<double>(total), 50000.0, 1.0);
  EXPECT_NEAR(static_cast<double>(detect_budget), 36363.6, 1.0);

  // The verified flow must also actually run.
  tianshu::dsl::FlowRuntime runtime;
  runtime.run_for(flow, std::chrono::milliseconds(50));
}

// 2. Overrun (11.04ms planned vs 10ms deadline): build() fails fast and
//    the error carries the offending path and the worst offender.
TEST(SlaTest, OverrunThrowsWithDetail) {
  bool threw = false;
  try {
    static_cast<void>(
        tianshu::dsl::FlowBuilder("sla_bad")
            .source<TickMsg>("cam", std::chrono::milliseconds(10),
                             [](std::uint64_t t) { return TickMsg{.tick = t}; })
            .map<DetectMsg>([](const TickMsg& in) { return DetectMsg{.tick = in.tick}; })
            .with_wcet(std::chrono::milliseconds(8))
            .map<FuseMsg>([](const DetectMsg& in) { return FuseMsg{.tick = in.tick}; })
            .with_wcet(std::chrono::milliseconds(3))
            .sink([](const FuseMsg&, const tianshu::core::Lineage&) {})
            .with_sla(tianshu::sla::Sla{.deadline = std::chrono::milliseconds(10)})
            .build());
  } catch (const std::runtime_error& e) {
    threw = true;
    const std::string msg = e.what();
    EXPECT_NE(msg.find("cam"), std::string::npos);
    EXPECT_NE(msg.find("worst offender"), std::string::npos);
    EXPECT_NE(msg.find("map ->"), std::string::npos);
  }
  EXPECT_TRUE(threw);
}

// 3. Join backtracking: the std::chrono::milliseconds(8) branch (not the
// std::chrono::milliseconds(1) one) decides L, so
//    only its map lands on the critical path; the join node itself runs
//    on the default WCET and is named in the notes.
TEST(SlaTest, JoinTakesCriticalBranch) {
  tianshu::dsl::FlowBuilder b("sla_join");
  auto branch_a = b.source<TickMsg>("a", std::chrono::milliseconds(10),
                                    [](std::uint64_t t) { return TickMsg{.tick = t}; });
  auto a_mapped =
      branch_a.map<DetectMsg>([](const TickMsg& in) { return DetectMsg{.tick = in.tick}; });
  a_mapped.with_wcet(std::chrono::milliseconds(8));
  auto branch_b = b.source<TickMsg>("b2", std::chrono::milliseconds(10),
                                    [](std::uint64_t t) { return TickMsg{.tick = t}; });
  auto b_mapped =
      branch_b.map<DetectMsg>([](const TickMsg& in) { return DetectMsg{.tick = in.tick}; });
  b_mapped.with_wcet(std::chrono::milliseconds(1));
  auto joined = b.join<DetectMsg, DetectMsg, FuseMsg>(
      a_mapped, b_mapped,
      [](const DetectMsg& x, const DetectMsg& y) { return FuseMsg{.tick = x.tick + y.tick}; });
  joined.sink([](const FuseMsg&, const tianshu::core::Lineage&) {});
  joined.with_sla(tianshu::sla::Sla{.deadline = std::chrono::milliseconds(30)});

  const auto flow = b.build();
  EXPECT_TRUE(flow.sla_report().ok);
  ASSERT_EQ(flow.sla_report().budgets.size(), 2U);             // join out + critical map
  ASSERT_EQ(flow.sla_report().default_wcet_notes.size(), 1U);  // the join node
  EXPECT_NE(flow.sla_report().default_wcet_notes[0].find("join"), std::string::npos);
}

// 4. Saturation: warning by default, load rejected when strict.
TEST(SlaTest, SaturationWarnsThenRejectsWhenStrict) {
  const auto make = [](bool strict) {
    tianshu::dsl::FlowBuilder b("sla_sat");
    b.with_sla_config(
        tianshu::sla::SlaConfig{.machine_cores = 1, .margin = 0.2, .strict_utilization = strict});
    b.source<TickMsg>("hot", std::chrono::milliseconds(1),
                      [](std::uint64_t t) { return TickMsg{.tick = t}; })
        .map<DetectMsg>([](const TickMsg& in) { return DetectMsg{.tick = in.tick}; })
        .with_wcet(std::chrono::milliseconds(2))  // 2ms per 1ms period, 1 core: U = 2.0
        .sink([](const DetectMsg&, const tianshu::core::Lineage&) {})
        .with_sla(tianshu::sla::Sla{.deadline = std::chrono::milliseconds(5)});
    return b.build();
  };

  const auto lenient = make(false);
  EXPECT_TRUE(lenient.sla_report().ok);
  EXPECT_FALSE(lenient.sla_report().saturation_warning.empty());

  EXPECT_THROW(static_cast<void>(make(true)), std::runtime_error);
}

// 5. Undeclared WCET: default applied and named in the notes.
TEST(SlaTest, DefaultWcetIsReported) {
  const auto flow =
      tianshu::dsl::FlowBuilder("sla_default")
          .source<TickMsg>("cam", std::chrono::milliseconds(10),
                           [](std::uint64_t t) { return TickMsg{.tick = t}; })
          .map<DetectMsg>([](const TickMsg& in) { return DetectMsg{.tick = in.tick}; })
          .sink([](const DetectMsg&, const tianshu::core::Lineage&) {})
          .with_sla(tianshu::sla::Sla{.deadline = std::chrono::milliseconds(50)})
          .build();

  EXPECT_TRUE(flow.sla_report().ok);
  ASSERT_EQ(flow.sla_report().default_wcet_notes.size(), 1U);
  EXPECT_NE(flow.sla_report().default_wcet_notes[0].find("map"), std::string::npos);
}

// 6. No SLA declarations: analysis does not run, report stays empty.
TEST(SlaTest, NoEndpointsNoAnalysis) {
  const auto flow =
      tianshu::dsl::FlowBuilder("sla_free")
          .source<TickMsg>("cam", std::chrono::milliseconds(10),
                           [](std::uint64_t t) { return TickMsg{.tick = t}; })
          .map<DetectMsg>([](const TickMsg& in) { return DetectMsg{.tick = in.tick}; })
          .sink([](const DetectMsg&, const tianshu::core::Lineage&) {})
          .build();

  EXPECT_TRUE(flow.sla_report().ok);
  EXPECT_TRUE(flow.sla_report().budgets.empty());
  EXPECT_TRUE(flow.sla_report().default_wcet_notes.empty());
}

// 7. Runtime defense (ADR-0029 D6): a real run measures per-message e2e
//    at the endpoint; a generous deadline records zero misses.
TEST(SlaTest, RuntimeHistogramsMeasureE2E) {
  const auto flow =
      tianshu::dsl::FlowBuilder("sla_rt")
          .source<TickMsg>("cam", std::chrono::milliseconds(10),
                           [](std::uint64_t t) { return TickMsg{.tick = t}; })
          .map<DetectMsg>([](const TickMsg& in) { return DetectMsg{.tick = in.tick}; })
          .sink([](const DetectMsg&, const tianshu::core::Lineage&) {})
          .with_sla(tianshu::sla::Sla{.deadline = std::chrono::milliseconds(50)})
          .build();

  tianshu::dsl::FlowRuntime runtime;
  runtime.run_for(flow, std::chrono::milliseconds(150));

  const auto stats = runtime.sla_snapshot();
  ASSERT_EQ(stats.size(), 1U);
  EXPECT_EQ(stats[0].deadline, std::chrono::milliseconds(50));
  EXPECT_GT(stats[0].count, 0U);
  EXPECT_EQ(stats[0].miss_count, 0U);
}

// 8. Misses counted: a map that sleeps past the deadline guarantees
//    every message misses (sleep is a lower bound on e2e).
TEST(SlaTest, RuntimeMissCountedWhenDeadlineExceeded) {
  const auto flow = tianshu::dsl::FlowBuilder("sla_miss")
                        .source<TickMsg>("cam", std::chrono::milliseconds(10),
                                         [](std::uint64_t t) { return TickMsg{.tick = t}; })
                        .map<DetectMsg>([](const TickMsg& in) {
                          std::this_thread::sleep_for(std::chrono::milliseconds(3));
                          return DetectMsg{.tick = in.tick};
                        })
                        .sink([](const DetectMsg&, const tianshu::core::Lineage&) {})
                        .with_sla(tianshu::sla::Sla{.deadline = std::chrono::milliseconds(1)})
                        .build();

  tianshu::dsl::FlowRuntime runtime;
  runtime.run_for(flow, std::chrono::milliseconds(150));

  const auto stats = runtime.sla_snapshot();
  ASSERT_EQ(stats.size(), 1U);
  ASSERT_GT(stats[0].count, 0U);
  EXPECT_EQ(stats[0].miss_count, stats[0].count);
}

// 9. Collector unit: log-scale bucket floors, miss accounting, and the
//    non-endpoint no-op contract.
TEST(SlaTest, CollectorBucketsAndMisses) {
  tianshu::sla::SlaStatsCollector collector;
  collector.add_endpoint("ep", std::chrono::milliseconds(4));
  for (int i = 0; i < 100; ++i) {
    collector.record_if_endpoint("ep", std::chrono::milliseconds(5));
  }
  collector.record_if_endpoint("not/registered", std::chrono::milliseconds(1));

  const auto snapshot = collector.snapshot();
  ASSERT_EQ(snapshot.size(), 1U);
  EXPECT_EQ(snapshot[0].count, 100U);
  EXPECT_EQ(snapshot[0].miss_count, 100U);
  // 5ms lands in [1ms, 10ms): percentile floors are the bucket edge.
  EXPECT_EQ(snapshot[0].p50, std::chrono::nanoseconds(1000000));
  EXPECT_EQ(snapshot[0].p99, std::chrono::nanoseconds(1000000));
}

// 10. Flows without declarations never arm the collector.
TEST(SlaTest, RuntimeSnapshotEmptyWithoutEndpoints) {
  const auto flow = tianshu::dsl::FlowBuilder("sla_none")
                        .source<TickMsg>("cam", std::chrono::milliseconds(10),
                                         [](std::uint64_t t) { return TickMsg{.tick = t}; })
                        .sink([](const TickMsg&, const tianshu::core::Lineage&) {})
                        .build();

  tianshu::dsl::FlowRuntime runtime;
  runtime.run_for(flow, std::chrono::milliseconds(50));
  EXPECT_TRUE(runtime.sla_snapshot().empty());
}

// 11. Backtracker state isolation: each endpoint's worst path is
//     computed independently. Regression: best_ survived across
//     worst_path() calls, so the shallow endpoint analyzed second
//     inherited the deep endpoint's 8ms path and failed its 3ms
//     deadline as a false violation.
TEST(SlaTest, EndpointsDoNotShareWorstPath) {
  const std::vector<tianshu::sla::SlaSource> sources = {
      {.channel = "deep/src", .interval = std::chrono::milliseconds(100)},
      {.channel = "shallow/src", .interval = std::chrono::milliseconds(100)},
  };
  const std::vector<tianshu::sla::SlaNode> nodes = {
      {.kind = "map",
       .in_channels = {"deep/src"},
       .out_channel = "deep/out",
       .wcet = std::chrono::milliseconds(8)},
      {.kind = "map",
       .in_channels = {"shallow/src"},
       .out_channel = "shallow/out",
       .wcet = std::chrono::milliseconds(1)},
  };
  const std::vector<tianshu::sla::SlaEndpoint> endpoints = {
      {.channel = "deep/out", .deadline = std::chrono::milliseconds(50)},
      {.channel = "shallow/out", .deadline = std::chrono::milliseconds(3)},
  };

  const auto report =
      tianshu::sla::SlaAnalyzer::analyze(sources, nodes, endpoints, tianshu::sla::SlaConfig{});

  ASSERT_TRUE(report.ok);
  EXPECT_TRUE(report.violations.empty());
  ASSERT_EQ(report.budgets.size(), 2U);
  for (const auto& b : report.budgets) {
    if (b.channel == "deep/out") {
      EXPECT_EQ(b.planned, std::chrono::microseconds(50000));
    } else if (b.channel == "shallow/out") {
      EXPECT_EQ(b.planned, std::chrono::microseconds(3000));
    }
  }
}

}  // namespace
