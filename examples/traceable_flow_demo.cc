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

// Traceable-flow demo (ADR-0030 M-D): the flow declares itself via
// REGISTER_TRACEABLE_FLOW; the launcher discovers it BY NAME, builds
// (the build is the dry-run trace: SLA runs, the graph materializes),
// compiles it to a .so artifact, and runs the compiled wiring. The
// README's target-API first line is now real code.

#include <chrono>
#include <cstdio>
#include <exception>
#include <string>

#include "tianshu/compiler/ir.h"
#include "tianshu/compiler/pipeline.h"
#include "tianshu/dsl/dsl_runtime.h"
#include "tianshu/dsl/flow.h"

#include "traceable_flow_decls.h"

REGISTER_TRACEABLE_FLOW("demo_traceable", declare_demo_flow)
REGISTER_TRACEABLE_FLOW("demo_traceable_lite", declare_demo_flow_lite)

int main() try {
  static_cast<void>(std::printf("== registered flows ==\n"));
  for (const auto& name : tianshu::dsl::registered_flow_names()) {
    static_cast<void>(std::printf("  %s\n", name.c_str()));
  }

  // Dry-run by name: build = trace (SLA verdict + graph).
  const auto flow = tianshu::dsl::build_registered_flow("demo_traceable");
  static_cast<void>(std::printf("\n== dry-run trace ==\n%s\n", flow.describe().c_str()));
  static_cast<void>(std::printf("%s", flow.sla_report().format().c_str()));

  auto graph = tianshu::compiler::IrGraph::from_flow(flow);
  graph.normalize();
  static_cast<void>(std::printf("artifact hash: %s\n", graph.stable_hash().c_str()));
  static_cast<void>(std::printf("fallback ladder: %s\n", flow.fallback_flow().empty()
                                                             ? "(none)"
                                                             : flow.fallback_flow().c_str()));

  // Compile and run the artifact.
  auto compiled = tianshu::compiler::Pipeline::compile(flow);
  static_cast<void>(std::printf("\n== compiled run (%s, %s) ==\n",
                                compiled.valid() ? "artifact" : "degraded",
                                compiled.from_cache() ? "cached" : "fresh"));
  tianshu::dsl::FlowRuntime runtime;
  compiled.run(runtime, flow, std::chrono::milliseconds(60));
  const auto fallback = runtime.fallback_state();
  static_cast<void>(
      std::printf("degradation events: %llu\n", static_cast<unsigned long long>(fallback.events)));
  return 0;
} catch (const std::exception& e) {
  static_cast<void>(std::fprintf(stderr, "error: %s\n", e.what()));
  return 1;
}
