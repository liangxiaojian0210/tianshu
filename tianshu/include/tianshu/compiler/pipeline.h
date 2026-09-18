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

// Compile pipeline (ADR-0030 D3, M-B): P1 validation, P4 codegen, P5
// artifact emission + loading. The artifact is a .so built by the
// system compiler from generated source, cached by the normalized-IR
// hash (runtime ABI included — see IrGraph::stable_hash). When the
// compiler is unavailable or compilation fails, compile() returns a
// degraded CompiledFlow whose run() falls back to the interpreter —
// the escape hatch of ADR-0030 D6.

#pragma once

#include <chrono>
#include <string>

#include "tianshu/compiler/ir.h"

namespace tianshu::dsl {
class Flow;
class FlowRuntime;
}  // namespace tianshu::dsl

namespace tianshu::compiler {

struct CompileOptions {
  // Artifact cache; sources are kept next to the .so for audit.
  std::string cache_dir{"build/tianshu-gen"};
  // Empty: $CXX, else "c++". Invoked with a fixed whitelist of flags
  // (ADR-0030 D6): -std=c++20 -O2 -fPIC -shared.
  std::string compiler;
  // False: compile failures throw instead of degrading.
  bool allow_fallback{true};
};

class CompiledFlow {
 public:
  CompiledFlow() = default;
  ~CompiledFlow();

  CompiledFlow(CompiledFlow&& other) noexcept;
  CompiledFlow& operator=(CompiledFlow&& other) noexcept;
  CompiledFlow(const CompiledFlow&) = delete;
  CompiledFlow& operator=(const CompiledFlow&) = delete;

  // A compiled artifact is loaded and installable.
  [[nodiscard]] bool valid() const { return install_ != nullptr; }
  // No compiler / compilation failed: run() uses the interpreter.
  [[nodiscard]] bool degraded() const { return degraded_; }
  // Why the interpreter fallback was taken; empty unless degraded.
  [[nodiscard]] const std::string& degraded_reason() const { return degraded_reason_; }
  [[nodiscard]] bool from_cache() const { return from_cache_; }
  [[nodiscard]] const std::string& hash() const { return hash_; }
  [[nodiscard]] const std::string& artifact_path() const { return artifact_; }

  // Installs wiring (compiled straight-line replay, or the
  // interpreter's own wiring when degraded) and runs the flow's
  // sources for `duration`. Throws std::invalid_argument when `flow`
  // does not hash to the artifact's key.
  void run(dsl::FlowRuntime& rt, const dsl::Flow& flow, std::chrono::milliseconds duration) const;

 private:
  friend class Pipeline;
  void* handle_{nullptr};
  void* install_{nullptr};
  bool degraded_{false};
  std::string degraded_reason_;
  bool from_cache_{false};
  std::string hash_;
  std::string artifact_;
};

class Pipeline {
 public:
  // P1 structural validation: duplicate outputs and inputs produced by
  // no node are rejected (throws std::invalid_argument).
  static void validate(const IrGraph& graph);

  // P4+P5: normalize, hash, cache lookup, codegen, system compiler,
  // dlopen. Never returns an invalid-and-not-degraded flow: compile
  // failures either degrade (allow_fallback) or throw.
  [[nodiscard]] static CompiledFlow compile(const dsl::Flow& flow,
                                            const CompileOptions& options = {});
};

}  // namespace tianshu::compiler
