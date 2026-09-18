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

#include "tianshu/compiler/pipeline.h"

#include <dlfcn.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

#include "tianshu/compiler/codegen.h"
#include "tianshu/compiler/ir.h"
#include "tianshu/dsl/dsl_runtime.h"

namespace tianshu::compiler {
namespace {

using InstallFn = void (*)(dsl::FlowRuntime*, const dsl::Flow*);

[[nodiscard]] std::string sanitize_name(const std::string& name) {
  std::string out = name;
  std::ranges::transform(out, out.begin(), [](unsigned char c) {
    return (std::isalnum(c) != 0) || c == '_' || c == '-' || c == '.' ? c : '_';
  });
  return out;
}

[[nodiscard]] std::string resolve_compiler(const CompileOptions& options) {
  if (!options.compiler.empty()) {
    return options.compiler;
  }
  const char* env = std::getenv("CXX");
  return env != nullptr ? env : "c++";
}

[[nodiscard]] std::string resolve_include_dir() {
#ifdef TIANSHU_GEN_INCLUDE_DIR
  return TIANSHU_GEN_INCLUDE_DIR;
#else
  // Bazel runfiles cwd contains the workspace layout; repo-root
  // invocations resolve the same relative path.
  return "tianshu/include";
#endif
}

void degrade_or_throw(const CompileOptions& options, const std::string& what) {
  if (options.allow_fallback) {
    return;
  }
  throw std::runtime_error("tianshu pipeline: " + what);
}

}  // namespace

CompiledFlow::~CompiledFlow() {
  if (handle_ != nullptr) {
    dlclose(handle_);
  }
}

CompiledFlow::CompiledFlow(CompiledFlow&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)),
      install_(std::exchange(other.install_, nullptr)),
      degraded_(other.degraded_),
      degraded_reason_(std::move(other.degraded_reason_)),
      from_cache_(other.from_cache_),
      hash_(std::move(other.hash_)),
      artifact_(std::move(other.artifact_)) {}

CompiledFlow& CompiledFlow::operator=(CompiledFlow&& other) noexcept {
  if (this != &other) {
    if (handle_ != nullptr) {
      dlclose(handle_);
    }
    handle_ = std::exchange(other.handle_, nullptr);
    install_ = std::exchange(other.install_, nullptr);
    degraded_ = other.degraded_;
    degraded_reason_ = std::move(other.degraded_reason_);
    from_cache_ = other.from_cache_;
    hash_ = std::move(other.hash_);
    artifact_ = std::move(other.artifact_);
  }
  return *this;
}

void Pipeline::validate(const IrGraph& graph) {
  std::unordered_set<std::string> outputs;
  for (const auto& node : graph.nodes()) {
    if (node.output.empty()) {
      continue;  // sinks produce nothing
    }
    if (!outputs.insert(node.output).second) {
      throw std::invalid_argument("duplicate channel producer: " + node.output);
    }
  }
  for (const auto& node : graph.nodes()) {
    for (const auto& in : node.inputs) {
      if (!outputs.contains(in)) {
        throw std::invalid_argument("dangling input channel: " + in + " (consumed by " + node.kind +
                                    ", produced by nothing)");
      }
    }
  }
}

CompiledFlow Pipeline::compile(const dsl::Flow& flow, const CompileOptions& options) {
  IrGraph graph = IrGraph::from_flow(flow);
  graph.normalize();
  validate(graph);

  CompiledFlow compiled;
  compiled.hash_ = graph.stable_hash();
  compiled.artifact_ =
      options.cache_dir + "/" + sanitize_name(flow.name()) + "." + compiled.hash_ + ".so";
  const std::string source_path =
      options.cache_dir + "/" + sanitize_name(flow.name()) + "." + compiled.hash_ + ".gen.cc";
  std::filesystem::create_directories(options.cache_dir);

  if (std::filesystem::exists(compiled.artifact_)) {
    compiled.from_cache_ = true;
  } else {
    const std::string source = generate_source(graph);
    {
      std::ofstream out(source_path);
      out << source;
    }
    const std::string log_path = options.cache_dir + "/" + sanitize_name(flow.name()) + "." +
                                 compiled.hash_ + ".compile.log";
    const std::string cmd = resolve_compiler(options) + " -std=c++20 -O2 -fPIC -shared -I" +
                            resolve_include_dir() + " \"" + source_path + "\" -o \"" +
                            compiled.artifact_ + "\" 2> \"" + log_path + "\"";
    // ADR-0030 D6: the command is assembled from a fixed flag
    // whitelist and filesystem-sanitized names; the compiler binary
    // comes from trusted configuration (options/env), not flow input.
    // NOLINTNEXTLINE(cert-env33-c,bugprone-command-processor)
    if (std::system(cmd.c_str()) != 0) {
      compiled.degraded_reason_ =
          "system compiler failed for " + flow.name() + " (log: " + log_path + ")";
      degrade_or_throw(options, compiled.degraded_reason_);
      compiled.degraded_ = true;
      return compiled;
    }
  }

  compiled.handle_ = dlopen(compiled.artifact_.c_str(), RTLD_NOW);
  if (compiled.handle_ == nullptr) {
    compiled.degraded_reason_ = std::string("dlopen failed: ") + dlerror();
    degrade_or_throw(options, compiled.degraded_reason_);
    compiled.degraded_ = true;
    return compiled;
  }
  compiled.install_ = dlsym(compiled.handle_, "tianshu_flow_install");
  if (compiled.install_ == nullptr) {
    compiled.degraded_reason_ = "artifact exports no tianshu_flow_install";
    degrade_or_throw(options, compiled.degraded_reason_);
    dlclose(std::exchange(compiled.handle_, nullptr));
    compiled.degraded_ = true;
  }
  return compiled;
}

void CompiledFlow::run(dsl::FlowRuntime& rt, const dsl::Flow& flow,
                       std::chrono::milliseconds duration) const {
  if (degraded_ || install_ == nullptr) {
    rt.run_for(flow, duration);  // interpreter escape hatch (ADR-0030 D6)
    return;
  }
  IrGraph check = IrGraph::from_flow(flow);
  check.normalize();
  if (check.stable_hash() != hash_) {
    throw std::invalid_argument("flow does not match compiled artifact (expected hash " + hash_ +
                                ")");
  }
  reinterpret_cast<InstallFn>(install_)(&rt, &flow);
  rt.run_sources(flow, duration);
}

}  // namespace tianshu::compiler
