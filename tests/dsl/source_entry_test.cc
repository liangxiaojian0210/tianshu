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

// H1 byte-equality for the typed source entry (ADR-0033): a specialized
// runtime driven through SourceEntry must produce the same sink captures
// (message fields + lineage describe bytes) as the generic interpreter
// driven through publish_bytes. Covers linear chains, fan-in (one entry
// per source channel), SLA-bearing flows (entry stays generic by
// eligibility), and observer-declared history capture (a span consuming
// the source channel's history slice transitively proves the entry's
// history pushes: a missed or wrong push would diverge the span output).

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "tianshu/core/lineage.h"
#include "tianshu/core/message_traits.h"
#include "tianshu/dsl/dsl_runtime.h"
#include "tianshu/dsl/flow.h"
#include "tianshu/dsl/record_v2.h"
#include "tianshu/sla/sla_analyzer.h"

namespace {

using tianshu::core::Lineage;
using tianshu::dsl::Flow;
using tianshu::dsl::FlowBuilder;
using tianshu::dsl::FlowRuntime;
using tianshu::dsl::SourceEntry;

struct SeMsg {
  std::uint64_t seq{0};
  std::uint64_t value{0};
};

}  // namespace

TIANSHU_TRAITS_POD(SeMsg, "entry.SeMsg");

namespace {

constexpr int kMsgs = 64;

using Capture = std::vector<std::string>;

Flow make_chain(const std::string& name, int hops, Capture* out, bool with_sla = false) {
  FlowBuilder b(name);
  auto chain = b.source<SeMsg>("src", std::chrono::milliseconds(1),
                               [](std::uint64_t t) { return SeMsg{.seq = t, .value = 0}; });
  for (int i = 0; i < hops; ++i) {
    chain = chain.map<SeMsg>(
        [](const SeMsg& in) { return SeMsg{.seq = in.seq, .value = in.value + 1}; });
  }
  auto tail = chain.sink([out](const SeMsg& msg, const Lineage& lin) {
    out->push_back(std::to_string(msg.seq) + ":" + std::to_string(msg.value) + " @ " +
                   lin.describe());
  });
  if (with_sla) {
    tail.with_sla(tianshu::sla::Sla{.deadline = std::chrono::milliseconds(50)});
  }
  return b.build();
}

void publish_generic(FlowRuntime& rt, const Flow& flow, int messages) {
  const std::string& channel = flow.sources().front().channel;
  for (int i = 0; i < messages; ++i) {
    const SeMsg msg{.seq = static_cast<std::uint64_t>(i), .value = 100};
    rt.publish_bytes(channel, &msg, sizeof(msg),
                     Lineage::rooted(channel, static_cast<std::uint64_t>(i)));
  }
}

void publish_typed(FlowRuntime& rt, const Flow& flow, int messages) {
  SourceEntry entry(rt, flow, flow.sources().front().channel);
  for (int i = 0; i < messages; ++i) {
    entry.publish(SeMsg{.seq = static_cast<std::uint64_t>(i), .value = 100},
                  static_cast<std::uint64_t>(i));
  }
}

void expect_equal_captures(const Capture& expected, const Capture& actual, const char* label) {
  ASSERT_EQ(expected.size(), actual.size()) << label;
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(expected[i], actual[i]) << label << " divergence at output " << i;
  }
}

}  // namespace

TEST(SourceEntryH1, LinearChainsMatchGeneric) {
  for (const int hops : {1, 4, 9}) {
    Capture interpreted;
    Capture specialized;
    Capture typed;
    {
      FlowRuntime rt;
      const auto flow = make_chain("se_lin", hops, &interpreted);
      rt.wire(flow);
      publish_generic(rt, flow, kMsgs);
    }
    {
      FlowRuntime rt;
      const auto flow = make_chain("se_lin", hops, &specialized);
      rt.wire_specialized(flow);
      publish_generic(rt, flow, kMsgs);
    }
    {
      FlowRuntime rt;
      const auto flow = make_chain("se_lin", hops, &typed);
      rt.wire_specialized(flow);
      publish_typed(rt, flow, kMsgs);
    }
    ASSERT_FALSE(interpreted.empty());
    expect_equal_captures(interpreted, specialized, "specialized publish_bytes");
    expect_equal_captures(interpreted, typed, "typed source entry");
  }
}

TEST(SourceEntryH1, FanInOneEntryPerSourceMatchesGeneric) {
  const auto build = [](Capture* out) {
    FlowBuilder b("se_fanin");
    const auto emit = [](std::uint64_t t) {
      return SeMsg{.seq = t, .value = 1};
    };
    auto sa = b.source<SeMsg>("a", std::chrono::milliseconds(1), emit);
    auto sb = b.source<SeMsg>("b", std::chrono::milliseconds(1), emit);
    auto sc = b.source<SeMsg>("c", std::chrono::milliseconds(1), emit);
    b.join<SeMsg, SeMsg, SeMsg>(sa, sb, [](const SeMsg& x, const SeMsg& y) {
       return SeMsg{.seq = x.seq, .value = x.value + y.value};
     }).sink([out](const SeMsg& msg, const Lineage& lin) {
      out->push_back(std::to_string(msg.seq) + ":" + std::to_string(msg.value) + " @ " +
                     lin.describe());
    });
    static_cast<void>(sc);
    return b.build();
  };
  const auto drive_generic = [](FlowRuntime& rt, const Flow& flow, int messages) {
    for (int i = 0; i < messages; ++i) {
      for (const auto& source : flow.sources()) {
        const SeMsg msg{.seq = static_cast<std::uint64_t>(i), .value = 1};
        rt.publish_bytes(source.channel, &msg, sizeof(msg),
                         Lineage::rooted(source.channel, static_cast<std::uint64_t>(i)));
      }
    }
  };
  const auto drive_typed = [](FlowRuntime& rt, const Flow& flow, int messages) {
    std::vector<std::unique_ptr<SourceEntry>> entries;
    for (const auto& source : flow.sources()) {
      entries.push_back(std::make_unique<SourceEntry>(rt, flow, source.channel));
    }
    for (int i = 0; i < messages; ++i) {
      for (auto& entry : entries) {
        entry->publish(SeMsg{.seq = static_cast<std::uint64_t>(i), .value = 1},
                       static_cast<std::uint64_t>(i));
      }
    }
  };
  Capture interpreted;
  Capture typed;
  {
    FlowRuntime rt;
    const auto flow = build(&interpreted);
    rt.wire(flow);
    drive_generic(rt, flow, kMsgs);
  }
  {
    FlowRuntime rt;
    const auto flow = build(&typed);
    rt.wire_specialized(flow);
    drive_typed(rt, flow, kMsgs);
  }
  ASSERT_FALSE(interpreted.empty());
  expect_equal_captures(interpreted, typed, "fan-in typed entries");
}

TEST(SourceEntryH1, SlaFlowStaysGenericAndMatches) {
  Capture interpreted;
  Capture typed;
  {
    FlowRuntime rt;
    const auto flow = make_chain("se_sla", 2, &interpreted, /*with_sla=*/true);
    rt.wire(flow);
    publish_generic(rt, flow, kMsgs);
  }
  {
    FlowRuntime rt;
    const auto flow = make_chain("se_sla", 2, &typed, /*with_sla=*/true);
    rt.wire_specialized(flow);
    publish_typed(rt, flow, kMsgs);
  }
  expect_equal_captures(interpreted, typed, "SLA flow entry");
}

TEST(SourceEntryH1, ObserverHistoryCapturedForObservedChannel) {
  // The span consumes the data channel's history slice, so equal span
  // outputs between the generic and entry-driven runs prove the entry's
  // history pushes transitively (ADR-0033 D3).
  const auto build = [](Capture* out) {
    FlowBuilder b("se_obs");
    const auto data = b.tap<SeMsg>("d");
    const auto trig = b.tap<SeMsg>("g");
    auto fused = b.span_join<SeMsg>(
        trig, data,
        [](const SeMsg& t) { return std::pair<std::uint64_t, std::uint64_t>(0, t.seq + 1); },
        [](const SeMsg& m) { return m.seq; },
        [](const SeMsg& /*t*/, const tianshu::dsl::Slice<SeMsg>& slice) {
          std::uint64_t sum = 0;
          for (const auto& item : slice.items) {
            sum += item.value;
          }
          return SeMsg{.seq = slice.items.size(), .value = sum};
        });
    fused.sink([out](const SeMsg& msg, const Lineage& lin) {
      out->push_back(std::to_string(msg.seq) + ":" + std::to_string(msg.value) + " @ " +
                     lin.describe());
    });
    b.source<SeMsg>("d", std::chrono::milliseconds(1),
                    [](std::uint64_t t) { return SeMsg{.seq = t, .value = 2}; });
    b.source<SeMsg>("g", std::chrono::milliseconds(1),
                    [](std::uint64_t t) { return SeMsg{.seq = t, .value = 0}; });
    return b.build();
  };
  const auto drive = [](auto&& publish_d, auto&& publish_g) {
    for (int i = 0; i < kMsgs; ++i) {
      publish_d(SeMsg{.seq = static_cast<std::uint64_t>(i), .value = 2},
                static_cast<std::uint64_t>(i));
    }
    publish_g(SeMsg{.seq = kMsgs - 1, .value = 0}, static_cast<std::uint64_t>(kMsgs - 1));
  };
  Capture interpreted;
  Capture typed;
  {
    FlowRuntime rt;
    const auto flow = build(&interpreted);
    rt.wire(flow);
    const auto pub = [&rt, &flow](const SeMsg& m, std::uint64_t seq) {
      const std::string& channel =
          m.value == 0 ? flow.sources()[1].channel : flow.sources()[0].channel;
      rt.publish_bytes(channel, &m, sizeof(m), Lineage::rooted(channel, seq));
    };
    drive(pub, pub);
  }
  {
    FlowRuntime rt;
    const auto flow = build(&typed);
    rt.wire_specialized(flow);
    SourceEntry entry_d(rt, flow, flow.sources()[0].channel);
    SourceEntry entry_g(rt, flow, flow.sources()[1].channel);
    drive([&entry_d](const SeMsg& m, std::uint64_t seq) { entry_d.publish(m, seq); },
          [&entry_g](const SeMsg& m, std::uint64_t seq) { entry_g.publish(m, seq); });
  }
  ASSERT_FALSE(interpreted.empty());
  expect_equal_captures(interpreted, typed, "observer history via span output");
}

// Recorder-armed runs: the entry falls back to publish_bytes per message
// (ADR-0033 D4), so record files stay byte-identical to an interpreted
// run of the same flow.
TEST(SourceEntryH1, RecorderArmedFallsBackToGeneric) {
  const char* path_i = "/tmp/tianshu_se_entry_rec_i.trec";
  const char* path_s = "/tmp/tianshu_se_entry_rec_s.trec";
  Capture interpreted;
  Capture typed;
  {
    FlowRuntime rt;
    const auto flow = make_chain("se_rec", 2, &interpreted);
    rt.wire(flow);
    rt.start_recording(path_i);
    publish_generic(rt, flow, kMsgs);
    ASSERT_TRUE(rt.stop_recording());
  }
  {
    FlowRuntime rt;
    const auto flow = make_chain("se_rec", 2, &typed);
    rt.wire_specialized(flow);
    rt.start_recording(path_s);
    publish_typed(rt, flow, kMsgs);
    ASSERT_TRUE(rt.stop_recording());
  }
  expect_equal_captures(interpreted, typed, "recording sink outputs");

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
