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

// DSL v0: declarative flow declarations (per ADR-0021).
//
// The builder records a strongly-typed graph; the runtime (dsl_runtime.h)
// interprets it on the L4 stack. This header is declaration-only, so the
// same Flow can later feed the L1 compiler unchanged.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tianshu/core/lineage.h"
#include "tianshu/core/message_traits.h"
#include "tianshu/sla/sla_analyzer.h"

namespace tianshu::dsl {

class FlowRuntime;
class SpecializeBuilder;

namespace detail {
// Defined in dsl_runtime.h AFTER FlowRuntime is complete: the lambda
// bodies perform member access on FlowRuntime, which must stay a
// dependent expression (two-phase lookup would otherwise require a
// complete type at this point).
template <typename T>
std::function<void(FlowRuntime&, std::uint64_t)> make_source_drive(
    std::string channel, std::function<T(std::uint64_t)> emit);

template <typename TIn, typename TOut>
std::function<void(FlowRuntime&)> make_map_wire(std::string in_channel, std::string out_channel,
                                                std::function<TOut(const TIn&)> fn);

// Specialize-hook factories (ADR-0032): the typed second wiring
// closure carried by every map/join/sink declaration; defined in
// dsl_runtime.h alongside make_map_wire (same dual-declaration
// discipline). Per ADR-0035 the factories carry the operator's own
// type F (deduced at the FlowBuilder call site) so specialized stages
// call it directly.
template <typename TIn, typename TOut, typename F>
std::function<void(FlowRuntime&, SpecializeBuilder&)> make_map_specialize(std::string in_channel,
                                                                          std::string out_channel,
                                                                          F fn);

template <typename TA, typename TB, typename TC>
std::function<void(FlowRuntime&)> make_join_wire(std::string in_a, std::string in_b,
                                                 std::string out_channel,
                                                 std::function<TC(const TA&, const TB&)> fn);

template <typename TA, typename TB, typename TC, typename F>
std::function<void(FlowRuntime&, SpecializeBuilder&)> make_join_specialize(std::string in_a,
                                                                           std::string in_b,
                                                                           std::string out_channel,
                                                                           F fn);

template <typename TIn, typename TOut, typename TOp>
std::function<void(FlowRuntime&)> make_op_wire(std::string in_channel, std::string out_channel,
                                               TOp impl);

template <typename TIn, typename TOut, typename TState, typename TImpl>
std::function<void(FlowRuntime&)> make_stateful_wire(std::string in_channel,
                                                     std::string out_channel,
                                                     std::string state_channel, TImpl impl);

template <typename TTrig, typename TData, typename TOut, typename TSpanFn, typename TTimeFn,
          typename TImpl>
std::function<void(FlowRuntime&)> make_span_wire(std::string trig_channel, std::string data_channel,
                                                 std::string out_channel, TSpanFn span_fn,
                                                 TTimeFn time_fn, TImpl impl);

template <typename TOut>
bool probe_source_shape(std::string_view registry_name);

template <typename TIn, typename TOut>
bool probe_component_shape(std::string_view registry_name);

template <typename TIn0, typename TIn1, typename TOut>
bool probe_component2_shape(std::string_view registry_name);

template <typename TOut>
std::function<void(FlowRuntime&)> make_from_source_wire(std::string registry_name,
                                                        std::string out_channel,
                                                        std::chrono::milliseconds interval);

template <typename TIn, typename TOut>
std::function<void(FlowRuntime&)> make_from_component_wire(std::string registry_name,
                                                           std::string in_channel,
                                                           std::string out_channel);

template <typename TIn0, typename TIn1, typename TOut>
std::function<void(FlowRuntime&)> make_from_component2_wire(std::string registry_name,
                                                            std::string in_channel0,
                                                            std::string in_channel1,
                                                            std::string out_channel);

template <typename T>
std::function<void(FlowRuntime&)> make_sink_wire(
    std::string channel, std::function<void(const T&, const core::Lineage&)> fn);

template <typename T, typename F>
std::function<void(FlowRuntime&, SpecializeBuilder&)> make_sink_specialize(std::string channel,
                                                                           F fn);
}  // namespace detail

// Materialized slice of a channel's bounded history (ADR-0026): the
// framework copies matching messages (POD memcpy) at trigger time, so
// the member set is exactly known — that is what makes slice lineage
// precise.
template <typename T>
struct Slice {
  std::vector<T> items;
  std::uint64_t seq_lo{0};
  std::uint64_t seq_hi{0};  // inclusive; lo > hi when empty
  bool truncated{false};    // history ring may have dropped span head

  [[nodiscard]] bool empty() const { return seq_lo > seq_hi; }
};

// Typed handle to a graph edge; the template parameter makes wiring
// mistakes a compile error.
template <typename T>
class Stream {
 public:
  Stream() = default;
  Stream(std::string channel, std::string type_name)
      : channel_(std::move(channel)), type_name_(std::move(type_name)) {}

  [[nodiscard]] const std::string& channel() const { return channel_; }
  [[nodiscard]] const std::string& type_name() const { return type_name_; }
  [[nodiscard]] bool valid() const { return !channel_.empty(); }

 private:
  std::string channel_;
  std::string type_name_;
};

// ---------------------------------------------------------------------------
// The recorded declaration graph (immutable after build())
// ---------------------------------------------------------------------------

class Flow {
 public:
  // Type-erased but self-describing node records: the runtime walks
  // these; the future compiler consumes these.
  struct SourceDecl {
    std::string channel;
    std::chrono::milliseconds interval;
    std::string type_name;
    // One-tick driver: publishes the payload + its root lineage.
    std::function<void(FlowRuntime& rt, std::uint64_t tick)> drive;
  };
  struct MapDecl {
    std::string in_channel;
    std::string out_channel;
    std::string in_type_name;
    std::string out_type_name;
    // Wiring installer: attaches the transformation to the runtime.
    std::function<void(FlowRuntime& rt)> wire;
    // Specialized installer (ADR-0032): typed closure that hands the
    // operator to a SpecializeBuilder instead of replaying wire().
    std::function<void(FlowRuntime& rt, SpecializeBuilder& plan)> specialize;
  };
  struct JoinDecl {
    std::string in_channel_a;
    std::string in_channel_b;
    std::string out_channel;
    std::string type_name_a;
    std::string type_name_b;
    std::string out_type_name;
    std::function<void(FlowRuntime& rt)> wire;
    std::function<void(FlowRuntime& rt, SpecializeBuilder& plan)> specialize;
  };
  struct OpDecl {
    std::string in_channel;
    std::string out_channel;
    std::string in_type_name;
    std::string out_type_name;
    std::function<void(FlowRuntime& rt)> wire;
  };
  struct SpanDecl {
    std::string trig_channel;
    std::string data_channel;
    std::string out_channel;
    std::string out_type_name;
    std::function<void(FlowRuntime& rt)> wire;
  };
  struct StatefulDecl {
    std::string in_channel;
    std::string out_channel;
    std::string state_channel;
    std::string out_type_name;
    std::string state_type_name;
    std::function<void(FlowRuntime& rt)> wire;
  };
  struct FromDecl {
    std::string registry_name;
    std::string in_channel;    // empty = source-like (timer-driven)
    std::string in_channel_2;  // non-empty = two-input referenced component
    std::string out_channel;
    std::string out_type_name;
    std::chrono::milliseconds interval{};
    std::function<void(FlowRuntime& rt)> wire;
  };
  struct SinkDecl {
    std::string channel;
    std::string type_name;
    std::function<void(FlowRuntime& rt)> wire;
    std::function<void(FlowRuntime& rt, SpecializeBuilder& plan)> specialize;
  };

  [[nodiscard]] const std::string& name() const { return name_; }
  [[nodiscard]] const std::vector<SourceDecl>& sources() const { return sources_; }
  [[nodiscard]] const std::vector<MapDecl>& maps() const { return maps_; }
  [[nodiscard]] const std::vector<JoinDecl>& joins() const { return joins_; }
  [[nodiscard]] const std::vector<OpDecl>& ops() const { return ops_; }
  [[nodiscard]] const std::vector<StatefulDecl>& statefuls() const { return statefuls_; }
  [[nodiscard]] const std::vector<SpanDecl>& spans() const { return spans_; }
  [[nodiscard]] const std::vector<FromDecl>& froms() const { return froms_; }
  [[nodiscard]] const std::vector<SinkDecl>& sinks() const { return sinks_; }

  // Load-time SLA verdict (ADR-0029): budgets, violations, saturation.
  // Empty report when the flow declared no SLA endpoints.
  [[nodiscard]] const sla::SlaReport& sla_report() const { return sla_report_; }
  [[nodiscard]] const std::vector<sla::SlaEndpoint>& sla_endpoints() const {
    return sla_endpoints_;
  }

  // Declared per-node WCETs keyed by producing node's output channel
  // (ADR-0029 D2); consumed by the compiler IR (ADR-0030 D2).
  [[nodiscard]] const std::map<std::string, std::chrono::microseconds>& wcet_by_out() const {
    return wcet_by_out_;
  }

  // Declared degradation target (ADR-0031): name of the registered flow to
  // fall back to when this flow persistently misses its SLA. Empty when no
  // fallback was declared.
  [[nodiscard]] const std::string& fallback_flow() const { return fallback_flow_; }

  // Wiring summary: "src -> map -> sink" with channels, for tests.
  [[nodiscard]] std::string describe() const {
    std::string out = "flow " + name_ + ":";
    for (const auto& s : sources_) {
      out += " src[" + s.channel + "]";
    }
    for (const auto& m : maps_) {
      out += " map[" + m.in_channel + " -> " + m.out_channel + "]";
    }
    for (const auto& j : joins_) {
      out += " join[" + j.in_channel_a + " + " + j.in_channel_b + " -> " + j.out_channel + "]";
    }
    for (const auto& b : ops_) {
      out += " op[" + b.in_channel + " -> " + b.out_channel + "]";
    }
    for (const auto& s : statefuls_) {
      out += " stateful[" + s.in_channel + " -> " + s.out_channel + " + " + s.state_channel + "]";
    }
    for (const auto& s : spans_) {
      out += " span[" + s.trig_channel + " x " + s.data_channel + " -> " + s.out_channel + "]";
    }
    for (const auto& f : froms_) {
      if (f.in_channel.empty()) {
        out += " from[" + f.registry_name + " -> " + f.out_channel + "]";
      } else if (f.in_channel_2.empty()) {
        out += " from[" + f.in_channel + " via " + f.registry_name + " -> " + f.out_channel + "]";
      } else {
        out += " from[" + f.in_channel + " + " + f.in_channel_2 + " via " + f.registry_name +
               " -> " + f.out_channel + "]";
      }
    }
    for (const auto& s : sinks_) {
      out += " sink[" + s.channel + "]";
    }
    return out;
  }

 private:
  friend class FlowBuilder;
  explicit Flow(std::string name) : name_(std::move(name)) {}

  std::string name_;
  std::vector<SourceDecl> sources_;
  std::vector<MapDecl> maps_;
  std::vector<JoinDecl> joins_;
  std::vector<OpDecl> ops_;
  std::vector<StatefulDecl> statefuls_;
  std::vector<SpanDecl> spans_;
  std::vector<FromDecl> froms_;
  std::vector<SinkDecl> sinks_;
  std::vector<sla::SlaEndpoint> sla_endpoints_;
  std::map<std::string, std::chrono::microseconds> wcet_by_out_;
  sla::SlaReport sla_report_;
  std::string fallback_flow_;
};

// ---------------------------------------------------------------------------
// Builder + chained API
//
//   auto flow = FlowBuilder("demo")
//                   .source<T>("ticks", 10ms, emit)
//                   .map<T, U>(fn)
//                   .sink<U>(collect)
//                   .build();
//
// FlowChain holds a raw pointer to its builder: the chained expression is
// one full statement, so a temporary builder stays alive through the whole
// chain (v0 constraint; split chains require keeping the builder alive).
// ---------------------------------------------------------------------------

template <typename T>
class FlowChain;

class FlowBuilder {
 public:
  explicit FlowBuilder(std::string name) : name_(std::move(name)) {}

  template <typename T>
  FlowChain<T> source(std::string_view name, std::chrono::milliseconds interval,
                      std::function<T(std::uint64_t)> emit);

  // AllLatest fusion of two streams (ADR-0021 amendment): fires when both
  // inputs are non-empty, consumes one message from each; the output
  // lineage merges both parents' branches.
  template <typename TA, typename TB, typename TC, typename F>
  FlowChain<TC> join(const FlowChain<TA>& a, const FlowChain<TB>& b, F fn);

  // User-defined operator with lifecycle (ADR-0024): on_init may publish
  // an initial output at wiring time (feedback-loop bootstrap); handle
  // transforms inputs and publishes derived outputs.
  template <typename TIn, typename TOut, typename TOp>
  FlowChain<TOut> op(const FlowChain<TIn>& in, std::string_view out_name, TOp impl);

  // Stateful operator (ADR-0027): like op, plus a SECOND publish handle
  // bound to a state channel — every state publication is a versioned,
  // lineage-carrying message (state lineage = input lineage + hop, so
  // the state's provenance records exactly which input it absorbed).
  template <typename TOut, typename TState, typename TIn, typename TImpl>
  FlowChain<TOut> stateful(const FlowChain<TIn>& in, std::string_view out_name,
                           std::string_view state_name, TImpl impl) {
    const std::string out = channel_for(out_name);
    const std::string st = channel_for(state_name);
    Flow::StatefulDecl decl{in.stream_.channel(),
                            out,
                            st,
                            std::string(core::MessageTraits<TOut>::name()),
                            std::string(core::MessageTraits<TState>::name()),
                            detail::make_stateful_wire<TIn, TOut, TState, TImpl>(
                                in.stream_.channel(), out, st, std::move(impl))};
    statefuls_.push_back(std::move(decl));
    return FlowChain<TOut>(this, Stream<TOut>(out, std::string(core::MessageTraits<TOut>::name())));
  }

  // Trigger-aligned slice fusion (ADR-0026): on each trigger message,
  // materialize the data channel's history slice where time(msg) is in
  // [t0, t1] = span_fn(trigger) — the output lineage merges the trigger
  // branch with a RANGE branch over the slice members.
  template <typename TOut, typename TTrig, typename TData, typename TSpanFn, typename TTimeFn,
            typename TImpl>
  FlowChain<TOut> span_join(const FlowChain<TTrig>& trig, const FlowChain<TData>& data,
                            TSpanFn span_fn, TTimeFn time_fn, TImpl impl) {
    const std::string out = anon_channel();
    Flow::SpanDecl decl{trig.stream_.channel(), data.stream_.channel(), out,
                        std::string(core::MessageTraits<TOut>::name()),
                        detail::make_span_wire<TTrig, TData, TOut, TSpanFn, TTimeFn, TImpl>(
                            trig.stream_.channel(), data.stream_.channel(), out, std::move(span_fn),
                            std::move(time_fn), std::move(impl))};
    spans_.push_back(std::move(decl));
    return FlowChain<TOut>(this, Stream<TOut>(out, std::string(core::MessageTraits<TOut>::name())));
  }

  // Handle-only declaration bound to a named channel (no producer
  // declared): breaks cycles in feedback graphs — join can reference the
  // port before the box writing it is constructed.
  template <typename T>
  FlowChain<T> tap(std::string_view name) {
    return FlowChain<T>(this,
                        Stream<T>(channel_for(name), std::string(core::MessageTraits<T>::name())));
  }

  // Reference a REGISTERED component by name (ADR-0025). Source-like
  // overload: TimerSourceComponent<TOut>; interval is flow-declared.
  // Returns an INVALID chain when the name is unknown or the shape does
  // not match (check valid()).
  template <typename TOut>
  FlowChain<TOut> from(std::string_view registry_name, std::string_view out_name,
                       std::chrono::milliseconds interval) {
    if (!detail::probe_source_shape<TOut>(registry_name)) {
      return FlowChain<TOut>(this, Stream<TOut>());
    }
    const std::string out = channel_for(out_name);
    Flow::FromDecl decl{
        std::string(registry_name),
        {},
        {},
        out,
        std::string(core::MessageTraits<TOut>::name()),
        interval,
        detail::make_from_source_wire<TOut>(std::string(registry_name), out, interval)};
    froms_.push_back(std::move(decl));
    return FlowChain<TOut>(this, Stream<TOut>(out, std::string(core::MessageTraits<TOut>::name())));
  }

  // Read-write overload: Component<TIn, TOut>; the input channel is the
  // chain's, the output channel is flow-declared (injected).
  template <typename TIn, typename TOut>
  FlowChain<TOut> from(std::string_view registry_name, const FlowChain<TIn>& in,
                       std::string_view out_name) {
    if (!detail::probe_component_shape<TIn, TOut>(registry_name)) {
      return FlowChain<TOut>(this, Stream<TOut>());
    }
    const std::string out = channel_for(out_name);
    Flow::FromDecl decl{std::string(registry_name),
                        in.stream_.channel(),
                        {},
                        out,
                        std::string(core::MessageTraits<TOut>::name()),
                        {},
                        detail::make_from_component_wire<TIn, TOut>(std::string(registry_name),
                                                                    in.stream_.channel(), out)};
    froms_.push_back(std::move(decl));
    return FlowChain<TOut>(this, Stream<TOut>(out, std::string(core::MessageTraits<TOut>::name())));
  }

  // Two-input read-write overload: Component<TIn0, TIn1, TOut>. Both input
  // channels come from the given chains; the component's visitor fuses them
  // and every publish inside proc carries BOTH inputs' lineage (merged
  // branches, same rule as a DSL join).
  template <typename TOut, typename TIn0, typename TIn1>
  FlowChain<TOut> from(std::string_view registry_name, const FlowChain<TIn0>& in0,
                       const FlowChain<TIn1>& in1, std::string_view out_name) {
    if (!detail::probe_component2_shape<TIn0, TIn1, TOut>(registry_name)) {
      return FlowChain<TOut>(this, Stream<TOut>());
    }
    const std::string out = channel_for(out_name);
    Flow::FromDecl decl{
        std::string(registry_name),
        in0.stream_.channel(),
        in1.stream_.channel(),
        out,
        std::string(core::MessageTraits<TOut>::name()),
        {},
        detail::make_from_component2_wire<TIn0, TIn1, TOut>(
            std::string(registry_name), in0.stream_.channel(), in1.stream_.channel(), out)};
    froms_.push_back(std::move(decl));
    return FlowChain<TOut>(this, Stream<TOut>(out, std::string(core::MessageTraits<TOut>::name())));
  }

  // Accepted and ignored in v0 (kept for source compatibility); the
  // typed FlowChain::with_sla(sla::Sla) is the real declaration path
  // consumed by the load-time analyzer (ADR-0029).
  FlowBuilder& with_sla(std::string_view sla);

  // Overrides the SLA analysis configuration (ADR-0029 D2/D3): default
  // WCET, hop cost, machine cores, saturation margin/strictness.
  FlowBuilder& with_sla_config(sla::SlaConfig config);

  // Declares the degradation target (ADR-0031): name of a flow registered
  // via REGISTER_TRACEABLE_FLOW. build() fails fast when the name is
  // unknown or self-referential — the fallback ladder is verified at load
  // time, like every other cross-flow reference.
  FlowBuilder& with_fallback(std::string_view flow_name);

  [[nodiscard]] Flow build();

 private:
  template <typename U>
  friend class FlowChain;

  template <typename TIn, typename TOut, typename F>
  Stream<TOut> map_stream(const Stream<TIn>& in, F fn);

  template <typename TIn, typename TOut, typename F>
  Stream<TOut> map_stream_to(const Stream<TIn>& in, std::string_view out_name, F fn);

  template <typename TIn, typename TOut>
  Stream<TOut> emit_map(const Stream<TIn>& in, const std::string& out_channel,
                        std::function<TOut(const TIn&)> fn);

  template <typename T, typename F>
  void sink_stream(const Stream<T>& in, F fn);

  [[nodiscard]] std::string channel_for(std::string_view name) const {
    return name_ + "/" + std::string(name);
  }
  std::string anon_channel() { return name_ + "/~" + std::to_string(anon_++); }

  void add_sla_endpoint(std::string channel, std::chrono::microseconds deadline) {
    sla_endpoints_.push_back(sla::SlaEndpoint{.channel = std::move(channel), .deadline = deadline});
  }
  void declare_wcet(const std::string& out_channel, std::chrono::microseconds budget) {
    wcet_by_out_[out_channel] = budget;
  }

  // Lowers the declaration graph into analyzer input (ADR-0029 D7) and
  // runs the load-time pass. No-op for graphs without SLA endpoints.
  void run_sla_analysis(Flow& flow) const;

  std::string name_;
  std::vector<Flow::SourceDecl> sources_;
  std::vector<Flow::MapDecl> maps_;
  std::vector<Flow::JoinDecl> joins_;
  std::vector<Flow::OpDecl> ops_;
  std::vector<Flow::StatefulDecl> statefuls_;
  std::vector<Flow::SpanDecl> spans_;
  std::vector<Flow::FromDecl> froms_;
  std::vector<Flow::SinkDecl> sinks_;
  std::vector<sla::SlaEndpoint> sla_endpoints_;
  std::map<std::string, std::chrono::microseconds> wcet_by_out_;
  sla::SlaConfig sla_config_;
  std::string fallback_flow_;
  std::uint64_t anon_{0};
};

template <typename T>
class FlowChain {
 public:
  template <typename TOut, typename F>
  FlowChain<TOut> map(F fn) const {
    return FlowChain<TOut>(builder_, builder_->map_stream<T, TOut>(stream_, std::move(fn)));
  }

  // Map with an explicit output channel (feedback edges: a stage writing
  // back into a channel that other stages also publish to or join on).
  template <typename TOut, typename F>
  FlowChain<TOut> map_to(std::string_view out_name, F fn) const {
    return FlowChain<TOut>(builder_,
                           builder_->map_stream_to<T, TOut>(stream_, out_name, std::move(fn)));
  }

  FlowChain<T>& with_sla(std::string_view sla) {
    builder_->with_sla(sla);
    return *this;
  }

  // Binds a deadline to the chain's current channel (endpoint semantics,
  // ADR-0029 D1). Flow::build() verifies every endpoint at load time and
  // throws when the worst upstream path exceeds the deadline.
  FlowChain<T>& with_sla(sla::Sla sla) {
    builder_->add_sla_endpoint(stream_.channel(), sla.deadline);
    return *this;
  }

  // Declares the worst-case execution time of the node that produced
  // this chain's channel (ADR-0029 D2). Undeclared nodes fall back to
  // the configured default WCET and are named in the analysis output.
  FlowChain<T>& with_wcet(std::chrono::microseconds budget) {
    builder_->declare_wcet(stream_.channel(), budget);
    return *this;
  }

  FlowChain<T>& with_fallback(std::string_view flow_name) {
    builder_->with_fallback(flow_name);
    return *this;
  }

  template <typename F>
  FlowChain<T>& sink(F fn) {
    builder_->sink_stream<T>(stream_, std::move(fn));
    return *this;
  }

  [[nodiscard]] Flow build() { return builder_->build(); }

  // False for chains returned by a failed from() reference (unknown
  // registration or shape mismatch).
  [[nodiscard]] bool valid() const { return stream_.valid(); }

 private:
  friend class FlowBuilder;
  template <typename U>
  friend class FlowChain;

  FlowChain(FlowBuilder* builder, Stream<T> stream)
      : builder_(builder), stream_(std::move(stream)) {}

  FlowBuilder* builder_;
  Stream<T> stream_;
};

// --- FlowBuilder member definitions (after FlowChain, for name lookup) ---

template <typename T>
FlowChain<T> FlowBuilder::source(std::string_view name, std::chrono::milliseconds interval,
                                 std::function<T(std::uint64_t)> emit) {
  const std::string ch = channel_for(name);
  Flow::SourceDecl decl{ch, interval, std::string(core::MessageTraits<T>::name()),
                        detail::make_source_drive<T>(ch, std::move(emit))};
  sources_.push_back(std::move(decl));
  return FlowChain<T>(this, Stream<T>(ch, std::string(core::MessageTraits<T>::name())));
}

inline FlowBuilder& FlowBuilder::with_sla(std::string_view /*sla*/) { return *this; }

inline FlowBuilder& FlowBuilder::with_sla_config(sla::SlaConfig config) {
  sla_config_ = config;
  return *this;
}

inline FlowBuilder& FlowBuilder::with_fallback(std::string_view flow_name) {
  fallback_flow_ = std::string(flow_name);
  return *this;
}

inline void FlowBuilder::run_sla_analysis(Flow& flow) const {
  if (sla_endpoints_.empty()) {
    return;  // zero-overhead for graphs without SLA declarations
  }

  std::vector<sla::SlaSource> sources;
  std::vector<sla::SlaNode> nodes;
  sources.reserve(sources_.size() + froms_.size());
  nodes.reserve(maps_.size() + joins_.size() + ops_.size() + statefuls_.size() + spans_.size() +
                froms_.size());
  const auto wcet_of = [this](const std::string& out) {
    const auto it = wcet_by_out_.find(out);
    return it != wcet_by_out_.end() ? it->second : std::chrono::microseconds{0};
  };

  for (const auto& s : sources_) {
    sources.push_back(sla::SlaSource{.channel = s.channel, .interval = s.interval});
  }
  for (const auto& m : maps_) {
    nodes.push_back(sla::SlaNode{.kind = "map",
                                 .in_channels = {m.in_channel},
                                 .out_channel = m.out_channel,
                                 .wcet = wcet_of(m.out_channel)});
  }
  for (const auto& j : joins_) {
    nodes.push_back(sla::SlaNode{.kind = "join",
                                 .in_channels = {j.in_channel_a, j.in_channel_b},
                                 .out_channel = j.out_channel,
                                 .wcet = wcet_of(j.out_channel)});
  }
  for (const auto& b : ops_) {
    nodes.push_back(sla::SlaNode{.kind = "op",
                                 .in_channels = {b.in_channel},
                                 .out_channel = b.out_channel,
                                 .wcet = wcet_of(b.out_channel)});
  }
  // Stateful: the data path carries the latency contract; the state
  // channel is recovery bookkeeping (ADR-0027) and stays out of v0.
  for (const auto& s : statefuls_) {
    nodes.push_back(sla::SlaNode{.kind = "stateful",
                                 .in_channels = {s.in_channel},
                                 .out_channel = s.out_channel,
                                 .wcet = wcet_of(s.out_channel)});
  }
  for (const auto& sp : spans_) {
    nodes.push_back(sla::SlaNode{.kind = "span",
                                 .in_channels = {sp.trig_channel, sp.data_channel},
                                 .out_channel = sp.out_channel,
                                 .wcet = wcet_of(sp.out_channel)});
  }
  for (const auto& f : froms_) {
    if (f.in_channel.empty()) {
      sources.push_back(sla::SlaSource{.channel = f.out_channel, .interval = f.interval});
    } else if (f.in_channel_2.empty()) {
      nodes.push_back(sla::SlaNode{.kind = "from",
                                   .in_channels = {f.in_channel},
                                   .out_channel = f.out_channel,
                                   .wcet = wcet_of(f.out_channel)});
    } else {
      nodes.push_back(sla::SlaNode{.kind = "from",
                                   .in_channels = {f.in_channel, f.in_channel_2},
                                   .out_channel = f.out_channel,
                                   .wcet = wcet_of(f.out_channel)});
    }
  }

  const sla::SlaReport report =
      sla::SlaAnalyzer::analyze(sources, nodes, sla_endpoints_, sla_config_);
  flow.sla_endpoints_ = sla_endpoints_;
  if (!report.ok) {
    throw std::runtime_error("flow '" + name_ + "' rejected by SLA analysis:\n" + report.format());
  }
  if (!report.saturation_warning.empty() || !report.default_wcet_notes.empty()) {
    static_cast<void>(std::fprintf(stderr, "[sla] flow '%s' warnings:\n%s", name_.c_str(),
                                   report.format().c_str()));
  }
  flow.sla_report_ = report;
}

namespace detail {
// Registry probe, defined next to the traceable-flow registry below.
bool is_registered_flow(const std::string& name);
}  // namespace detail

inline Flow FlowBuilder::build() {
  if (!fallback_flow_.empty()) {
    if (fallback_flow_ == name_) {
      throw std::invalid_argument("flow '" + name_ + "' cannot fall back to itself");
    }
    if (!detail::is_registered_flow(fallback_flow_)) {
      throw std::invalid_argument("flow '" + name_ + "' declares unknown fallback '" +
                                  fallback_flow_ + "'");
    }
  }
  Flow flow(name_);
  run_sla_analysis(flow);            // reads the builder decls: must precede the moves
  flow.wcet_by_out_ = wcet_by_out_;  // compiler IR input regardless of SLA presence
  flow.fallback_flow_ = fallback_flow_;
  flow.sources_ = std::move(sources_);
  flow.maps_ = std::move(maps_);
  flow.joins_ = std::move(joins_);
  flow.ops_ = std::move(ops_);
  flow.statefuls_ = std::move(statefuls_);
  flow.spans_ = std::move(spans_);
  flow.froms_ = std::move(froms_);
  flow.sinks_ = std::move(sinks_);
  return flow;
}

// ---------------------------------------------------------------------------
// Traceable-flow registry (ADR-0030 M-D): flows register by name at static
// init; launchers build them by name — the build IS the dry-run trace.
// ---------------------------------------------------------------------------

namespace detail {

// Lock-free intrusive registration list: nodes are POD statics the macro
// drops at static init (pointer compare-and-swap, nothing allocates or
// throws).
struct FlowRegistrationNode;

inline std::atomic<FlowRegistrationNode*>& flow_registration_head() {
  static std::atomic<FlowRegistrationNode*> head{nullptr};
  return head;
}

struct FlowRegistrationNode {
  const char* name;
  void (*declare)(FlowBuilder&);
  std::atomic<FlowRegistrationNode*> next{nullptr};

  FlowRegistrationNode(const char* n, void (*d)(FlowBuilder&)) noexcept : name(n), declare(d) {
    FlowRegistrationNode* current = flow_registration_head().load(std::memory_order_relaxed);
    do {
      next.store(current, std::memory_order_relaxed);
    } while (!flow_registration_head().compare_exchange_weak(
        current, this, std::memory_order_release, std::memory_order_relaxed));
  }
};

}  // namespace detail

[[nodiscard]] inline std::vector<std::string> registered_flow_names() {
  std::vector<std::string> names;
  for (auto* node = detail::flow_registration_head().load(std::memory_order_acquire);
       node != nullptr; node = node->next.load(std::memory_order_acquire)) {
    names.emplace_back(node->name);
  }
  return names;
}
namespace detail {
inline bool is_registered_flow(const std::string& name) {
  for (auto* node = flow_registration_head().load(std::memory_order_acquire); node != nullptr;
       node = node->next.load(std::memory_order_acquire)) {
    if (name == node->name) {
      return true;
    }
  }
  return false;
}
}  // namespace detail

// Dry-run by name: builds (traces) the registered flow — SLA analysis runs
// at build, the declaration graph is the traced result. Throws
// std::invalid_argument for an unknown name, rethrows the declare
// function's exceptions as-is.
[[nodiscard]] inline Flow build_registered_flow(const std::string& name) {
  for (auto* node = detail::flow_registration_head().load(std::memory_order_acquire);
       node != nullptr; node = node->next.load(std::memory_order_acquire)) {
    if (name == node->name) {
      FlowBuilder builder(node->name);
      node->declare(builder);
      return builder.build();
    }
  }
  throw std::invalid_argument("no flow registered under '" + name + "'");
}

// The macro drops a POD node (const char* + declare fn, lock-free
// intrusive list) at static init; nothing allocates or throws.
#define REGISTER_TRACEABLE_FLOW(name, fn)                                                      \
  namespace tianshu_flow_registration_##fn {                                                   \
    static const ::tianshu::dsl::detail::FlowRegistrationNode registration_node{(name), (fn)}; \
  }  // namespace tianshu_flow_registration_##fn

template <typename TIn, typename TOut, typename F>
Stream<TOut> FlowBuilder::map_stream(const Stream<TIn>& in, F fn) {
  const std::string out = anon_channel();
  Flow::MapDecl decl{in.channel(),
                     out,
                     in.type_name(),
                     std::string(core::MessageTraits<TOut>::name()),
                     detail::make_map_wire<TIn, TOut>(in.channel(), out, fn),
                     detail::make_map_specialize<TIn, TOut>(in.channel(), out, std::move(fn))};
  maps_.push_back(std::move(decl));
  return Stream<TOut>(out, std::string(core::MessageTraits<TOut>::name()));
}

template <typename TIn, typename TOut, typename F>
Stream<TOut> FlowBuilder::map_stream_to(const Stream<TIn>& in, std::string_view out_name, F fn) {
  const std::string out = channel_for(out_name);
  Flow::MapDecl decl{in.channel(),
                     out,
                     in.type_name(),
                     std::string(core::MessageTraits<TOut>::name()),
                     detail::make_map_wire<TIn, TOut>(in.channel(), out, fn),
                     detail::make_map_specialize<TIn, TOut>(in.channel(), out, std::move(fn))};
  maps_.push_back(std::move(decl));
  return Stream<TOut>(out, std::string(core::MessageTraits<TOut>::name()));
}

template <typename T, typename F>
void FlowBuilder::sink_stream(const Stream<T>& in, F fn) {
  Flow::SinkDecl decl{in.channel(), in.type_name(), detail::make_sink_wire<T>(in.channel(), fn),
                      detail::make_sink_specialize<T>(in.channel(), std::move(fn))};
  sinks_.push_back(std::move(decl));
}

template <typename TIn, typename TOut, typename TOp>
FlowChain<TOut> FlowBuilder::op(const FlowChain<TIn>& in, std::string_view out_name, TOp impl) {
  const std::string out = channel_for(out_name);
  Flow::OpDecl decl{
      in.stream_.channel(), out, in.stream_.type_name(),
      std::string(core::MessageTraits<TOut>::name()),
      detail::make_op_wire<TIn, TOut, TOp>(in.stream_.channel(), out, std::move(impl))};
  ops_.push_back(std::move(decl));
  return FlowChain<TOut>(this, Stream<TOut>(out, std::string(core::MessageTraits<TOut>::name())));
}

template <typename TA, typename TB, typename TC, typename F>
FlowChain<TC> FlowBuilder::join(const FlowChain<TA>& a, const FlowChain<TB>& b, F fn) {
  const std::string out = anon_channel();
  Flow::JoinDecl decl{
      a.stream_.channel(),
      b.stream_.channel(),
      out,
      a.stream_.type_name(),
      b.stream_.type_name(),
      std::string(core::MessageTraits<TC>::name()),
      detail::make_join_wire<TA, TB, TC>(a.stream_.channel(), b.stream_.channel(), out, fn),
      detail::make_join_specialize<TA, TB, TC>(a.stream_.channel(), b.stream_.channel(), out,
                                               std::move(fn))};
  joins_.push_back(std::move(decl));
  return FlowChain<TC>(this, Stream<TC>(out, std::string(core::MessageTraits<TC>::name())));
}

}  // namespace tianshu::dsl
