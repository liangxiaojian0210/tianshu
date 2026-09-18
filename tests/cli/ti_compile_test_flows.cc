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

// Provider .so for the ti-compile CLI tests: registers two flows at
// static init so ti-compile can find them only after dlopening this
// library via --flows (the registry is process-local).

#include <chrono>
#include <cstdint>

#include "tianshu/core/lineage.h"
#include "tianshu/core/message_traits.h"
#include "tianshu/dsl/dsl_runtime.h"  // NOLINT(misc-include-cleaner)  // specialize-hook template definitions
#include "tianshu/dsl/flow.h"
#include "tianshu/sla/sla_analyzer.h"

// NOLINTNEXTLINE(misc-use-internal-linkage)  // traits must precede template use
struct CompileTick {
  std::uint64_t tick{0};
};
// NOLINTNEXTLINE(misc-use-internal-linkage)  // same ordering constraint
struct CompileDoubled {
  std::uint64_t tick{0};
  double value{0.0};
};

TIANSHU_TRAITS_POD(CompileTick, "cli.CompileTick")
TIANSHU_TRAITS_POD(CompileDoubled, "cli.CompileDoubled")

namespace {

// M-B acceptance shape: source -> map -> sink with WCET + SLA.
[[maybe_unused]] void declare_single_chain(tianshu::dsl::FlowBuilder& b) {
  b.source<CompileTick>("cc_ticks", std::chrono::milliseconds(5),
                        [](std::uint64_t t) { return CompileTick{.tick = t}; })
      .map<CompileDoubled>([](const CompileTick& in) {
        return CompileDoubled{.tick = in.tick, .value = static_cast<double>(in.tick) * 2.0};
      })
      .with_wcet(std::chrono::microseconds(80))
      .sink([](const CompileDoubled&, const tianshu::core::Lineage&) {})
      .with_sla(tianshu::sla::Sla{.deadline = std::chrono::milliseconds(20)});
}

// Two independent source -> sink chains: multi-source declaration in
// one flow.
[[maybe_unused]] void declare_echo_pair(tianshu::dsl::FlowBuilder& b) {
  b.source<CompileTick>("echo_a", std::chrono::milliseconds(7), [](std::uint64_t t) {
     return CompileTick{.tick = t};
   }).sink([](const CompileTick&, const tianshu::core::Lineage&) {});
  b.source<CompileTick>("echo_b", std::chrono::milliseconds(11), [](std::uint64_t t) {
     return CompileTick{.tick = t};
   }).sink([](const CompileTick&, const tianshu::core::Lineage&) {});
}

}  // namespace

REGISTER_TRACEABLE_FLOW("single_chain", declare_single_chain)
REGISTER_TRACEABLE_FLOW("echo_pair", declare_echo_pair)
