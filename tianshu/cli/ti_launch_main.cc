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

// ti-launch: run a component DAG from a config file, or a registered
// traceable flow by name (ADR-0030 M-D).
//
//   ti-launch <flow.dag> [--mode intra|shm]
//   ti-launch <flow-name> [--flows PATH]...
//
// Argument resolution: a positional that names an existing readable
// regular file is a DagConfig path (the original mode, unchanged);
// anything else is a flow name looked up in the process-local registry
// (populated by --flows provider .so dlopens). Exit codes: 0 normal
// shutdown, 1 not-a-file-and-not-a-flow / parse or assembly failure,
// 2 usage.

#include <dlfcn.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <csignal>
// NOLINTNEXTLINE(modernize-deprecated-headers)  // POSIX sigset_t/timespec
#include <signal.h>

#include <sys/stat.h>
// NOLINTNEXTLINE(modernize-deprecated-headers)  // POSIX timespec
#include <time.h>

#include <cstdio>
#include <exception>
#include <string>
#include <vector>

#include "tianshu/compiler/pipeline.h"
#include "tianshu/core/launcher.h"
#include "tianshu/dsl/dsl_runtime.h"
#include "tianshu/dsl/flow.h"
#include "tianshu/transport/transport_backend.h"

namespace {

void print_usage() {
  static_cast<void>(std::fprintf(stderr,
                                 "usage: ti-launch <flow.dag> [--mode intra|shm]\n"
                                 "       ti-launch <flow-name> [--flows PATH]...\n"));
}

bool is_readable_file(const std::string& path) {
  struct stat st{};  // NOLINT(modernize-type-traits)
  return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && access(path.c_str(), R_OK) == 0;
}

// Same registry-population contract as ti-compile: the registry is
// process-local static init, a provider .so's nodes only run here.
bool load_provider(const std::string& path) {
  if (dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL) == nullptr) {
    static_cast<void>(std::fprintf(stderr, "ti-launch: --flows %s: %s\n", path.c_str(), dlerror()));
    return false;
  }
  return true;
}

struct Args {
  std::string positional;
  std::vector<std::string> flows;
  bool mode_given = false;
  tianshu::transport::TransportMode mode = tianshu::transport::TransportMode::kIntra;
};

// Returns false (usage error) on an unknown option, a flag missing its
// value, an unknown mode value, or a second positional argument.
bool parse_args(int argc, char** argv, Args* args) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    if (arg == "--mode" && i + 1 < argc) {
      const std::string value(
          argv[++i]);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      if (value == "shm") {
        args->mode = tianshu::transport::TransportMode::kShm;
      } else if (value != "intra") {
        static_cast<void>(
            std::fprintf(stderr, "ti-launch: unknown mode '%s' (intra|shm)\n", value.c_str()));
        return false;
      }
      args->mode_given = true;
    } else if (arg == "--flows" && i + 1 < argc) {
      args->flows.emplace_back(
          argv[++i]);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    } else if (arg == "--mode" || arg == "--flows") {
      static_cast<void>(std::fprintf(stderr, "ti-launch: %s requires an argument\n", arg.c_str()));
      return false;
    } else if (arg.starts_with("--")) {
      return false;
    } else if (!args->positional.empty()) {
      static_cast<void>(std::fprintf(stderr, "ti-launch: unexpected argument '%s'\n", arg.c_str()));
      return false;
    } else {
      args->positional = arg;
    }
  }
  return !args->positional.empty();
}

volatile std::sig_atomic_t g_flow_stop = 0;

void flow_signal_handler(int /*sig*/) { g_flow_stop = 1; }

int run_file_mode(const std::string& dag_path, tianshu::transport::TransportMode mode) {
  const auto dag = tianshu::core::DagConfig::parse_file(dag_path);
  if (!dag.ok()) {
    static_cast<void>(
        std::fprintf(stderr, "ti-launch: %s: %s\n", dag_path.c_str(), dag.error.c_str()));
    return 1;
  }

  tianshu::core::Launcher launcher(mode);
  std::string error;
  if (!launcher.start(dag, &error)) {
    static_cast<void>(std::fprintf(stderr, "ti-launch: %s\n", error.c_str()));
    return 1;
  }

  static_cast<void>(std::printf("ti-launch: %zu components running (Ctrl-C to stop)\n",
                                launcher.components().size()));
  launcher.run_until_signal();
  static_cast<void>(std::printf("ti-launch: stopped\n"));
  return 0;
}

int run_flow_mode(const std::string& name, const std::vector<std::string>& providers) {
  for (const auto& provider : providers) {
    if (!load_provider(provider)) {
      return 1;
    }
  }

  const auto names = tianshu::dsl::registered_flow_names();
  if (std::ranges::find(names, name) == names.end()) {
    static_cast<void>(std::fprintf(stderr,
                                   "ti-launch: '%s' is neither a readable DAG file nor a"
                                   " registered flow name\n",
                                   name.c_str()));
    if (names.empty()) {
      static_cast<void>(
          std::fprintf(stderr, "ti-launch: no flows registered (--flows loads a provider .so)\n"));
    } else {
      std::string listed;
      for (const auto& registered : names) {
        listed += listed.empty() ? registered : ", " + registered;
      }
      static_cast<void>(std::fprintf(stderr, "ti-launch: registered flows: %s\n", listed.c_str()));
    }
    return 1;
  }

  const auto flow = tianshu::dsl::build_registered_flow(name);
  static_cast<void>(std::printf("sla report: %s\n", flow.sla_report().ok ? "OK" : "REJECTED"));
  const auto compiled = tianshu::compiler::Pipeline::compile(flow);
  static_cast<void>(std::printf("compiled: %s\n", compiled.valid() ? "valid" : "degraded"));

  // Declaration order follows traceable_flow_demo.cc: the runtime must
  // be destroyed before the CompiledFlow releases the artifact.
  tianshu::dsl::FlowRuntime runtime;

  // Directed wait, not bare pause()/sigwait(): the asynchronous handler
  // may run on any thread, so this thread observes the flag from a
  // timed wait (same rationale as launcher.cc's run_until_signal).
  static_cast<void>(std::signal(SIGINT, flow_signal_handler));   // NOLINT(misc-include-cleaner)
  static_cast<void>(std::signal(SIGTERM, flow_signal_handler));  // NOLINT(misc-include-cleaner)
  // POSIX sigset_t/timespec live in <signal.h>/<time.h> which the C++
  // deprecation check rejects; the C++ headers do not guarantee them.
  // NOLINTBEGIN(modernize-deprecated-headers)
  // NOLINTNEXTLINE(modernize-deprecated-headers,misc-include-cleaner)  // POSIX sigset_t
  sigset_t empty;
  static_cast<void>(sigemptyset(&empty));
  // NOLINTNEXTLINE(modernize-deprecated-headers,misc-const-correctness,readability-magic-numbers)
  timespec nap{.tv_sec = 0, .tv_nsec = 50'000'000};
  // NOLINTEND(modernize-deprecated-headers)

  // duration <= 0 is install-only (thread creation skipped); the
  // driving loop below then feeds the same run_sources in 50ms
  // windows. Bootstrap hooks (op/stateful on_init) re-fire once per
  // window — a known v0 limitation for bootstrap-publishing flows.
  compiled.run(runtime, flow, std::chrono::milliseconds(0));
  static_cast<void>(std::printf("ti-launch: flow '%s' running (Ctrl-C to stop)\n", name.c_str()));
  while (g_flow_stop == 0) {
    static_cast<void>(sigtimedwait(&empty, nullptr, &nap));
    if (g_flow_stop != 0) {
      break;
    }
    runtime.run_sources(flow, std::chrono::milliseconds(50));
  }
  static_cast<void>(std::printf("ti-launch: stopped\n"));
  return 0;
}

}  // namespace

int main(int argc, char** argv) try {
  Args args;
  if (!parse_args(argc, argv, &args)) {
    print_usage();
    return 2;
  }

  if (is_readable_file(args.positional)) {
    if (!args.flows.empty()) {
      static_cast<void>(std::fprintf(stderr,
                                     "ti-launch: --flows is only valid for a flow name, not %s\n",
                                     args.positional.c_str()));
      return 2;
    }
    return run_file_mode(args.positional, args.mode);
  }
  if (args.mode_given) {
    static_cast<void>(std::fprintf(stderr,
                                   "ti-launch: --mode applies to DAG file paths only; flow mode"
                                   " selects no transport mode\n"));
    return 2;
  }
  return run_flow_mode(args.positional, args.flows);
} catch (const std::exception& e) {
  static_cast<void>(std::fprintf(stderr, "ti-launch: %s\n", e.what()));
  return 1;
}
