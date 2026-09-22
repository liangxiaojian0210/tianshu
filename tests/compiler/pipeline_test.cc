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

// Pipeline acceptance tests (ADR-0030 M-B): a compiled artifact runs
// its flow and produces the interpreter's exact output; caching,
// degraded fallback, and the artifact hash guard are proven here.

#include "tianshu/compiler/pipeline.h"

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "tianshu/core/lineage.h"
#include "tianshu/core/message_traits.h"
#include "tianshu/dsl/dsl_runtime.h"
#include "tianshu/dsl/flow.h"

// NOLINTNEXTLINE(misc-use-internal-linkage)  // traits must precede template use
struct PipeTick {
  std::uint64_t tick;
};
// NOLINTNEXTLINE(misc-use-internal-linkage)  // same ordering constraint
struct PipeDouble {
  std::uint64_t tick;
  std::uint64_t value;
};

TIANSHU_TRAITS_POD(PipeTick, "pipe.TickMsg");
TIANSHU_TRAITS_POD(PipeDouble, "pipe.DoubledMsg");

namespace {

namespace compiler = tianshu::compiler;
namespace dsl = tianshu::dsl;

// Bazel sandboxes only guarantee TEST_TMPDIR writable; bare runs share /tmp,
// so the fallback cache is PID-suffixed to keep parallel invocations apart.
std::string cache_dir() {
  const char* tmp = std::getenv("TEST_TMPDIR");
  return tmp != nullptr
             ? std::string(tmp) + "/tianshu-gen-cache"
             : "/tmp/tianshu-pipeline-test-cache." + std::to_string(static_cast<int>(getpid()));
}

compiler::CompileOptions cache_opts(const std::string& dir, std::string cc = {}) {
  compiler::CompileOptions opts;
  opts.cache_dir = dir;
  opts.compiler = std::move(cc);
  return opts;
}

dsl::Flow make_chain(const std::string& name, std::vector<std::string>* out) {
  dsl::FlowBuilder b(name);
  b.source<PipeTick>("ticks", std::chrono::milliseconds(5),
                     [](std::uint64_t t) { return PipeTick{.tick = t}; })
      .map<PipeDouble>(
          [](const PipeTick& in) { return PipeDouble{.tick = in.tick, .value = in.tick * 2}; })
      .sink([out](const PipeDouble& msg, const tianshu::core::Lineage&) {
        out->push_back("tick=" + std::to_string(msg.tick) + " value=" + std::to_string(msg.value));
      });
  return b.build();
}

// Both runs emit ticks 0,1,2,... from their own start, so outputs share
// an identical prefix; only the tail count can drift by scheduling.
void expect_matching_outputs(const std::vector<std::string>& a, const std::vector<std::string>& b) {
  ASSERT_FALSE(a.empty());
  ASSERT_FALSE(b.empty());
  const std::size_t common = std::min(a.size(), b.size());
  EXPECT_GE(common, 15U);  // ~23 ticks at 5ms over 120ms
  for (std::size_t i = 0; i < common; ++i) {
    EXPECT_EQ(a[i], b[i]) << "divergence at output " << i;
  }
}

TEST(PipelineTest, CompiledMatchesInterpreter) {
  std::filesystem::remove_all(cache_dir());

  std::vector<std::string> interpreted;
  {
    dsl::FlowRuntime rt;
    rt.run_for(make_chain("pipe_e2e", &interpreted), std::chrono::milliseconds(120));
  }

  std::vector<std::string> compiled_out;
  auto compiled =
      compiler::Pipeline::compile(make_chain("pipe_e2e", &compiled_out), cache_opts(cache_dir()));
  ASSERT_TRUE(compiled.valid());
  EXPECT_FALSE(compiled.degraded());
  {
    dsl::FlowRuntime rt;
    compiled.run(rt, make_chain("pipe_e2e", &compiled_out), std::chrono::milliseconds(120));
  }

  expect_matching_outputs(interpreted, compiled_out);
}

TEST(PipelineTest, SecondCompileHitsCache) {
  std::filesystem::remove_all(cache_dir());

  std::vector<std::string> sink;
  const auto flow = make_chain("pipe_cache", &sink);
  auto first = compiler::Pipeline::compile(flow, cache_opts(cache_dir()));
  ASSERT_TRUE(first.valid());
  EXPECT_FALSE(first.from_cache());

  auto second = compiler::Pipeline::compile(flow, cache_opts(cache_dir()));
  ASSERT_TRUE(second.valid());
  EXPECT_TRUE(second.from_cache());
  EXPECT_TRUE(std::filesystem::exists(second.artifact_path()));
}

TEST(PipelineTest, MissingCompilerDegradesButStillRuns) {
  std::filesystem::remove_all(cache_dir());

  std::vector<std::string> interpreted;
  {
    dsl::FlowRuntime rt;
    rt.run_for(make_chain("pipe_deg", &interpreted), std::chrono::milliseconds(120));
  }

  std::vector<std::string> degraded_out;
  auto compiled =
      compiler::Pipeline::compile(make_chain("pipe_deg", &degraded_out),
                                  cache_opts(cache_dir(), "/nonexistent/tianshu-compiler"));
  EXPECT_TRUE(compiled.degraded());
  EXPECT_FALSE(compiled.valid());
  {
    dsl::FlowRuntime rt;
    compiled.run(rt, make_chain("pipe_deg", &degraded_out), std::chrono::milliseconds(120));
  }

  expect_matching_outputs(interpreted, degraded_out);
}

TEST(PipelineTest, ArtifactRejectsForeignFlow) {
  std::filesystem::remove_all(cache_dir());

  std::vector<std::string> sink;
  auto compiled =
      compiler::Pipeline::compile(make_chain("pipe_guard", &sink), cache_opts(cache_dir()));
  ASSERT_TRUE(compiled.valid());

  // Different topology (one extra map) -> different hash -> rejected.
  std::vector<std::string> other_sink;
  dsl::FlowBuilder b("pipe_guard");
  b.source<PipeTick>("ticks", std::chrono::milliseconds(5),
                     [](std::uint64_t t) { return PipeTick{.tick = t}; })
      .map<PipeDouble>(
          [](const PipeTick& in) { return PipeDouble{.tick = in.tick, .value = in.tick * 2}; })
      .map<PipeDouble>(
          [](const PipeDouble& in) { return PipeDouble{.tick = in.tick, .value = in.value + 1}; })
      .sink([&other_sink](const PipeDouble& msg, const tianshu::core::Lineage&) {
        other_sink.push_back(std::to_string(msg.tick));
      });

  dsl::FlowRuntime rt;
  EXPECT_THROW(static_cast<void>(compiled.run(rt, b.build(), std::chrono::milliseconds(50))),
               std::invalid_argument);
}

}  // namespace
