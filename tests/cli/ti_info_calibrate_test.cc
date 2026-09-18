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

// End-to-end tests for `ti-info --calibrate --wcet` (ADR-0029 D2 drift
// comparison). Each test writes a small record v2 fixture whose single
// stage `t/out` measures exactly 1.0us per message, runs the real
// ti-info binary via fork+exec (TI_BIN_DIR, mirroring launcher_test),
// and asserts on stdout/stderr content plus the exit code.

#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <sys/wait.h>

#include "tianshu/core/lineage.h"
#include "tianshu/dsl/record_v2.h"

namespace {

// One src message + one derived out message per step; ts(out k) =
// ts(src k) + 1000ns, so every stage e2e is exactly 1.0us and
// WCET_suggest = 1.0 * 1.3 = 1.3us.
constexpr int kMessages = 10;
constexpr std::uint64_t kStageLatencyNs = 1000;

// mkstemp-based unique path so parallel ctest runs never collide.
std::string unique_record_path() {
  std::string tmpl = "/tmp/tianshu_ti_info_calibrate_XXXXXX.trec";
  std::vector<char> buf(tmpl.begin(), tmpl.end());
  buf.push_back('\0');
  const int fd = mkstemp(buf.data());  // NOLINT(misc-include-cleaner)  // glibc: stdlib
  if (fd >= 0) {
    static_cast<void>(close(fd));
  }
  return {buf.data()};
}

struct ExecResult {
  int exit_code = -1;
  std::string out;
  std::string err;
};

// Runs $TI_BIN_DIR/ti-info <file> <flags...>, capturing stdout/stderr
// and the propagated exit status.
ExecResult run_ti_info(const std::string& file, const std::vector<std::string>& flags) {
  ExecResult result;
  const char* ti_dir = std::getenv("TI_BIN_DIR");
  int out_pipe[2];
  int err_pipe[2];
  if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
    ADD_FAILURE() << "pipe() failed";
    return result;
  }
  std::vector<std::string> argv_strings;
  argv_strings.emplace_back(std::string(ti_dir) + "/ti-info");
  argv_strings.push_back(file);
  for (const auto& flag : flags) {
    argv_strings.push_back(flag);
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

class TiInfoCalibrateTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (std::getenv("TI_BIN_DIR") == nullptr) {
      GTEST_SKIP() << "TI_BIN_DIR not set (run via ctest)";
    }
    path_ = unique_record_path();
    tianshu::dsl::record::RecordWriter writer(path_, tianshu::dsl::record::Compression::kNone);
    const auto src_id = writer.add_channel("t/src", 0, "SrcMsg");
    const auto out_id = writer.add_channel("t/out", 0, "OutMsg");
    for (std::uint64_t k = 0; k < kMessages; ++k) {
      const std::uint64_t src_ts = k * 1000000;  // 1ms cadence keeps ts order
      const std::uint64_t out_ts = src_ts + kStageLatencyNs;
      auto lin = tianshu::core::Lineage::rooted("t/src", k);
      lin.add_hop({.channel = "t/out", .seq = k});
      const std::uint64_t payload = k;
      writer.append(src_id, k, src_ts, &payload, sizeof(payload));
      writer.append(out_id, k, out_ts, &payload, sizeof(payload), &lin);
    }
    ASSERT_TRUE(writer.finish());
  }

  void TearDown() override { static_cast<void>(std::remove(path_.c_str())); }

  std::string path_;
};

// Baseline table: t/out row with N=10, all percentiles 1.0us (exact
// 1000ns latencies), WCET_suggest 1.3us, and rc 0.
TEST_F(TiInfoCalibrateTest, CalibratePrintsPercentilesAndSuggestion) {
  const auto result = run_ti_info(path_, {"--calibrate"});
  EXPECT_EQ(result.exit_code, 0) << "stderr: " << result.err;
  EXPECT_NE(result.out.find("t/out"), std::string::npos) << result.out;
  EXPECT_NE(result.out.find("10"), std::string::npos) << result.out;
  EXPECT_NE(result.out.find("1.0"), std::string::npos) << result.out;  // p99.9(us)
  EXPECT_NE(result.out.find("1.3"), std::string::npos) << result.out;  // WCET_suggest
}

// declared=1.0 vs suggest=1.3 -> ratio ~0.77, inside the 3x window:
// RATIO column present, no drift verdict anywhere, rc 0.
TEST_F(TiInfoCalibrateTest, DeclaredWithinWindowIsNotDrift) {
  const auto result = run_ti_info(path_, {"--calibrate", "--wcet", "t/out=1"});
  EXPECT_EQ(result.exit_code, 0) << "stderr: " << result.err;
  EXPECT_NE(result.out.find("RATIO"), std::string::npos) << result.out;
  EXPECT_NE(result.out.find("0.77"), std::string::npos) << result.out;
  EXPECT_EQ(result.out.find("DRIFT-"), std::string::npos) << result.out;
}

// declared=5.2 = 4x suggest (>3x): DRIFT-HIGH verdict, rc 3.
TEST_F(TiInfoCalibrateTest, DeclaredTooHighFlagsDriftHigh) {
  const auto result = run_ti_info(path_, {"--calibrate", "--wcet", "t/out=5.2"});
  EXPECT_EQ(result.exit_code, 3) << "stdout: " << result.out << "stderr: " << result.err;
  EXPECT_NE(result.out.find("DRIFT-HIGH"), std::string::npos) << result.out;
}

// declared=0.2 < suggest/3 (~0.43): DRIFT-LOW verdict, rc 3.
TEST_F(TiInfoCalibrateTest, DeclaredTooLowFlagsDriftLow) {
  const auto result = run_ti_info(path_, {"--calibrate", "--wcet", "t/out=0.2"});
  EXPECT_EQ(result.exit_code, 3) << "stdout: " << result.out << "stderr: " << result.err;
  EXPECT_NE(result.out.find("DRIFT-LOW"), std::string::npos) << result.out;
}

// Stage names are output channel names; t/nope is not one -> rc 2 and
// the error names the unknown stage.
TEST_F(TiInfoCalibrateTest, UnknownStageRejected) {
  const auto result = run_ti_info(path_, {"--calibrate", "--wcet", "t/nope=1"});
  EXPECT_EQ(result.exit_code, 2) << "stdout: " << result.out;
  EXPECT_NE(result.err.find("t/nope"), std::string::npos) << result.err;
}

// --wcet without --calibrate is an argument error -> rc 2.
TEST_F(TiInfoCalibrateTest, WcetRequiresCalibrate) {
  const auto result = run_ti_info(path_, {"--wcet", "t/out=1"});
  EXPECT_EQ(result.exit_code, 2) << "stdout: " << result.out;
  EXPECT_FALSE(result.err.empty());
}

}  // namespace
