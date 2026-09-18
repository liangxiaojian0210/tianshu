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

// End-to-end tests for ti-launch flow-name mode (ADR-0030 M-D) via the
// TI_BIN_DIR fork+exec pattern. The provider .so
// (tests/cli/ti_launch_flow_test_flows.cc) registers `tick_writer`,
// whose sink appends one line per message to $TI_LAUNCH_TEST_OUT.

#include <unistd.h>

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <sys/wait.h>

namespace {

struct ExecResult {
  int exit_code = -1;
  std::string out;
  std::string err;
};

// Runs $TI_BIN_DIR/ti-launch with TI_LAUNCH_TEST_OUT exported as `env`.
ExecResult run_ti_launch(const std::vector<std::string>& args, const std::string& env) {
  ExecResult result;
  const char* ti_dir = std::getenv("TI_BIN_DIR");
  int out_pipe[2];
  int err_pipe[2];
  if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
    ADD_FAILURE() << "pipe() failed";
    return result;
  }
  std::vector<std::string> argv_strings;
  argv_strings.emplace_back(std::string(ti_dir) + "/ti-launch");
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
    static_cast<void>(
        setenv("TI_LAUNCH_TEST_OUT", env.c_str(), 1));  // NOLINT(misc-include-cleaner)
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

std::size_t count_lines(const std::string& text) {
  return static_cast<std::size_t>(std::ranges::count(text, '\n'));
}

std::string read_file(const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "r");
  if (f == nullptr) {
    return {};
  }
  std::string text;
  char buf[4096];  // NOLINT(modernize-avoid-c-arrays)
  std::size_t n = 0;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
    text.append(buf, n);
  }
  static_cast<void>(std::fclose(f));
  return text;
}

// Spawns ti-launch in flow mode; polls the out-file up to 5s for
// `min_lines`, then SIGTERMs and waits up to 5s for exit. Pipes are
// drained only after exit (the process blocks in its run loop, so an
// earlier read-to-EOF would stall).
struct FlowRun {
  pid_t pid = -1;
  int exit_code = -1;
  std::string out;
  std::string err;
};

// Forks ti-launch with the pipes wired as stdout/stderr; the child
// exports TI_LAUNCH_TEST_OUT before exec. Returns the child pid.
pid_t spawn_ti_launch(const std::vector<std::string>& args, const std::string& env,
                      int (&out_pipe)[2], int (&err_pipe)[2]) {
  std::vector<std::string> argv_strings;
  argv_strings.emplace_back(std::string(std::getenv("TI_BIN_DIR")) + "/ti-launch");
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
    static_cast<void>(
        setenv("TI_LAUNCH_TEST_OUT", env.c_str(), 1));  // NOLINT(misc-include-cleaner)
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
  return pid;
}

// Reads fd to EOF; the child has exited by call time, so this cannot stall.
std::string drain_fd(int fd) {
  std::string out;
  char buf[4096];  // NOLINT(modernize-avoid-c-arrays)
  ssize_t n = 0;
  while ((n = read(fd, buf, sizeof(buf))) > 0) {
    out.append(buf, static_cast<std::size_t>(n));
  }
  return out;
}

FlowRun run_flow_until_lines(const std::vector<std::string>& args, const std::string& env,
                             std::size_t min_lines) {
  FlowRun run;
  int out_pipe[2];
  int err_pipe[2];
  if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
    ADD_FAILURE() << "pipe() failed";
    return run;
  }
  run.pid = spawn_ti_launch(args, env, out_pipe, err_pipe);

  for (int waited = 0; waited < 5000 && count_lines(read_file(env)) < min_lines; waited += 50) {
    usleep(50'000);  // NOLINT(misc-include-cleaner)  // POSIX: unistd
  }
  static_cast<void>(kill(run.pid, SIGTERM));  // NOLINT(misc-include-cleaner)  // POSIX: signal
  int status = 0;
  for (int waited = 0; waited < 5000; waited += 50) {
    const pid_t done = waitpid(run.pid, &status, WNOHANG);  // NOLINT(misc-include-cleaner)
    if (done == run.pid) {
      run.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;  // NOLINT
      break;
    }
    usleep(50'000);  // NOLINT(misc-include-cleaner)  // POSIX: unistd
  }

  run.out = drain_fd(out_pipe[0]);
  run.err = drain_fd(err_pipe[0]);
  static_cast<void>(close(out_pipe[0]));
  static_cast<void>(close(err_pipe[0]));
  return run;
}

// mkstemp-based unique path so parallel ctest runs never collide.
std::string unique_out_path() {
  std::string tmpl = "/tmp/tianshu_ti_launch_flow_XXXXXX.out";
  std::vector<char> buf(tmpl.begin(), tmpl.end());
  buf.push_back('\0');
  const int fd = mkstemp(buf.data());  // NOLINT(misc-include-cleaner)  // glibc: stdlib
  if (fd >= 0) {
    static_cast<void>(close(fd));
  }
  return {buf.data()};
}

class TiLaunchFlowTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (std::getenv("TI_BIN_DIR") == nullptr) {
      GTEST_SKIP() << "TI_BIN_DIR not set (run via ctest)";
    }
    provider_ = std::string(std::getenv("TI_BIN_DIR")) + "/libti_launch_flow_test_flows.so";
    ASSERT_TRUE(std::filesystem::exists(provider_)) << provider_;
  }

  std::string provider_;
};

// Flow mode runs the compiled product until SIGTERM, then exits 0 and
// stops producing (line count frozen after termination).
TEST_F(TiLaunchFlowTest, FlowModeRunsUntilSignal) {
  const std::string out_file = unique_out_path();
  const auto run = run_flow_until_lines({"tick_writer", "--flows", provider_}, out_file, 3);
  EXPECT_NE(run.pid, -1);
  ASSERT_GT(run.exit_code, -1) << "ti-launch did not exit within 5s of SIGTERM";
  EXPECT_EQ(run.exit_code, 0) << "stdout: " << run.out << "stderr: " << run.err;
  const std::size_t lines_at_stop = count_lines(read_file(out_file));
  EXPECT_GE(lines_at_stop, 3);
  usleep(200'000);  // NOLINT(misc-include-cleaner)  // POSIX: unistd
  EXPECT_EQ(count_lines(read_file(out_file)), lines_at_stop);
  static_cast<void>(std::remove(out_file.c_str()));
}

// File mode contract unchanged: a nonexistent path is rc 1.
TEST_F(TiLaunchFlowTest, FileModeStillWorks) {
  const auto result = run_ti_launch({"/nonexistent/flow.dag"}, unique_out_path());
  EXPECT_EQ(result.exit_code, 1) << "stdout: " << result.out;
  EXPECT_NE(result.err.find("/nonexistent/flow.dag"), std::string::npos) << result.err;
}

// Neither a readable file nor a registered flow: rc 1, stderr names
// both interpretations.
TEST_F(TiLaunchFlowTest, NeitherFileNorFlowIsRc1) {
  const auto result = run_ti_launch({"no_such_anything"}, unique_out_path());
  EXPECT_EQ(result.exit_code, 1) << "stdout: " << result.out;
  EXPECT_NE(result.err.find("file"), std::string::npos) << result.err;
  EXPECT_NE(result.err.find("flow"), std::string::npos) << result.err;
}

// --mode is a DAG-file-mode flag; flow mode rejects it with rc 2.
TEST_F(TiLaunchFlowTest, FlowModeRejectsModeFlag) {
  const auto result =
      run_ti_launch({"tick_writer", "--flows", provider_, "--mode", "intra"}, unique_out_path());
  EXPECT_EQ(result.exit_code, 2) << "stdout: " << result.out;
  EXPECT_NE(result.err.find("--mode"), std::string::npos) << result.err;
}

}  // namespace
