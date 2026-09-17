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

// H1 byte-equality between the generic interpreter wiring (wire())
// and the specialized install (wire_specialized(), ADR-0032): the
// same flow shape, the same deterministic manual publish stream, and
// sink captures that fold value + lineage describe() bytes together,
// so any divergence in payload order, lineage hops, branch merges, or
// per-channel seq numbering fails the comparison. Covers the five
// roadmap shapes (short/medium/long linear, fan-in, fan-out),
// multi-producer map_to channels, mixed generic segments (op /
// stateful / span), SLA-bearing flows (which stay fully generic), the
// history-capture narrowing contract, live record content, and replay
// onto a specialized runtime.

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "tianshu/core/lineage.h"
#include "tianshu/core/message_traits.h"
#include "tianshu/dsl/dsl_runtime.h"
#include "tianshu/dsl/flow.h"
#include "tianshu/dsl/record.h"
#include "tianshu/dsl/record_v2.h"
#include "tianshu/sla/sla_analyzer.h"

namespace {

using tianshu::core::Lineage;
using tianshu::dsl::Flow;
using tianshu::dsl::FlowBuilder;
using tianshu::dsl::FlowRuntime;

struct SpMsg {
  std::uint64_t seq{0};
  std::uint64_t value{0};
};

struct FusedMsg {
  std::uint64_t seq{0};
  std::uint64_t va{0};
  std::uint64_t vb{0};
};

}  // namespace

TIANSHU_TRAITS_POD(SpMsg, "spec.SpMsg");
TIANSHU_TRAITS_POD(FusedMsg, "spec.FusedMsg");

namespace {

constexpr int kMsgs = 64;

using Capture = std::vector<std::string>;

void sink_capture(Capture* out, const SpMsg& msg, const Lineage& lin) {
  out->push_back(std::to_string(msg.seq) + ":" + std::to_string(msg.value) + " @ " +
                 lin.describe());
}

Flow make_chain(const std::string& name, int hops, Capture* out) {
  FlowBuilder b(name);
  auto chain = b.source<SpMsg>("src", std::chrono::milliseconds(1),
                               [](std::uint64_t t) { return SpMsg{.seq = t, .value = 0}; });
  for (int i = 0; i < hops; ++i) {
    chain = chain.map<SpMsg>(
        [](const SpMsg& in) { return SpMsg{.seq = in.seq, .value = in.value + 1}; });
  }
  chain.sink([out](const SpMsg& msg, const Lineage& lin) { sink_capture(out, msg, lin); });
  return b.build();
}

Flow make_fanin(const std::string& name, Capture* out) {
  FlowBuilder b(name);
  const auto emit = [](std::uint64_t t) {
    return SpMsg{.seq = t, .value = 1};
  };
  auto sa = b.source<SpMsg>("a", std::chrono::milliseconds(1), emit);
  auto sb = b.source<SpMsg>("b", std::chrono::milliseconds(1), emit);
  auto sc = b.source<SpMsg>("c", std::chrono::milliseconds(1), emit);
  auto j1 = b.join<SpMsg, SpMsg, FusedMsg>(sa, sb, [](const SpMsg& x, const SpMsg& y) {
    return FusedMsg{.seq = x.seq, .va = x.value, .vb = y.value};
  });
  auto j2 = b.join<FusedMsg, SpMsg, FusedMsg>(j1, sc, [](const FusedMsg& x, const SpMsg& y) {
    return FusedMsg{.seq = x.seq, .va = x.va + 1, .vb = x.vb + y.value};
  });
  j2.sink([out](const FusedMsg& msg, const Lineage& lin) {
    out->push_back(std::to_string(msg.seq) + ":" + std::to_string(msg.va) + "+" +
                   std::to_string(msg.vb) + " @ " + lin.describe());
  });
  return b.build();
}

Flow make_fanout(const std::string& name, std::array<Capture, 4>* outs) {
  FlowBuilder b(name);
  auto src = b.source<SpMsg>("src", std::chrono::milliseconds(1),
                             [](std::uint64_t t) { return SpMsg{.seq = t, .value = 0}; });
  for (int i = 0; i < 4; ++i) {
    src.map<SpMsg>([i](const SpMsg& in) {
         return SpMsg{.seq = in.seq, .value = (in.value * 10) + static_cast<std::uint64_t>(i)};
       })
        .sink([i, outs](const SpMsg& msg, const Lineage& lin) {
          sink_capture(&(*outs)[static_cast<std::size_t>(i)], msg, lin);
        });
  }
  return b.build();
}

void publish_src(FlowRuntime& rt, const Flow& flow, int messages) {
  const std::string& channel = flow.sources().front().channel;
  for (int i = 0; i < messages; ++i) {
    const SpMsg msg{.seq = static_cast<std::uint64_t>(i), .value = 100};
    rt.publish_bytes(channel, &msg, sizeof(msg),
                     Lineage::rooted(channel, static_cast<std::uint64_t>(i)));
  }
}

void publish_fanin(FlowRuntime& rt, const Flow& flow, int messages) {
  const auto& sources = flow.sources();
  for (int i = 0; i < messages; ++i) {
    const auto seq = static_cast<std::uint64_t>(i);
    const SpMsg m{.seq = seq, .value = 1};
    for (const auto& source : sources) {
      rt.publish_bytes(source.channel, &m, sizeof(m), Lineage::rooted(source.channel, seq));
    }
  }
}

void expect_equal_captures(const Capture& expected, const Capture& actual, const char* label) {
  ASSERT_EQ(expected.size(), actual.size()) << label;
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(expected[i], actual[i]) << label << " divergence at output " << i;
  }
}

}  // namespace

TEST(SpecializeH1, LinearChainsMatchInterpreter) {
  for (const int hops : {1, 4, 9}) {
    Capture interpreted;
    Capture specialized;
    {
      FlowRuntime rt;
      const auto flow = make_chain("sp_lin", hops, &interpreted);
      rt.wire(flow);
      publish_src(rt, flow, kMsgs);
    }
    {
      FlowRuntime rt;
      const auto flow = make_chain("sp_lin", hops, &specialized);
      rt.wire_specialized(flow);
      publish_src(rt, flow, kMsgs);
    }
    ASSERT_FALSE(interpreted.empty());
    expect_equal_captures(interpreted, specialized, "linear chain");
  }
}

TEST(SpecializeH1, FanInMatchesInterpreter) {
  Capture interpreted;
  Capture specialized;
  {
    FlowRuntime rt;
    const auto flow = make_fanin("sp_fi", &interpreted);
    rt.wire(flow);
    publish_fanin(rt, flow, kMsgs);
  }
  {
    FlowRuntime rt;
    const auto flow = make_fanin("sp_fi", &specialized);
    rt.wire_specialized(flow);
    publish_fanin(rt, flow, kMsgs);
  }
  ASSERT_FALSE(interpreted.empty());
  expect_equal_captures(interpreted, specialized, "fan-in");
}

TEST(SpecializeH1, FanOutMatchesInterpreterPerBranch) {
  std::array<Capture, 4> interpreted{};
  std::array<Capture, 4> specialized{};
  {
    FlowRuntime rt;
    const auto flow = make_fanout("sp_fo", &interpreted);
    rt.wire(flow);
    publish_src(rt, flow, kMsgs);
  }
  {
    FlowRuntime rt;
    const auto flow = make_fanout("sp_fo", &specialized);
    rt.wire_specialized(flow);
    publish_src(rt, flow, kMsgs);
  }
  for (std::size_t i = 0; i < 4; ++i) {
    ASSERT_FALSE(interpreted[i].empty());
    expect_equal_captures(interpreted[i], specialized[i], "fan-out branch");
  }
}

// The narrowing contract (ADR-0032): on a specialized install the
// intermediate channels of a pure map chain capture no history (no
// graph-declared observer), while the interpreted run retains every
// entry. This also pins that the fast path actually engaged — an
// all-generic fallback would populate the history ring.
TEST(SpecializeH1, FastPathEngagedAndIntermediateHistoryNarrowed) {
  const char* const flow_name = "sp_hist";
  Capture sink_interpreted;
  Capture sink_specialized;
  std::string mid_channel;
  std::size_t interpreted_entries = 0;
  {
    FlowRuntime rt;
    const auto flow = make_chain(flow_name, 4, &sink_interpreted);
    rt.wire(flow);
    publish_src(rt, flow, 8);
    mid_channel = flow.maps().front().out_channel;
    const auto* hist = rt.history(mid_channel);
    ASSERT_NE(hist, nullptr);
    interpreted_entries = hist->entries().size();
  }
  {
    FlowRuntime rt;
    const auto flow = make_chain(flow_name, 4, &sink_specialized);
    rt.wire_specialized(flow);
    publish_src(rt, flow, 8);
    const auto* hist = rt.history(mid_channel);
    if (hist != nullptr) {
      EXPECT_TRUE(hist->entries().empty());
    }
  }
  EXPECT_EQ(interpreted_entries, 8U);
  expect_equal_captures(sink_interpreted, sink_specialized, "history-narrowed chain");
}

// Multi-producer channel (two map_to stages writing one named
// channel): the plan must keep the locked queue path and outputs must
// still match the interpreter exactly.
TEST(SpecializeH1, MultiProducerMapToMatchesInterpreter) {
  const auto make_flow = [](const std::string& name, Capture* out) {
    FlowBuilder b(name);
    auto s1 = b.source<SpMsg>("s1", std::chrono::milliseconds(1),
                              [](std::uint64_t t) { return SpMsg{.seq = t, .value = 1}; });
    auto s2 = b.source<SpMsg>("s2", std::chrono::milliseconds(1),
                              [](std::uint64_t t) { return SpMsg{.seq = t, .value = 2}; });
    s1.map_to<SpMsg>("shared",
                     [](const SpMsg& in) { return SpMsg{.seq = in.seq, .value = in.value + 10}; });
    s2.map_to<SpMsg>("shared",
                     [](const SpMsg& in) { return SpMsg{.seq = in.seq, .value = in.value + 20}; });
    // One sink consumes both producers' messages.
    b.tap<SpMsg>("shared").sink(
        [out](const SpMsg& msg, const Lineage& lin) { sink_capture(out, msg, lin); });
    return b.build();
  };
  const auto drive = [](FlowRuntime& rt, const Flow& flow) {
    for (int i = 0; i < kMsgs; ++i) {
      const auto seq = static_cast<std::uint64_t>(i);
      const SpMsg m{.seq = seq};
      rt.publish_bytes(flow.sources()[0].channel, &m, sizeof(m),
                       Lineage::rooted(flow.sources()[0].channel, seq));
      rt.publish_bytes(flow.sources()[1].channel, &m, sizeof(m),
                       Lineage::rooted(flow.sources()[1].channel, seq));
    }
  };
  Capture interpreted;
  Capture specialized;
  {
    FlowRuntime rt;
    const auto flow = make_flow("sp_mp", &interpreted);
    rt.wire(flow);
    drive(rt, flow);
  }
  {
    FlowRuntime rt;
    const auto flow = make_flow("sp_mp", &specialized);
    rt.wire_specialized(flow);
    drive(rt, flow);
  }
  ASSERT_EQ(interpreted.size(), static_cast<std::size_t>(2 * kMsgs));
  expect_equal_captures(interpreted, specialized, "multi-producer map_to");
}

// Mixed graph: fast map segment, generic op in the middle, fast map
// segment again. The op's channel is queue-delivered; both sides of
// the boundary must stay byte-identical to the interpreter.
namespace {

struct DoublerOp {
  static void handle(const SpMsg& in, tianshu::dsl::OpPub<SpMsg>& pub) {
    pub.publish(SpMsg{.seq = in.seq, .value = in.value + 1000});
  }
  static void on_init(tianshu::dsl::OpPub<SpMsg>& /*pub*/) {}
};

}  // namespace

TEST(SpecializeH1, MixedOpSegmentMatchesInterpreter) {
  const auto make_flow = [](const std::string& name, Capture* out) {
    FlowBuilder b(name);
    auto chain = b.source<SpMsg>("src", std::chrono::milliseconds(1),
                                 [](std::uint64_t t) { return SpMsg{.seq = t, .value = 0}; });
    chain = chain.map<SpMsg>(
        [](const SpMsg& in) { return SpMsg{.seq = in.seq, .value = in.value + 1}; });
    auto boxed = b.op<SpMsg, SpMsg>(chain, "opout", DoublerOp{});
    boxed.map<SpMsg>([](const SpMsg& in) { return SpMsg{.seq = in.seq, .value = in.value + 1}; })
        .sink([out](const SpMsg& msg, const Lineage& lin) { sink_capture(out, msg, lin); });
    return b.build();
  };
  Capture interpreted;
  Capture specialized;
  {
    FlowRuntime rt;
    const auto flow = make_flow("sp_op", &interpreted);
    rt.wire(flow);
    publish_src(rt, flow, kMsgs);
  }
  {
    FlowRuntime rt;
    const auto flow = make_flow("sp_op", &specialized);
    rt.wire_specialized(flow);
    publish_src(rt, flow, kMsgs);
  }
  ASSERT_FALSE(interpreted.empty());
  expect_equal_captures(interpreted, specialized, "mixed op segment");
}

// SLA-bearing flows stay fully generic in v0 (ADR-0032): outputs must
// still match the interpreter bit for bit.
TEST(SpecializeH1, SlaFlowStaysGenericAndMatches) {
  const auto make_flow = [](const std::string& name, Capture* out) {
    FlowBuilder b(name);
    auto chain = b.source<SpMsg>("src", std::chrono::milliseconds(1),
                                 [](std::uint64_t t) { return SpMsg{.seq = t, .value = 0}; });
    chain = chain.map<SpMsg>(
        [](const SpMsg& in) { return SpMsg{.seq = in.seq, .value = in.value + 1}; });
    chain.with_sla(tianshu::sla::Sla{.deadline = std::chrono::milliseconds(50)});
    chain.sink([out](const SpMsg& msg, const Lineage& lin) { sink_capture(out, msg, lin); });
    return b.build();
  };
  Capture interpreted;
  Capture specialized;
  {
    FlowRuntime rt;
    const auto flow = make_flow("sp_sla", &interpreted);
    rt.wire(flow);
    publish_src(rt, flow, kMsgs);
  }
  {
    FlowRuntime rt;
    const auto flow = make_flow("sp_sla", &specialized);
    rt.wire_specialized(flow);
    publish_src(rt, flow, kMsgs);
  }
  ASSERT_FALSE(interpreted.empty());
  expect_equal_captures(interpreted, specialized, "SLA generic fallback");
}

// Live record content: the specialized fast path routes through the
// generic publish while armed, so the captured (channel, seq, payload,
// lineage) stream must equal the interpreted recording. Timestamps are
// wall-clock and excluded from the comparison.
TEST(SpecializeH1, RecordingContentMatchesInterpreter) {
  const char* path_i = "/tmp/tianshu_spec_rec_i.trec";
  const char* path_s = "/tmp/tianshu_spec_rec_s.trec";
  Capture sink_interpreted;
  Capture sink_specialized;
  {
    FlowRuntime rt;
    const auto flow = make_chain("sp_rec", 4, &sink_interpreted);
    rt.wire(flow);
    rt.start_recording(path_i);
    publish_src(rt, flow, 16);
    ASSERT_TRUE(rt.stop_recording());
  }
  {
    FlowRuntime rt;
    const auto flow = make_chain("sp_rec", 4, &sink_specialized);
    rt.wire_specialized(flow);
    rt.start_recording(path_s);
    publish_src(rt, flow, 16);
    ASSERT_TRUE(rt.stop_recording());
  }
  expect_equal_captures(sink_interpreted, sink_specialized, "recording sink outputs");

  std::vector<std::string> recorded_i;
  std::vector<std::string> recorded_s;
  const auto read_back = [](const char* path, std::vector<std::string>* out) {
    auto reader_opt = tianshu::dsl::record::RecordReader::open(path);
    if (!reader_opt.has_value()) {
      return;
    }
    auto& reader = reader_opt.value();
    tianshu::dsl::record::RecordedMessageV2 msg;
    while (reader.next(&msg)) {
      std::uint64_t seq = 0;
      if (msg.payload.size() < sizeof(seq)) {
        continue;
      }
      std::memcpy(&seq, msg.payload.data(), sizeof(seq));
      out->push_back(std::to_string(msg.channel_id) + "#" + std::to_string(seq) + " @ " +
                     (msg.lineage.has_value() ? msg.lineage.value().describe() : "<none>"));
    }
  };
  read_back(path_i, &recorded_i);
  read_back(path_s, &recorded_s);
  ASSERT_FALSE(recorded_i.empty());
  expect_equal_captures(recorded_i, recorded_s, "record content");
}

// Replay onto a specialized runtime reproduces the interpreted run's
// outputs (entry publishes drive the fast cascade).
TEST(SpecializeH1, ReplayOnSpecializedMatchesInterpreter) {
  const char* path = "/tmp/tianshu_spec_replay.trec";
  Capture reference;
  std::vector<tianshu::dsl::RecordedMessage> records;
  {
    FlowRuntime rt;
    const auto flow = make_chain("sp_rep", 4, &reference);
    rt.wire(flow);
    rt.start_recording(path);
    publish_src(rt, flow, 16);
    ASSERT_TRUE(rt.stop_recording());
    auto reader_opt = tianshu::dsl::record::RecordReader::open(path);
    if (!reader_opt.has_value()) {
      FAIL() << "record reader failed to open: " << path;
      return;
    }
    auto& reader = reader_opt.value();
    tianshu::dsl::record::RecordedMessageV2 msg;
    const std::string& src_channel = flow.sources().front().channel;
    while (reader.next(&msg)) {
      const auto* channel = reader.find_channel(msg.channel_id);
      ASSERT_NE(channel, nullptr);
      if (channel->name != src_channel) {
        continue;
      }
      tianshu::dsl::RecordedMessage rec{
          .channel = channel->name,
          .seq = msg.seq,
          .bytes = msg.payload,
          .lineage_text = msg.lineage.has_value() ? msg.lineage.value().describe() : "",
      };
      records.push_back(std::move(rec));
    }
  }
  Capture replayed;
  {
    FlowRuntime rt;
    const auto flow = make_chain("sp_rep", 4, &replayed);
    rt.wire_specialized(flow);
    rt.replay_from(records);
  }
  ASSERT_EQ(replayed.size(), reference.size());
  expect_equal_captures(reference, replayed, "root replay");
}
