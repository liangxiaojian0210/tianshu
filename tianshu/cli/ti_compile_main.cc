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

// ti-compile: offline compilation of a registered traceable flow
// (ADR-0030 M-B): dry-run trace by name, SLA verdict, Pipeline::compile,
// .dag/.conf export next to the artifact.
//
//   ti-compile <flow-name> [--flows PATH]... [--cache-dir DIR]
//              [--compiler CXX] [--emit-source] [--no-fallback]
//   ti-compile --list [--flows PATH]...
//
// Exit codes: 0 success (valid or degraded fallback), 1 unknown flow /
// provider load failure / compile failure under --no-fallback, 2 usage.

#include <dlfcn.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <vector>

#include "tianshu/compiler/ir.h"
#include "tianshu/compiler/pipeline.h"
#include "tianshu/dsl/flow.h"

namespace {

struct CliOptions {
  std::vector<std::string> flows;
  std::string cache_dir;
  std::string compiler;
  std::string flow_name;
  bool emit_source = false;
  bool no_fallback = false;
  bool list = false;
};

void print_usage() {
  static_cast<void>(
      std::fprintf(stderr,
                   "usage: ti-compile <flow-name> [options]\n"
                   "  --flows PATH     provider .so exporting REGISTER_TRACEABLE_FLOW nodes\n"
                   "                   (repeatable; loaded RTLD_NOW|RTLD_LOCAL before lookup)\n"
                   "  --cache-dir DIR  artifact cache (default: build/tianshu-gen)\n"
                   "  --compiler CXX   system compiler (default: $CXX or c++)\n"
                   "  --emit-source    print the generated .gen.cc to stdout after compiling\n"
                   "  --no-fallback    compile failure is an error (rc 1), not a degraded rc 0\n"
                   "  --list           print registered flow names and exit 0\n"));
}

// The flow registry is process-local and populated by static init, so a
// CLI binary only sees flows from translation units linked in — or from
// a provider .so dlopened here, whose registration nodes then run.
bool load_provider(const std::string& path) {
  if (dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL) == nullptr) {
    static_cast<void>(
        std::fprintf(stderr, "ti-compile: --flows %s: %s\n", path.c_str(), dlerror()));
    return false;
  }
  return true;
}

// Returns false (usage error) on an unknown option, a flag missing its
// value, or a second positional argument.
bool parse_options(int argc, char** argv, CliOptions* opts) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    if (arg == "--flows" || arg == "--cache-dir" || arg == "--compiler") {
      if (i + 1 >= argc) {
        static_cast<void>(
            std::fprintf(stderr, "ti-compile: %s requires an argument\n", arg.c_str()));
        return false;
      }
      const std::string value(
          argv[++i]);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      if (arg == "--flows") {
        opts->flows.push_back(value);
      } else if (arg == "--cache-dir") {
        opts->cache_dir = value;
      } else {
        opts->compiler = value;
      }
    } else if (arg == "--emit-source") {
      opts->emit_source = true;
    } else if (arg == "--no-fallback") {
      opts->no_fallback = true;
    } else if (arg == "--list") {
      opts->list = true;
    } else if (arg.starts_with("--")) {
      static_cast<void>(std::fprintf(stderr, "ti-compile: unknown option '%s'\n", arg.c_str()));
      return false;
    } else if (!opts->flow_name.empty()) {
      static_cast<void>(
          std::fprintf(stderr, "ti-compile: unexpected argument '%s'\n", arg.c_str()));
      return false;
    } else {
      opts->flow_name = arg;
    }
  }
  return opts->list || !opts->flow_name.empty();
}

void print_registered_names() {
  const auto names = tianshu::dsl::registered_flow_names();
  if (names.empty()) {
    static_cast<void>(
        std::fprintf(stderr, "ti-compile: no flows registered (load a provider with --flows)\n"));
    return;
  }
  std::string listed;
  for (const auto& name : names) {
    listed += listed.empty() ? name : ", " + name;
  }
  static_cast<void>(std::fprintf(stderr, "ti-compile: registered flows: %s\n", listed.c_str()));
}

// Writes one exported artifact sidecar; reports what went where.
bool write_export(const std::string& path, const std::string& content) {
  std::ofstream out(path);
  if (!out) {
    static_cast<void>(std::fprintf(stderr, "ti-compile: cannot write %s\n", path.c_str()));
    return false;
  }
  out << content;
  static_cast<void>(std::printf("exported: %s\n", path.c_str()));
  return true;
}

void print_source(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    static_cast<void>(std::fprintf(stderr, "ti-compile: cannot read %s\n", path.c_str()));
    return;
  }
  static_cast<void>(std::printf(
      "%s",
      std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()).c_str()));
}

}  // namespace

int main(int argc, char** argv) try {
  CliOptions opts;
  if (!parse_options(argc, argv, &opts)) {
    print_usage();
    return 2;
  }

  for (const auto& provider : opts.flows) {
    if (!load_provider(provider)) {
      return 1;
    }
  }
  if (opts.list) {
    for (const auto& name : tianshu::dsl::registered_flow_names()) {
      static_cast<void>(std::printf("%s\n", name.c_str()));
    }
    return 0;
  }

  const auto names = tianshu::dsl::registered_flow_names();
  if (std::ranges::find(names, opts.flow_name) == names.end()) {
    static_cast<void>(std::fprintf(stderr, "ti-compile: no flow registered under '%s'\n",
                                   opts.flow_name.c_str()));
    print_registered_names();
    return 1;
  }

  const auto flow = tianshu::dsl::build_registered_flow(opts.flow_name);
  static_cast<void>(std::printf("sla report: %s\n", flow.sla_report().ok ? "OK" : "REJECTED"));

  tianshu::compiler::CompileOptions compile_opts;
  if (!opts.cache_dir.empty()) {
    compile_opts.cache_dir = opts.cache_dir;
  }
  if (!opts.compiler.empty()) {
    compile_opts.compiler = opts.compiler;
  }
  compile_opts.allow_fallback = !opts.no_fallback;

  auto compiled = tianshu::compiler::Pipeline::compile(flow, compile_opts);
  if (compiled.valid()) {
    static_cast<void>(std::printf("compiled: valid\n"));
    static_cast<void>(std::printf("artifact: %s\n", compiled.artifact_path().c_str()));
    static_cast<void>(std::printf("cache: %s\n", compiled.from_cache() ? "hit" : "miss"));
  } else {
    static_cast<void>(std::printf("degraded: %s\n", compiled.degraded_reason().c_str()));
  }

  // Audit pair next to the artifact: the pipeline names sidecars
  // <artifact-basename>.{gen.cc,.dag,.conf}, derived here from the
  // artifact path (".so" suffix stripped).
  auto graph = tianshu::compiler::IrGraph::from_flow(flow);
  graph.normalize();
  const std::string base =
      compiled.artifact_path().substr(0, compiled.artifact_path().size() - strlen(".so"));
  if (!write_export(base + ".dag", graph.export_dag()) ||
      !write_export(base + ".conf", graph.export_conf())) {
    return 1;
  }
  if (opts.emit_source) {
    print_source(base + ".gen.cc");
  }
  return 0;
} catch (const std::exception& e) {
  static_cast<void>(std::fprintf(stderr, "ti-compile: %s\n", e.what()));
  return 1;
}
