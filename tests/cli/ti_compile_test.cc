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

// End-to-end tests for the ti-compile binary (ADR-0030 M-B) via the
// TI_BIN_DIR fork+exec pattern (mirroring ti_info_calibrate_test.cc).
// The provider .so (tests/cli/ti_compile_test_flows.cc) registers
// `single_chain` and `echo_pair` at static init; ti-compile only sees
// them after dlopening it through --flows.

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>
#include <sys/wait.h>

namespace {

struct ExecResult {
  int exit_code = -1;
  std::string out;
  std::string err;
};

ExecResult run_ti_compile(const std::vector<std::string>& args) {
  ExecResult result;
  const char* ti_dir = std::getenv("TI_BIN_DIR");
  int out_pipe[2];
  int err_pipe[2];
  if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
    ADD_FAILURE() << "pipe() failed";
    return result;
  }
  std::vector<std::string> argv_strings;
  argv_strings.emplace_back(std::string(ti_dir) + "/ti-compile");
  for (const auto& arg : args) {
    argv_strings.push_back(arg);
  }
  std::vector<char*> argv;  // NOLINT(modernize-avoid-c-arrays)
  argv.reserve(argv_strings.size() + 1);
  for (auto& arg : argv_strings) {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);
  const pid_t pid = fork();  // NOLINT(misc-include-cleaner)  // glibc: unistd
  if (pid == 0) {
    static_cast<void>(dup2(out_pipe[1], STDOUT_FILENO));
    static_cast<void>(dup2(err_pipe[1], STDERR_FILENO));
    static_cast<void>(close(out_pipe[0]));
    static_cast<void>(close(out_pipe[1]));
    static_cast<void>(close(err_pipe[0]));
    static_cast<void>(close(err_pipe[1]));
    execv(argv[0], argv.data());  // NOLINT(misc-include-cleaner)  // POSIX: unistd
    _exit(127);                   // exec failed
  }
  static_cast<void>(close(out_pipe[1]));
  static_cast<void>(close(err_pipe[1]));
  char buf[4096];  // NOLINT(modernize-avoid-c-arrays)
  ssize_t n = 0;
  while ((n = read(out_pipe[0], buf, sizeof(buf))) > 0) {
    result.out.append(buf, static_cast<std::size_t>(n));
  }
  while ((n = read(err_pipe[0], buf, sizeof(buf))) > 0) {
    result.err.append(buf, static_cast<std::size_t>(n));
  }
  static_cast<void>(close(out_pipe[0]));
  static_cast<void>(close(err_pipe[0]));
  int status = 0;
  if (waitpid(pid, &status, 0) != pid) {
    ADD_FAILURE() << "waitpid() failed";
    return result;
  }
  result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;  // NOLINT
  return result;
}

// Finds <flow-prefix>.*.so in the cache dir (the artifact name carries
// the IR hash, which the test does not predict).
bool artifact_exists(const std::string& cache_dir, const std::string& flow_prefix) {
  try {
    return std::ranges::any_of(
        std::filesystem::directory_iterator(cache_dir), [&](const auto& entry) {
          const auto name = entry.path().filename().string();
          return name.starts_with(flow_prefix + ".") && name.ends_with(".so");
        });
  } catch (const std::filesystem::filesystem_error&) {
    return false;
  }
}

class TiCompileTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (std::getenv("TI_BIN_DIR") == nullptr) {
      GTEST_SKIP() << "TI_BIN_DIR not set (run via ctest)";
    }
    provider_ = std::string(std::getenv("TI_BIN_DIR")) + "/libti_compile_test_flows.so";
    ASSERT_TRUE(std::filesystem::exists(provider_)) << provider_;
    cache_ = unique_cache_dir();
  }

  void TearDown() override {
    if (!cache_.empty()) {
      std::error_code ec;
      static_cast<void>(std::filesystem::remove_all(cache_, ec));
    }
  }

  // mkdtemp-based unique dir so parallel ctest runs never collide.
  static std::string unique_cache_dir() {
    std::string tmpl = "/tmp/tianshu_ti_compile_XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    const char* dir = mkdtemp(buf.data());  // NOLINT(misc-include-cleaner)  // glibc: stdlib
    return dir != nullptr ? std::string(dir) : std::string();
  }

  std::string provider_;
  std::string cache_;
};

// rc 0, SLA verdict line, artifact path echoed, .so on disk.
TEST_F(TiCompileTest, CompileSucceedsAndProducesArtifact) {
  const auto result = run_ti_compile({"single_chain", "--flows", provider_, "--cache-dir", cache_});
  EXPECT_EQ(result.exit_code, 0) << "stdout: " << result.out << "stderr: " << result.err;
  EXPECT_NE(result.out.find("sla report: OK"), std::string::npos) << result.out;
  EXPECT_NE(result.out.find(cache_), std::string::npos) << result.out;
  EXPECT_TRUE(artifact_exists(cache_, "single_chain")) << "no single_chain.*.so in " << cache_;
}

// Second run over the same cache dir reports a cache hit.
TEST_F(TiCompileTest, SecondRunUsesCache) {
  const std::vector<std::string> args{"single_chain", "--flows", provider_, "--cache-dir", cache_};
  const auto first = run_ti_compile(args);
  ASSERT_EQ(first.exit_code, 0) << "stderr: " << first.err;
  const auto second = run_ti_compile(args);
  ASSERT_EQ(second.exit_code, 0) << "stderr: " << second.err;
  EXPECT_NE(second.out.find("cache: hit"), std::string::npos) << second.out;
}

// --emit-source prints the generated .gen.cc content.
TEST_F(TiCompileTest, EmitSourcePrintsGeneratedCc) {
  const auto result = run_ti_compile(
      {"single_chain", "--flows", provider_, "--cache-dir", cache_, "--emit-source"});
  EXPECT_EQ(result.exit_code, 0) << "stderr: " << result.err;
  EXPECT_NE(result.out.find("tianshu_flow_install"), std::string::npos) << result.out;
}

// Unknown flow name: rc 1, stderr lists the registered names.
TEST_F(TiCompileTest, UnknownFlowListsRegistered) {
  const auto result = run_ti_compile({"no_such_flow", "--flows", provider_});
  EXPECT_EQ(result.exit_code, 1) << "stdout: " << result.out;
  EXPECT_NE(result.err.find("single_chain"), std::string::npos) << result.err;
}

// --no-fallback turns a compile failure into rc 1; the default
// degrades to rc 0 with a greppable degraded line.
TEST_F(TiCompileTest, NoFallbackFailsCleanly) {
  const auto strict = run_ti_compile({"single_chain", "--flows", provider_, "--cache-dir", cache_,
                                      "--compiler", "/nonexistent/c++", "--no-fallback"});
  EXPECT_EQ(strict.exit_code, 1) << "stdout: " << strict.out;
  const std::string fallback_dir = unique_cache_dir();
  const auto degraded = run_ti_compile({"single_chain", "--flows", provider_, "--cache-dir",
                                        fallback_dir, "--compiler", "/nonexistent/c++"});
  std::error_code ec;
  static_cast<void>(std::filesystem::remove_all(fallback_dir, ec));
  EXPECT_EQ(degraded.exit_code, 0) << "stderr: " << degraded.err;
  EXPECT_NE(degraded.out.find("degraded: "), std::string::npos) << degraded.out;
}

// --list prints the names registered by the loaded providers.
TEST_F(TiCompileTest, ListNamesAfterLoadingProviders) {
  const auto result = run_ti_compile({"--list", "--flows", provider_});
  EXPECT_EQ(result.exit_code, 0) << "stderr: " << result.err;
  EXPECT_NE(result.out.find("single_chain"), std::string::npos) << result.out;
  EXPECT_NE(result.out.find("echo_pair"), std::string::npos) << result.out;
}

// No arguments: usage error rc 2.
TEST_F(TiCompileTest, UsageErrorRc2) {
  const auto result = run_ti_compile({});
  EXPECT_EQ(result.exit_code, 2) << "stdout: " << result.out;
  EXPECT_FALSE(result.err.empty());
}

}  // namespace
