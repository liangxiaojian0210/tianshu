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

#include "tianshu/sla/sla_analyzer.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tianshu::sla {
namespace {

using Micros = std::chrono::microseconds;

struct PathNode {
  const SlaNode* node{nullptr};
  Micros effective_wcet{0};
};

struct PathResult {
  Micros cost{0};
  std::vector<std::string> path;  // channels, source ... endpoint
  std::vector<PathNode> nodes;    // producing nodes, upstream first
};

// Iterative DFS over the channel graph. Feedback edges (map_to
// write-backs) make the graph cyclic: an input channel already on the
// current walk terminates that branch, so a cycle contributes at most
// once per path (conservative v0 stance per ADR-0029 D3).
class Backtracker {
 public:
  Backtracker(const std::unordered_map<std::string, const SlaNode*>& producer,
              const std::unordered_map<std::string, const SlaSource*>& source,
              const SlaConfig& config, std::vector<std::string>& default_notes,
              std::unordered_map<std::string, Micros>& source_worst)
      : producer_(producer),
        source_(source),
        config_(config),
        default_notes_(default_notes),
        source_worst_(source_worst) {}

  [[nodiscard]] PathResult worst_path(const std::string& endpoint) {
    stack_.clear();
    walk_.clear();
    nodes_.clear();
    // Reset per endpoint: a surviving best_ makes a later, smaller-
    // deadline endpoint inherit this path as a false violation.
    best_ = PathResult{};
    stack_.push_back(Frame{.channel = endpoint, .acc = Micros{0}});
    run();
    return best_;
  }

 private:
  struct Frame {
    std::string channel;
    Micros acc{0};
    const SlaNode* node{nullptr};
    Micros wcet{0};
    std::size_t next_input{0};
    bool entered{false};
    bool on_nodes{false};
  };

  void run() {
    while (!stack_.empty()) {
      Frame& frame = stack_.back();
      if (!frame.entered) {
        frame.entered = true;
        enter(frame);
      }
      if (frame.node != nullptr && frame.next_input < frame.node->in_channels.size()) {
        const auto& in = frame.node->in_channels[frame.next_input++];
        if (std::ranges::find(walk_, in) == walk_.end()) {
          // Hop cost is charged per consumed input edge (ADR-0029 D2).
          stack_.push_back(Frame{.channel = in, .acc = frame.acc + frame.wcet + config_.hop_cost});
        }
        continue;
      }
      leave(frame);
      stack_.pop_back();
    }
  }

  void enter(Frame& frame) {
    walk_.push_back(frame.channel);

    const auto src = source_.find(frame.channel);
    if (src != source_.end()) {
      auto& worst = source_worst_[frame.channel];
      worst = std::max(worst, frame.acc);
      if (frame.acc > best_.cost) {
        best_.cost = frame.acc;
        best_.path = walk_;
        best_.nodes = nodes_;
      }
    }

    const auto prod = producer_.find(frame.channel);
    if (prod != producer_.end()) {
      frame.node = prod->second;
      frame.wcet = frame.node->wcet;
      if (frame.wcet.count() <= 0) {
        frame.wcet = config_.default_wcet;
        note_default_wcet(*frame.node);
      }
      nodes_.push_back(PathNode{.node = frame.node, .effective_wcet = frame.wcet});
      frame.on_nodes = true;
    }
  }

  void leave(Frame& frame) {
    if (frame.on_nodes) {
      nodes_.pop_back();
    }
    walk_.pop_back();
  }

  void note_default_wcet(const SlaNode& node) {
    const std::string note = node.kind + " -> " + node.out_channel;
    if (std::ranges::find(default_notes_, note) == default_notes_.end()) {
      default_notes_.push_back(note);
    }
  }

  const std::unordered_map<std::string, const SlaNode*>& producer_;
  const std::unordered_map<std::string, const SlaSource*>& source_;
  const SlaConfig& config_;
  std::vector<std::string>& default_notes_;
  std::unordered_map<std::string, Micros>& source_worst_;

  std::vector<Frame> stack_;
  std::vector<std::string> walk_;
  std::vector<PathNode> nodes_;
  PathResult best_;
};

void verify_endpoints(const std::vector<SlaEndpoint>& endpoints,
                      const std::unordered_map<std::string, const SlaNode*>& producer,
                      const std::unordered_map<std::string, const SlaSource*>& source) {
  for (const auto& e : endpoints) {
    const bool reachable = producer.contains(e.channel) || source.contains(e.channel);
    if (!reachable) {
      throw std::invalid_argument("sla endpoint '" + e.channel +
                                  "' is neither produced nor a source");
    }
  }
}

[[nodiscard]] SlaViolation build_violation(const SlaEndpoint& endpoint, const PathResult& worst) {
  SlaViolation v;
  v.endpoint = endpoint.channel;
  v.deadline = endpoint.deadline;
  v.planned = worst.cost;
  v.path = worst.path;
  const PathNode* offender = nullptr;
  for (const auto& pn : worst.nodes) {
    if (offender == nullptr || pn.effective_wcet > offender->effective_wcet) {
      offender = &pn;
    }
  }
  if (offender != nullptr) {
    v.worst_offender = offender->node->kind + " -> " + offender->node->out_channel;
  }
  return v;
}

[[nodiscard]] Micros proportional_share(Micros total, Micros part, Micros whole) {
  const auto ratio = static_cast<double>(part.count()) / static_cast<double>(whole.count());
  const auto scaled = static_cast<std::int64_t>(static_cast<double>(total.count()) * ratio);
  return Micros{scaled};
}

void merge_budgets(const SlaEndpoint& endpoint, const PathResult& worst,
                   std::unordered_map<std::string, Micros>& budget_by_channel) {
  Micros wcet_sum{0};
  for (const auto& pn : worst.nodes) {
    wcet_sum += pn.effective_wcet;
  }
  for (const auto& pn : worst.nodes) {
    // Deadline descends the critical path proportionally to WCET
    // (ADR-0029 D4); a node feeding several endpoints keeps the min.
    const auto share = wcet_sum.count() > 0
                           ? proportional_share(endpoint.deadline, pn.effective_wcet, wcet_sum)
                           : Micros{0};
    const auto it = budget_by_channel.find(pn.node->out_channel);
    if (it == budget_by_channel.end()) {
      budget_by_channel.emplace(pn.node->out_channel, share);
    } else {
      it->second = std::min(it->second, share);
    }
  }
}

void append_saturation(SlaReport& report, const std::vector<SlaSource>& sources,
                       const std::unordered_map<std::string, Micros>& source_worst,
                       const SlaConfig& config) {
  const unsigned cores =
      config.machine_cores != 0 ? config.machine_cores : std::thread::hardware_concurrency();
  double utilization = 0.0;
  for (const auto& s : sources) {
    if (s.interval.count() <= 0) {
      continue;  // aperiodic: no utilization term
    }
    const auto it = source_worst.find(s.channel);
    if (it != source_worst.end()) {
      const auto period_us = std::chrono::duration_cast<std::chrono::microseconds>(s.interval);
      utilization +=
          static_cast<double>(it->second.count()) / static_cast<double>(period_us.count());
    }
  }
  const double bound = static_cast<double>(cores) * (1.0 - config.margin);
  if (utilization > bound) {
    std::string msg = "sla saturation: utilization ";
    msg += std::to_string(utilization);
    msg += " > cores*(1-margin) = ";
    msg += std::to_string(bound);
    msg += config.strict_utilization ? " (strict: load rejected)" : " (warning)";
    report.saturation_warning = std::move(msg);
  }
}

}  // namespace

SlaReport SlaAnalyzer::analyze(const std::vector<SlaSource>& sources,
                               const std::vector<SlaNode>& nodes,
                               const std::vector<SlaEndpoint>& endpoints, const SlaConfig& config) {
  std::unordered_map<std::string, const SlaNode*> producer;
  std::unordered_map<std::string, const SlaSource*> source;
  for (const auto& n : nodes) {
    producer.emplace(n.out_channel, &n);
  }
  for (const auto& s : sources) {
    source.emplace(s.channel, &s);
  }
  verify_endpoints(endpoints, producer, source);

  SlaReport report;
  std::unordered_map<std::string, Micros> source_worst;
  std::unordered_map<std::string, Micros> budget_by_channel;
  Backtracker tracker(producer, source, config, report.default_wcet_notes, source_worst);

  for (const auto& e : endpoints) {
    const PathResult worst = tracker.worst_path(e.channel);
    if (worst.cost > e.deadline) {
      report.violations.push_back(build_violation(e, worst));
      continue;
    }
    merge_budgets(e, worst, budget_by_channel);
  }

  report.budgets.reserve(budget_by_channel.size());
  for (const auto& [channel, planned] : budget_by_channel) {
    report.budgets.push_back(SlaBudget{.channel = channel, .planned = planned});
  }
  std::ranges::sort(report.budgets,
                    [](const SlaBudget& a, const SlaBudget& b) { return a.channel < b.channel; });

  append_saturation(report, sources, source_worst, config);
  report.ok = report.violations.empty() &&
              (!config.strict_utilization || report.saturation_warning.empty());
  return report;
}

std::string SlaReport::format() const {
  std::string out = "sla report: ";
  out += ok ? "OK" : "REJECTED";
  out += "\n";
  for (const auto& v : violations) {
    out += "  violation: endpoint '" + v.endpoint + "' planned " +
           std::to_string(v.planned.count()) + "us > deadline " +
           std::to_string(v.deadline.count()) + "us\n";
    out += "    path:";
    for (const auto& c : v.path) {
      out += " " + c;
    }
    out += "\n";
    if (!v.worst_offender.empty()) {
      out += "    worst offender: " + v.worst_offender + "\n";
    }
  }
  if (!saturation_warning.empty()) {
    out += "  " + saturation_warning + "\n";
  }
  for (const auto& n : default_wcet_notes) {
    out += "  undeclared wcet (default applied): " + n + "\n";
  }
  for (const auto& b : budgets) {
    out += "  budget: " + b.channel + " " + std::to_string(b.planned.count()) + "us\n";
  }
  return out;
}

}  // namespace tianshu::sla
