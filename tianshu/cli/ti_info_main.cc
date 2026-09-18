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

// ti-info: inspect a Tianshu Record v2 file (channels, statistics,
// metadata). Works on any .trec file without linking publisher code.
//
//   ti info <file.trec>                    summary
//   ti info <file.trec> --messages N       dump first N messages per channel
//   ti info <file.trec> --lineage          show lineage for messages
//   ti info <file.trec> --calibrate        H3 loop (ADR-0029): per-stage
//                                          measured e2e from lineage hops
//                                          (out ts - last input hop ts),
//                                          P50/P99/P99.9 -> WCET suggestion
//   ti info <file.trec> --calibrate
//           --wcet STAGE=US[,...]          D2 drift check (ADR-0029):
//                                          compare declared .with_wcet()
//                                          values against the suggestion;
//                                          >3x -> DRIFT-HIGH (rc 3),
//                                          <1/3 -> DRIFT-LOW (rc 3)

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "tianshu/dsl/record_v2.h"

namespace {

void print_usage() {
  static_cast<void>(
      std::fprintf(stderr,
                   "usage: ti-info <file.trec> [--messages N] [--lineage] [--calibrate]\n"
                   "                 (--calibrate only) [--wcet STAGE=US[,STAGE=US...]]\n"));
}

// Parses a --wcet argument "NAME=US[,NAME=US...]" into name -> us.
// Stage names are OUTPUT channel names (how the calibrate table keys
// stages); a later duplicate of a name overwrites the earlier one.
// Returns false (after printing a diagnostic) on a malformed pair.
bool parse_wcet_spec(const std::string& spec, std::map<std::string, double>* out) {
  std::size_t start = 0;
  while (start < spec.size()) {
    std::size_t end = spec.find(',', start);
    if (end == std::string::npos) {
      end = spec.size();
    }
    const std::string pair = spec.substr(start, end - start);
    const std::size_t eq = pair.find('=');
    bool ok = eq != std::string::npos && eq > 0 && eq + 1 < pair.size();
    if (ok) {
      const std::string name = pair.substr(0, eq);
      const std::string value = pair.substr(eq + 1);
      char* parse_end = nullptr;
      const double us = std::strtod(value.c_str(), &parse_end);
      ok = parse_end != nullptr && *parse_end == '\0';
      if (ok) {
        (*out)[name] = us;
      }
    }
    if (!ok) {
      static_cast<void>(std::fprintf(
          stderr, "ti-info: --wcet: malformed pair '%s' (expected NAME=US)\n", pair.c_str()));
      return false;
    }
    start = end + 1;
  }
  return true;
}

// Formats one fixed-width table cell (declared us / ratio columns).
std::string fmt_cell(const char* fmt, double v) {
  char buf[32];  // NOLINT(modernize-avoid-c-arrays)
  static_cast<void>(std::snprintf(buf, sizeof(buf), fmt, v));
  return {buf};
}

// Prints one calibrate row — percentiles, suggestion, DECLARED/RATIO
// cells, and (when declared) the ADR-0029 D2 drift verdict line.
// Returns true when the stage is in drift.
bool print_stage_row(const std::string& stage_name, const std::vector<std::uint64_t>& sorted,
                     const std::map<std::string, double>& declared_wcet_us) {
  const auto pick = [&sorted](double f) {
    return sorted[std::min(sorted.size() - 1,
                           static_cast<std::size_t>(f * static_cast<double>(sorted.size())))];
  };
  const double wcet_us =
      static_cast<double>(pick(0.999)) / 1000.0 * 1.3;  // ADR-0029 H3: p99.9 x 1.0~1.3
  const auto decl_it = declared_wcet_us.find(stage_name);
  const bool declared = decl_it != declared_wcet_us.end();
  const double declared_us = declared ? decl_it->second : 0.0;
  const bool has_ratio = declared && wcet_us > 0.0;
  const double ratio = has_ratio ? declared_us / wcet_us : 0.0;
  static_cast<void>(std::printf(
      "%-30s %8zu %10.1f %10.1f %10.1f %16.1f %12s %19s\n", stage_name.c_str(), sorted.size(),
      static_cast<double>(pick(0.50)) / 1000.0, static_cast<double>(pick(0.99)) / 1000.0,
      static_cast<double>(pick(0.999)) / 1000.0, wcet_us,
      declared ? fmt_cell("%.1f", declared_us).c_str() : "-",
      has_ratio ? fmt_cell("%.2f", ratio).c_str() : "-"));
  // ADR-0029 D2: declared outside [suggest/3, 3*suggest] is drift.
  if (declared && declared_us > 3.0 * wcet_us) {
    static_cast<void>(
        std::printf("[drift] %s declared=%.1f suggest=%.1f ratio=%.2f -> DRIFT-HIGH"
                    " (over-conservative >3x, ADR-0029 D2)\n",
                    stage_name.c_str(), declared_us, wcet_us, ratio));
    return true;
  }
  if (declared && 3.0 * declared_us < wcet_us) {
    static_cast<void>(
        std::printf("[drift] %s declared=%.1f suggest=%.1f ratio=%.2f -> DRIFT-LOW"
                    " (unsafe-optimistic <suggest/3, ADR-0029 D2)\n",
                    stage_name.c_str(), declared_us, wcet_us, ratio));
    return true;
  }
  return false;
}

// H3 calibration (ADR-0029 Phase 1): for every recorded message whose
// lineage carries hops, the stage that produced it measured
//   ts(out) - ts(last input hop's channel+seq)
// using the recorded timestamp of the lineage-named input message.
// Aggregates P50/P99/P99.9 per output channel and suggests a WCET.
// With declared WCETs (ADR-0029 D2), prints DECLARED/RATIO columns and
// a per-stage drift verdict. Returns the process exit code: 0 clean,
// 2 unknown declared stage name, 3 any drift detected.
int calibrate_from_lineage(tianshu::dsl::record::RecordReader& reader,
                           const std::map<std::string, double>& declared_wcet_us) {
  struct ChannelTiming {
    std::vector<std::uint64_t> ns;
  };
  // (channel, seq) -> ts, for resolving lineage hops.
  std::unordered_map<std::uint64_t, std::uint64_t> ts_index;
  const auto key = [](std::uint16_t ch, std::uint64_t seq) {
    return (static_cast<std::uint64_t>(ch) << 48) | (seq & 0xFFFFFFFFFFFFULL);
  };

  std::unordered_map<std::uint16_t, ChannelTiming> timings;
  tianshu::dsl::record::RecordedMessageV2 msg;
  while (reader.next(&msg)) {
    if (msg.lineage.has_value() && !msg.lineage->hops().empty()) {
      // Hops exclude the root; the LAST hop is the message's own
      // channel — the producing stage's INPUT is the second-to-last
      // hop, or the root for single-hop (direct map) lineages.
      const auto& hops = msg.lineage->hops();
      const auto& in_hop = hops.size() >= 2 ? hops[hops.size() - 2] : msg.lineage->root();
      const auto* in_ch = reader.find_channel(in_hop.channel);
      if (in_ch != nullptr) {
        const auto ts_it = ts_index.find(key(in_ch->id, in_hop.seq));
        if (ts_it != ts_index.end() && msg.ts_ns >= ts_it->second) {
          timings[msg.channel_id].ns.push_back(msg.ts_ns - ts_it->second);
        }
      }
    }
    ts_index[key(msg.channel_id, msg.seq)] = msg.ts_ns;
  }

  static_cast<void>(std::printf("\n--- H3 calibration (stage e2e from lineage) ---\n"));
  static_cast<void>(std::printf("%-30s %8s %10s %10s %10s %16s %12s %19s\n", "STAGE(out<-in)", "N",
                                "p50(us)", "p99(us)", "p99.9(us)", "WCET_suggest(us)",
                                "DECLARED(us)", "RATIO(decl/suggest)"));
  static_cast<void>(std::printf("%-30s %8s %10s %10s %10s %16s %12s %19s\n", "---------------",
                                "-----", "-------", "-------", "---------", "----------------",
                                "------------", "-------------------"));
  bool any_drift = false;
  std::set<std::string> stage_names;
  for (const auto& [ch_id, timing] : timings) {
    if (timing.ns.empty()) {
      continue;
    }
    std::vector<std::uint64_t> sorted(timing.ns);
    std::ranges::sort(sorted);
    const auto* ch = reader.find_channel(ch_id);
    const std::string stage_name = ch != nullptr ? ch->name : "?";
    stage_names.insert(stage_name);
    any_drift = print_stage_row(stage_name, sorted, declared_wcet_us) || any_drift;
  }
  const auto unknown = std::ranges::find_if(declared_wcet_us, [&stage_names](const auto& entry) {
    return !stage_names.contains(entry.first);
  });
  if (unknown != declared_wcet_us.end()) {
    static_cast<void>(std::fprintf(stderr,
                                   "ti-info: --wcet: unknown stage '%s' (stages are keyed"
                                   " by output channel name)\n",
                                   unknown->first.c_str()));
    return 2;
  }
  static_cast<void>(
      std::printf("\n[note] WCET_suggest = p99.9 x 1.3 (ADR-0029 H3 target window upper\n"
                  "bound). Feed back into .with_wcet() declarations; declared values\n"
                  "diverging >3x from these are drift (ADR-0029 D2).\n"));
  return any_drift ? 3 : 0;
}

void dump_messages_preview(tianshu::dsl::record::RecordReader& reader,
                           std::uint64_t max_per_channel, bool show_lineage) {
  static_cast<void>(std::printf("\n--- messages (first %llu per channel) ---\n",
                                static_cast<unsigned long long>(max_per_channel)));
  std::unordered_map<std::uint16_t, std::uint64_t> per_channel_count;
  tianshu::dsl::record::RecordedMessageV2 msg;
  while (reader.next(&msg)) {
    const auto count_it = per_channel_count.find(msg.channel_id);
    const std::uint64_t count = count_it != per_channel_count.end() ? count_it->second : 0;
    if (count >= max_per_channel) {
      continue;
    }
    per_channel_count[msg.channel_id] = count + 1;
    const auto* ch = reader.find_channel(msg.channel_id);
    const std::string ch_name = ch != nullptr ? ch->name : "?";
    static_cast<void>(std::printf("  [%s] seq=%llu ts=%llu size=%zu", ch_name.c_str(),
                                  static_cast<unsigned long long>(msg.seq),
                                  static_cast<unsigned long long>(msg.ts_ns), msg.payload.size()));
    if (show_lineage && msg.lineage.has_value()) {
      static_cast<void>(std::printf(" lineage: %s", msg.lineage->describe().c_str()));
    }
    static_cast<void>(std::printf("\n"));
  }
}

struct CliOptions {
  std::uint64_t dump_messages = 0;
  bool show_lineage = false;
  bool calibrate = false;
  std::map<std::string, double> declared_wcet;
};

// Parses argv[2..]; returns nullopt (after printing a diagnostic and
// usage) on argument errors such as a malformed --wcet pair or --wcet
// without --calibrate.
std::optional<CliOptions> parse_options(int argc, char** argv) {
  CliOptions opts;
  bool wcet_given = false;
  for (int i = 2; i < argc; ++i) {
    const std::string arg(argv[i]);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    if (arg == "--messages" && i + 1 < argc) {
      opts.dump_messages = std::strtoull(argv[++i], nullptr, 10);  // NOLINT
    } else if (arg == "--lineage") {
      opts.show_lineage = true;
    } else if (arg == "--calibrate") {
      opts.calibrate = true;
    } else if (arg == "--wcet") {
      if (i + 1 >= argc) {
        static_cast<void>(
            std::fprintf(stderr, "ti-info: --wcet requires an argument NAME=US[,...]\n"));
        return std::nullopt;
      }
      wcet_given = true;
      if (!parse_wcet_spec(argv[++i], &opts.declared_wcet)) {  // NOLINT
        return std::nullopt;
      }
    }
  }
  if (wcet_given && !opts.calibrate) {
    static_cast<void>(
        std::fprintf(stderr, "ti-info: --wcet is only valid together with --calibrate\n"));
    return std::nullopt;
  }
  return opts;
}

}  // namespace

int main(int argc, char** argv) try {
  if (argc < 2) {
    print_usage();
    return 2;
  }

  const std::string path = argv[1];  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  const auto options = parse_options(argc, argv);
  if (!options.has_value()) {
    print_usage();
    return 2;
  }

  auto reader_opt = tianshu::dsl::record::RecordReader::open(path);
  if (!reader_opt.has_value()) {
    static_cast<void>(
        std::fprintf(stderr, "ti-info: cannot open %s (not a v2 record file?)\n", path.c_str()));
    return 1;
  }
  auto& reader = reader_opt.value();

  // Summary.
  const auto& stats = reader.stats();
  static_cast<void>(std::printf("file: %s\n", path.c_str()));
  static_cast<void>(std::printf("  version:  v%u\n", reader.major_version()));
  static_cast<void>(std::printf("  channels: %zu\n", reader.channels().size()));
  static_cast<void>(
      std::printf("  messages: %llu\n", static_cast<unsigned long long>(stats.total_messages)));
  static_cast<void>(
      std::printf("  bytes:    %llu\n", static_cast<unsigned long long>(stats.total_bytes)));
  if (stats.duration_ns > 0) {
    static_cast<void>(
        std::printf("  duration: %.3fs\n", static_cast<double>(stats.duration_ns) / 1e9));
  }
  static_cast<void>(std::printf("\n"));

  // Per-channel statistics.
  static_cast<void>(
      std::printf("%-30s %8s %12s %8s %10s\n", "CHANNEL", "COUNT", "BYTES", "TYPE", "RATE(hz)"));
  static_cast<void>(
      std::printf("%-30s %8s %12s %8s %10s\n", "-------", "-----", "-----", "----", "--------"));
  for (const auto& ch : reader.channels()) {
    static_cast<void>(std::printf("%-30s %8llu %12llu %-8s %10.1f\n", ch.name.c_str(),
                                  static_cast<unsigned long long>(ch.message_count),
                                  static_cast<unsigned long long>(ch.payload_bytes),
                                  ch.type_name.c_str(), ch.avg_rate_hz));
  }

  // Optional: dump messages.
  if (options->dump_messages > 0) {
    dump_messages_preview(reader, options->dump_messages, options->show_lineage);
  }
  if (options->calibrate) {
    return calibrate_from_lineage(reader, options->declared_wcet);
  }

  return 0;
} catch (const std::exception& e) {
  static_cast<void>(std::fprintf(stderr, "ti-info: %s\n", e.what()));
  return 1;
}
