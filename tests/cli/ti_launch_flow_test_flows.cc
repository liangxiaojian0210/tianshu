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

// Provider .so for the ti-launch flow-mode tests: registers
// `tick_writer`, whose sink appends one line per message to the file
// named by the TI_LAUNCH_TEST_OUT environment variable.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "tianshu/core/lineage.h"
#include "tianshu/core/message_traits.h"
#include "tianshu/dsl/dsl_runtime.h"  // NOLINT(misc-include-cleaner)  // specialize-hook template definitions
#include "tianshu/dsl/flow.h"

// NOLINTNEXTLINE(misc-use-internal-linkage)  // traits must precede template use
struct LaunchTick {
  std::uint64_t tick{0};
};

TIANSHU_TRAITS_POD(LaunchTick, "cli.LaunchTick")

namespace {

[[maybe_unused]] void declare_tick_writer(tianshu::dsl::FlowBuilder& b) {
  b.source<LaunchTick>("launch_ticks", std::chrono::milliseconds(5),
                       [](std::uint64_t t) { return LaunchTick{.tick = t}; })
      .map<LaunchTick>([](const LaunchTick& in) { return in; })
      .sink([](const LaunchTick& msg, const tianshu::core::Lineage&) {
        const char* out = std::getenv("TI_LAUNCH_TEST_OUT");
        if (out == nullptr) {
          return;
        }
        std::FILE* f = std::fopen(out, "a");
        if (f == nullptr) {
          return;
        }
        static_cast<void>(
            std::fprintf(f, "tick %llu\n", static_cast<unsigned long long>(msg.tick)));
        static_cast<void>(std::fclose(f));
      });
}

}  // namespace

REGISTER_TRACEABLE_FLOW("tick_writer", declare_tick_writer)
