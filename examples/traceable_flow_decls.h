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

// Shared declarations for the traceable-flow demo (ADR-0030 M-D): the
// standalone demo binary and the --flows provider .so both build from
// this header, so `ti-compile`/`ti-launch` resolve by name the exact
// "demo_traceable" flow the demo binary runs.

#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>

#include "tianshu/core/lineage.h"
#include "tianshu/core/message_traits.h"
#include "tianshu/dsl/dsl_runtime.h"  // NOLINT(misc-include-cleaner)  // specialize-hook template definitions
#include "tianshu/dsl/flow.h"
#include "tianshu/sla/sla_analyzer.h"

// NOLINTNEXTLINE(misc-use-internal-linkage)  // traits must precede template use
struct DemoTick {
  std::uint64_t tick{0};
};
// NOLINTNEXTLINE(misc-use-internal-linkage)  // same ordering constraint
struct DemoDoubled {
  std::uint64_t tick{0};
  double value{0.0};
};

TIANSHU_TRAITS_POD(DemoTick, "trace.DemoTick")
TIANSHU_TRAITS_POD(DemoDoubled, "trace.DemoDoubled")

inline void declare_demo_flow(tianshu::dsl::FlowBuilder& b) {
  b.source<DemoTick>("ticks", std::chrono::milliseconds(5),
                     [](std::uint64_t t) { return DemoTick{.tick = t}; })
      .map<DemoDoubled>([](const DemoTick& in) {
        return DemoDoubled{.tick = in.tick, .value = static_cast<double>(in.tick) * 2};
      })
      .with_wcet(std::chrono::microseconds(80))
      .sink([](const DemoDoubled& msg, const tianshu::core::Lineage& lin) {
        if (msg.tick < 3) {
          static_cast<void>(std::printf("[sink] tick=%llu value=%.1f  %s\n",
                                        static_cast<unsigned long long>(msg.tick), msg.value,
                                        lin.describe().c_str()));
        }
      })
      .with_sla(tianshu::sla::Sla{.deadline = std::chrono::milliseconds(20)})
      .with_fallback("demo_traceable_lite");
}

inline void declare_demo_flow_lite(tianshu::dsl::FlowBuilder& b) {
  b.source<DemoTick>("lite_ticks", std::chrono::milliseconds(50), [](std::uint64_t t) {
     return DemoTick{.tick = t};
   }).sink([](const DemoTick&, const tianshu::core::Lineage&) {});
}
