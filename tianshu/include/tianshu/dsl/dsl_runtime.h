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

// DSL runtime v0.5: interprets a Flow declaration on the L4 stack
// (per ADR-0021). One consumer of the declaration; the L1 compiler
// replaces it later without touching the Flow API.
//
// Execution model:
//   - Sources drive the DataDispatcher DIRECTLY (no transport); one
//     publish cascades the whole chain synchronously on the source
//     thread (ADR-0021 amendment)
//   - Each map is a DataVisitor on its input channel; each join is a
//     two-input DataVisitor with AllLatest fusion (fires when both
//     inputs are non-empty, consumes one of each)
//   - Lineage travels through per-consumer lineage queues (ADR-0022
//     amendment): publish fans a copy out to every stage registered on
//     the channel, so multiple consumers never steal from each other

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "tianshu/base/cache_buffer.h"
#include "tianshu/base/small_vector.h"
#include "tianshu/base/spin_lock.h"
#include "tianshu/core/component.h"
#include "tianshu/core/data_dispatcher.h"
#include "tianshu/core/data_visitor.h"
#include "tianshu/core/lineage.h"
#include "tianshu/core/node.h"
#include "tianshu/dsl/flow.h"
#include "tianshu/dsl/record.h"
#include "tianshu/dsl/record_v2.h"
#include "tianshu/sla/sla_stats.h"
#include "tianshu/transport/transport_backend.h"

namespace tianshu::dsl {

class SpecializeBuilder;

// Op publish handle (ADR-0024): bound to one output channel of the op.
// Valid ONLY inside on_init/handle invocations; publish derives lineage
// from the current input when called from handle (map semantics) and
// roots at the box channel when called from on_init (source semantics).
template <typename T>
class OpPub {
 public:
  void publish(const T& msg);

 private:
  friend class FlowRuntime;

  OpPub(FlowRuntime* rt, std::string channel) : rt_(rt), channel_(std::move(channel)) {}

  FlowRuntime* rt_;
  std::string channel_;
  core::Lineage parent_;
};

namespace detail {

class StageHolder {
 public:
  virtual ~StageHolder() = default;
  // The dispatcher owner token this stage registered under, so
  // ~FlowRuntime can deregister before the buffers die (the
  // dispatcher is a process-wide singleton; a runtime that never
  // removes its sinks leaves dangling pointers that a later runtime
  // publishing on the same channel id would call into).
  [[nodiscard]] virtual const void* dispatcher_owner() const = 0;
};

template <typename... Ts>
class VisitorStage final : public StageHolder {
 public:
  explicit VisitorStage(std::unique_ptr<core::DataVisitor<Ts...>> v) : visitor(std::move(v)) {}

  [[nodiscard]] const void* dispatcher_owner() const override { return visitor.get(); }

  std::unique_ptr<core::DataVisitor<Ts...>> visitor;
};

// One retained entry of a channel's bounded history (ADR-0026/0027):
// the bytes, the channel-local seq, and the message's lineage.
// Payload bytes live in an inline-capacity buffer (ADR-0030 D8 L2c):
// the hot publish path used to heap-allocate a fresh vector per
// message just to retain the copy.
struct HistoryEntry {
  std::uint64_t seq{0};
  base::SmallVec<std::uint8_t, 32> bytes;
  core::Lineage lineage;
};

// Bounded per-channel history ring: slice queries and state recovery
// read from here; publish_bytes captures every message. Pushes and
// traversal are internally locked: a span visitor iterates entries on
// its trigger thread while the data channel's source thread pushes
// concurrently — an unlocked deque traversal was a map-realloc
// use-after-free (caught by asan on CI).
class HistoryRing {
 public:
  explicit HistoryRing(std::size_t depth) : depth_(depth) {}

  void push(std::uint64_t seq, const void* data, std::size_t size, const core::Lineage& lin) {
    const auto* b = static_cast<const std::uint8_t*>(data);
    base::SmallVec<std::uint8_t, 32> bytes;
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    for (std::size_t i = 0; i < size; ++i) {
      bytes.push_back(b[i]);
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const std::scoped_lock lock(entries_mutex_);
    entries_.push_back(HistoryEntry{.seq = seq, .bytes = std::move(bytes), .lineage = lin});
    if (entries_.size() > depth_) {
      entries_.pop_front();
    }
  }

  void push(std::uint64_t seq, const void* data, std::size_t size, core::Lineage&& lin) {
    const auto* b = static_cast<const std::uint8_t*>(data);
    base::SmallVec<std::uint8_t, 32> bytes;
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    for (std::size_t i = 0; i < size; ++i) {
      bytes.push_back(b[i]);
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const std::scoped_lock lock(entries_mutex_);
    entries_.push_back(
        HistoryEntry{.seq = seq, .bytes = std::move(bytes), .lineage = std::move(lin)});
    if (entries_.size() > depth_) {
      entries_.pop_front();
    }
  }

  // Lock-held traversal for cross-thread readers (span joins, dumps);
  // the raw entries() accessor stays for single-threaded tests.
  template <typename Fn>
  void for_each_entry(Fn&& fn) const {
    const std::scoped_lock lock(entries_mutex_);
    for (const auto& entry : entries_) {
      fn(entry);
    }
  }

  [[nodiscard]] const std::deque<HistoryEntry>& entries() const { return entries_; }

 private:
  std::size_t depth_;
  mutable std::mutex entries_mutex_;
  std::deque<HistoryEntry> entries_;
};

// Polymorphic lineage delivery sink (ADR-0032): one per (stage, input
// channel). Generic stages register a locked LineageQueue; specialized
// single-producer channels register a LineageInbox — the v0 cascade
// consumes synchronously on the publishing thread (dispatch fires the
// consumer notify inside the publish call), so the inbox trades the
// mutex + node-allocating deque for plain ring stores. Multi-producer
// channels (feedback edges, op/stateful/span/from outputs) keep the
// locked queue because their writers may run on distinct threads.
class LineageChannel {
 public:
  virtual ~LineageChannel() = default;
  virtual void push(const core::Lineage& lineage) = 0;
  virtual void push(core::Lineage&& lineage) = 0;
  virtual core::Lineage pop() = 0;
};

// Bounded lineage queue: the generic-path LineageChannel.
class LineageQueue final : public LineageChannel {
 public:
  explicit LineageQueue(std::size_t depth) : depth_(depth) {}

  void push(const core::Lineage& lineage) override {
    const std::scoped_lock lock(mutex_);
    queue_.push_back(lineage);
    if (queue_.size() > depth_) {
      queue_.pop_front();
    }
  }

  void push(core::Lineage&& lineage) override {
    const std::scoped_lock lock(mutex_);
    queue_.push_back(std::move(lineage));
    if (queue_.size() > depth_) {
      queue_.pop_front();
    }
  }

  core::Lineage pop() override {
    const std::scoped_lock lock(mutex_);
    if (queue_.empty()) {
      return {};
    }
    core::Lineage lineage = std::move(queue_.front());
    queue_.pop_front();
    return lineage;
  }

 private:
  std::size_t depth_;
  mutable std::mutex mutex_;
  std::deque<core::Lineage> queue_;
};

// Single-writer bounded lineage ring (ADR-0032). Producer and consumer
// run on the publishing thread by the v0 synchronous-cascade
// discipline; head/tail atomics give the acquire/release pairing for
// the cross-thread join case (a join's fused drain may run on either
// input's producer thread; the join stage's single-flight guard
// serializes drains, so there is at most one logical consumer).
class LineageInbox final : public LineageChannel {
 public:
  explicit LineageInbox(std::size_t depth) : capacity_(depth == 0 ? 1 : depth), ring_(capacity_) {}

  void push(const core::Lineage& lineage) override {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t head = head_.load(std::memory_order_acquire);
    if (tail - head == capacity_) {
      // Depth-bounded like LineageQueue: drop the oldest entry.
      head_.store(head + 1, std::memory_order_release);
    }
    ring_[tail % capacity_] = lineage;
    tail_.store(tail + 1, std::memory_order_release);
  }

  void push(core::Lineage&& lineage) override {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t head = head_.load(std::memory_order_acquire);
    if (tail - head == capacity_) {
      head_.store(head + 1, std::memory_order_release);
    }
    ring_[tail % capacity_] = std::move(lineage);
    tail_.store(tail + 1, std::memory_order_release);
  }

  core::Lineage pop() override {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t tail = tail_.load(std::memory_order_acquire);
    if (head == tail) {
      return {};
    }
    core::Lineage lineage = std::move(ring_[head % capacity_]);
    head_.store(head + 1, std::memory_order_release);
    return lineage;
  }

 private:
  std::size_t capacity_;
  std::vector<core::Lineage> ring_;
  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};
};

}  // namespace detail

class FlowRuntime {
 private:
  struct PublishCtx;

 public:
  FlowRuntime() = default;
  ~FlowRuntime();

  FlowRuntime(const FlowRuntime&) = delete;
  FlowRuntime& operator=(const FlowRuntime&) = delete;

  template <typename U>
  friend class OpPub;

  friend class SpecializeBuilder;

  // Copies `lineage` to every consumer lineage_queue on `channel`, captures
  // the message into the channel's bounded history, then cascades the
  // payload through the DataDispatcher (synchronous chain). The rvalue
  // overload moves the lineage into the last destination, saving one
  // deep copy per publish on the hot cascade path (ADR-0030 D8 L2).
  void publish_bytes(const std::string& channel, const void* data, std::size_t size,
                     const core::Lineage& lineage);
  void publish_bytes(const std::string& channel, const void* data, std::size_t size,
                     core::Lineage&& lineage);

  // Bounded history of a published channel (nullptr when never
  // published): (seq, bytes, lineage) entries, oldest first. Recovery
  // and slice queries read from here (ADR-0026/0027).
  [[nodiscard]] const detail::HistoryRing* history(const std::string& channel) const;

  // SLA runtime defense (ADR-0029 D6): per-endpoint e2e histograms and
  // miss counters for flows that declared SLA endpoints. Empty for
  // flows without declarations (recording never arms).
  [[nodiscard]] std::vector<sla::SlaEndpointStats> sla_snapshot() const;

  // Degradation ladder (ADR-0031 v0): populated when the flow declared a
  // fallback. The watcher samples the SLA miss counters once per window;
  // each window whose misses grew by >= kFallbackWindowMisses on any
  // endpoint fires one degradation event (counter + last-offender
  // tracking). v0 signals; hot-swapping to the fallback flow is v1.
  struct FallbackState {
    std::string declared;
    std::uint64_t events{0};
    std::string last_endpoint;
    std::uint64_t last_miss_count{0};
  };
  [[nodiscard]] FallbackState fallback_state() const;

  // Record substrate (ADR-0026 Phase C): dump every captured channel
  // history into an append-only record file (messages in capture
  // order, oldest first per channel). Legacy v0 format.
  [[nodiscard]] bool record_to(const std::string& path) const;

  // LIVE recording (ADR-0028 v2): hooks into publish_bytes so every
  // message is captured in-flight with its timestamp, lineage, and
  // per-channel schema (auto-registered on first publish). Call
  // stop_recording() to flush chunks, index, stats, and footer.
  void start_recording(const std::string& path,
                       record::Compression compression = record::Compression::kLz4);
  bool stop_recording();
  [[nodiscard]] bool is_recording() const;

  // Replay: re-publish every recorded message through this runtime in
  // record order — the live cascade rebuilds, so downstream outputs
  // and lineage reproduce exactly (the offline substrate of the same
  // slice-query API).
  void replay_from(const std::vector<RecordedMessage>& records);

  // Map stage wiring (called by Flow::MapDecl::wire).
  template <typename TIn, typename TOut>
  void attach_map(const std::string& in_channel, const std::string& out_channel,
                  std::function<TOut(const TIn&)> fn) {
    const auto lineage_queue = register_lineage_queue(in_channel);
    const auto box = std::make_shared<core::DataVisitor<TIn>*>(nullptr);
    auto visitor = std::make_unique<core::DataVisitor<TIn>>(
        in_channel, kQueueDepth, [this, box, lineage_queue, out_channel, fn = std::move(fn)] {
          auto* visitor_ptr = *box;
          if (visitor_ptr == nullptr) {
            return;
          }
          while (TIn* msg = visitor_ptr->try_fetch_0()) {
            core::Lineage parent = lineage_queue->pop();
            TOut out = fn(*msg);
            publish_derived(std::move(parent), out_channel, &out, sizeof(TOut));
          }
        });
    *box = visitor.get();
    stages_.push_back(std::make_unique<detail::VisitorStage<TIn>>(std::move(visitor)));
  }

  // Join stage wiring (called by Flow::JoinDecl::wire): AllLatest fusion
  // over both inputs; the output lineage merges both parents' branches.
  template <typename TA, typename TB, typename TC>
  void attach_join(const std::string& in_a, const std::string& in_b, const std::string& out_channel,
                   std::function<TC(const TA&, const TB&)> fn) {
    const auto lineage_queue_a = register_lineage_queue(in_a);
    const auto lineage_queue_b = register_lineage_queue(in_b);
    const auto box = std::make_shared<core::DataVisitor<TA, TB>*>(nullptr);
    auto visitor = std::make_unique<core::DataVisitor<TA, TB>>(
        in_a, in_b, kQueueDepth,
        [this, box, lineage_queue_a, lineage_queue_b, out_channel, fn = std::move(fn)] {
          auto* visitor_ptr = *box;
          if (visitor_ptr == nullptr) {
            return;
          }
          TA* a = visitor_ptr->try_fetch_0();
          TB* b = visitor_ptr->try_fetch_1();
          if (a == nullptr || b == nullptr) {
            return;
          }
          core::Lineage merged = lineage_queue_a->pop();
          merged.merge(lineage_queue_b->pop());
          TC out = fn(*a, *b);
          publish_derived(merged, out_channel, &out, sizeof(TC));
        });
    *box = visitor.get();
    stages_.push_back(std::make_unique<detail::VisitorStage<TA, TB>>(std::move(visitor)));
  }

  // Op wiring (called by Flow::OpDecl::wire): visitor on the input
  // channel + deferred on_init (runs after ALL wiring so consumers of the
  // output channel are registered before the bootstrap publication).
  template <typename TIn, typename TOut, typename TOp>
  void attach_op(const std::string& in_channel, const std::string& out_channel, TOp impl) {
    const std::shared_ptr<OpPub<TOut>> pub(new OpPub<TOut>(this, out_channel));
    const auto lineage_queue = register_lineage_queue(in_channel);
    const auto op_impl = std::make_shared<TOp>(std::move(impl));
    const auto stage = std::make_shared<core::DataVisitor<TIn>*>(nullptr);
    auto visitor = std::make_unique<core::DataVisitor<TIn>>(
        in_channel, kQueueDepth, [stage, op_impl, pub, lineage_queue] {
          auto* visitor_ptr = *stage;
          if (visitor_ptr == nullptr) {
            return;
          }
          while (TIn* msg = visitor_ptr->try_fetch_0()) {
            pub->parent_ = lineage_queue->pop();
            op_impl->handle(*msg, *pub);
            pub->parent_ = {};
          }
        });
    *stage = visitor.get();
    stages_.push_back(std::make_unique<detail::VisitorStage<TIn>>(std::move(visitor)));
    init_hooks_.push_back([op_impl, pub] { op_impl->on_init(*pub); });
  }

  // Referenced-component wiring (ADR-0025, called by Flow::FromDecl::wire).
  // Source-like: timer-driven publisher; the component keeps its own
  // thread (L4 behavior). Component-like: proc runs on the publisher's
  // dispatch thread via its DataVisitor. Both pump outputs back through
  // an intra reader -> publish_bytes with rooted lineage.
  void attach_referenced_source(const std::string& registry_name, const std::string& out_channel,
                                std::chrono::milliseconds interval);
  void attach_referenced_component(const std::string& registry_name, const std::string& in_channel,
                                   const std::string& out_channel);
  void attach_referenced_component2(const std::string& registry_name, const std::string& in0,
                                    const std::string& in1, const std::string& out_channel);

  // Span wiring (called by Flow::SpanDecl::wire, ADR-0026): visitor on
  // the TRIGGER channel; on each trigger, materialize the data channel's
  // history slice where time(msg) in [t0,t1] and publish with merged
  // lineage (trigger branch + data RANGE branch).
  template <typename TTrig, typename TData, typename TOut, typename TSpanFn, typename TTimeFn,
            typename TImpl>
  void attach_span(const std::string& trig_channel, const std::string& data_channel,
                   const std::string& out_channel, TSpanFn span_fn, TTimeFn time_fn, TImpl impl) {
    const auto lineage_queue = register_lineage_queue(trig_channel);
    const auto op_impl = std::make_shared<TImpl>(std::move(impl));
    const auto span = std::make_shared<TSpanFn>(std::move(span_fn));
    const auto time_of = std::make_shared<TTimeFn>(std::move(time_fn));
    const auto stage = std::make_shared<core::DataVisitor<TTrig>*>(nullptr);
    auto visitor = std::make_unique<core::DataVisitor<TTrig>>(
        trig_channel, kQueueDepth,
        [this, stage, lineage_queue, op_impl, span, time_of, data_channel, out_channel] {
          auto* visitor_ptr = *stage;
          if (visitor_ptr == nullptr) {
            return;
          }
          while (TTrig* trig = visitor_ptr->try_fetch_0()) {
            core::Lineage parent = lineage_queue->pop();
            const auto range = (*span)(*trig);

            Slice<TData> slice;
            slice.seq_lo = 1;  // empty marker: lo > hi
            const auto* hist = history(data_channel);
            if (hist != nullptr) {
              hist->for_each_entry([&](const detail::HistoryEntry& entry) {
                TData msg{};
                if (entry.bytes.size() != sizeof(TData)) {
                  return;
                }
                std::memcpy(&msg, entry.bytes.data(), sizeof(msg));
                const std::uint64_t t = (*time_of)(msg);
                if (t < range.first || t > range.second) {
                  return;
                }
                if (slice.items.empty()) {
                  slice.seq_lo = entry.seq;
                }
                slice.seq_hi = entry.seq;
                slice.items.push_back(msg);
              });
              const auto& entries = hist->entries();
              if (!entries.empty() && entries.front().seq > 0) {
                TData front_msg{};
                std::memcpy(&front_msg, entries.front().bytes.data(), sizeof(front_msg));
                slice.truncated = (*time_of)(front_msg) > range.first;
              }
            }

            TOut out = (*op_impl)(*trig, slice);
            // Span hops merge the data-range branch into the trigger's
            // lineage; the queue-popped parent itself is not mutated
            // beyond the merge, so it stays const here.
            core::Lineage lin = std::move(parent);
            if (!slice.empty()) {
              lin.merge(core::Lineage::rooted_range(data_channel, slice.seq_lo, slice.seq_hi));
            }
            publish_derived(std::move(lin), out_channel, &out, sizeof(TOut));
          }
        });
    *stage = visitor.get();
    stages_.push_back(std::make_unique<detail::VisitorStage<TTrig>>(std::move(visitor)));
  }

  // Stateful wiring (called by Flow::StatefulDecl::wire, ADR-0027):
  // one input visitor, TWO publish handles (output + state); both share
  // the input lineage as parent, so a state version's provenance points
  // at exactly the input that produced it — the recovery protocol reads
  // that to find the absorption point.
  template <typename TIn, typename TOut, typename TState, typename TImpl>
  void attach_stateful(const std::string& in_channel, const std::string& out_channel,
                       const std::string& state_channel, TImpl impl) {
    const std::shared_ptr<OpPub<TOut>> out_pub(new OpPub<TOut>(this, out_channel));
    const std::shared_ptr<OpPub<TState>> state_pub(new OpPub<TState>(this, state_channel));
    const auto lineage_queue = register_lineage_queue(in_channel);
    const auto op_impl = std::make_shared<TImpl>(std::move(impl));
    const auto stage = std::make_shared<core::DataVisitor<TIn>*>(nullptr);
    auto visitor = std::make_unique<core::DataVisitor<TIn>>(
        in_channel, kQueueDepth, [stage, op_impl, out_pub, state_pub, lineage_queue] {
          auto* visitor_ptr = *stage;
          if (visitor_ptr == nullptr) {
            return;
          }
          while (TIn* msg = visitor_ptr->try_fetch_0()) {
            core::Lineage parent = lineage_queue->pop();
            out_pub->parent_ = parent;
            state_pub->parent_ = parent;
            op_impl->handle(*msg, *out_pub, *state_pub);
            out_pub->parent_ = {};
            state_pub->parent_ = {};
          }
        });
    *stage = visitor.get();
    stages_.push_back(std::make_unique<detail::VisitorStage<TIn>>(std::move(visitor)));
    init_hooks_.emplace_back(
        [op_impl, out_pub, state_pub] { op_impl->on_init(*out_pub, *state_pub); });
  }

  // Sink wiring (called by Flow::SinkDecl::wire).
  template <typename T>
  void attach_sink(const std::string& channel,
                   std::function<void(const T&, const core::Lineage&)> fn) {
    const auto lineage_queue = register_lineage_queue(channel);
    const auto box = std::make_shared<core::DataVisitor<T>*>(nullptr);
    auto visitor = std::make_unique<core::DataVisitor<T>>(
        channel, kQueueDepth, [box, lineage_queue, fn = std::move(fn)] {
          auto* visitor_ptr = *box;
          if (visitor_ptr == nullptr) {
            return;
          }
          while (T* msg = visitor_ptr->try_fetch_0()) {
            fn(*msg, lineage_queue->pop());
          }
        });
    *box = visitor.get();
    stages_.push_back(std::make_unique<detail::VisitorStage<T>>(std::move(visitor)));
  }

  // Interprets the flow: wires maps/joins/sinks, then drives every source
  // on its interval (absolute-deadline pacing, no cumulative drift) until
  // `duration` elapses. Each source runs on its own thread.
  void run_for(const Flow& flow, std::chrono::milliseconds duration);

  // Wiring half of run_for: installs every stage's closures and arms the
  // SLA collector. Public so a compiled artifact (ADR-0030) can install
  // its own specialized wiring, then drive the same run loop.
  void wire(const Flow& flow);

  // Specialized wiring (ADR-0032): installs per-message fast stages for
  // closed single-producer map/join channels and generic stages for
  // everything else. Byte-equivalent outputs and lineage vs wire()
  // (H1), with the generic per-hop machinery (seq mutex, queue
  // deque allocs, context hash) eliminated on the fast path.
  void wire_specialized(const Flow& flow);

  // Begin a specialized install driven by the caller (the compiled
  // artifact's install function): returns the builder that computes
  // the per-channel plan from the flow and constructs the stages.
  SpecializeBuilder begin_specialize(const Flow& flow);

  // Run half: fires bootstrap hooks (ADR-0024), drives sources for
  // `duration`, quiesces referenced timer components. Call after some
  // form of wiring (wire() / wire_specialized() / a compiled install).
  void run_sources(const Flow& flow, std::chrono::milliseconds duration);

  // Live-recorder state for the fast-path fallback check (ADR-0032):
  // while armed, specialized stages route their fan-out through the
  // generic publish path so record files stay byte-identical to an
  // interpreted run.
  [[nodiscard]] bool recording_active() const {
    return recording_active_.load(std::memory_order_relaxed);
  }

 private:
  // Creates a lineage delivery slot owned by the runtime and registers
  // it for `channel` so publish_bytes fans lineage copies to it. The
  // specialized install path uses this instead of the shared_ptr
  // register_lineage_queue so inbox/queue choice follows the plan.
  detail::LineageChannel* register_specialized_slot(const std::string& channel, bool inbox);

  // Creates a lineage_queue owned by the runtime and registers it for
  // `channel` so publish_bytes fans lineage copies to it.
  std::shared_ptr<detail::LineageQueue> register_lineage_queue(const std::string& channel);

  // Publishes with a lineage derived from `parent` (parent chain + this
  // channel's hop with a fresh per-channel seq). Takes the parent BY
  // VALUE: callers move the queue-popped lineage in, add_hop appends in
  // place, and the terminal publish moves again — no full copy per hop
  // (ADR-0030 D8; chains were O(hops^2) per message otherwise).
  void publish_derived(core::Lineage parent, const std::string& channel, const void* data,
                       std::size_t size);

  // Snapshot resolution for publish_impl (D8 L1c): steady state is
  // lock-free; a first publish builds the entry under the mutex.
  const PublishCtx& resolve_publish_ctx(const std::string& channel);

  // Shared body of the two publish_bytes overloads; exactly one of
  // lineage_copy / lineage_move is non-null (ADR-0030 D8 L2).
  void publish_impl(const std::string& channel, const void* data, std::size_t size,
                    const core::Lineage* lineage_copy, core::Lineage* lineage_move);

  // Op publication: `parent` empty (on_init) roots at the channel;
  // otherwise derives from it (handle).
  void publish_op(const std::string& channel, const void* data, std::size_t size,
                  const core::Lineage& parent);

  // Pumps a referenced component's transport output back into the DSL
  // channel world with rooted lineage (ADR-0025 Q3).
  void attach_bridge_reader(const std::string& out_channel);

  [[nodiscard]] std::uint64_t next_seq(const std::string& channel);

  static constexpr std::size_t kQueueDepth = 16;
  static constexpr std::size_t kHistoryDepth = 64;

  std::vector<std::unique_ptr<detail::StageHolder>> stages_;
  std::vector<std::shared_ptr<detail::LineageQueue>> lineage_queues_;
  // Slots created by the specialized install path (ADR-0032): inboxes
  // for single-producer channels, queues otherwise. Declared after
  // stages_ so the stages (which read their slot during notifies) are
  // destroyed first.
  std::vector<std::unique_ptr<detail::LineageChannel>> owned_slots_;
  std::vector<std::function<void()>> init_hooks_;

  // Referenced-component plumbing (ADR-0025). Declaration order matters:
  // components must be destroyed before the node and bridge readers.
  std::unique_ptr<core::Node> bridge_node_;
  std::vector<std::unique_ptr<transport::ReaderBase>> bridge_readers_;
  std::vector<std::shared_ptr<core::ComponentBase>> components_;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::vector<detail::LineageChannel*>> channel_queues_;
  std::unordered_map<std::string, std::uint64_t> seq_counters_;
  std::unordered_map<std::string, detail::HistoryRing> histories_;

  // History-capture narrowing (ADR-0032): a specialized install fills
  // this with the channels that have graph-declared observers (span
  // data channels, stateful state channels); resolve_publish_ctx turns
  // it into PublishCtx::history_on. Empty (generic wire()) = capture
  // everything, preserving the interpreted reference behavior.
  std::unordered_set<std::string> history_observers_;

  // Publish-snapshot epoch (ADR-0032): bumped at every pub_ctx_
  // replacement; the thread-local pointer-identity cache in
  // resolve_publish_ctx validates against it to stay coherent across
  // wiring rounds and recorder arming.
  std::atomic<std::uint64_t> ctx_epoch_{0};

  // Recorder-armed flag for the fast-path fallback (see
  // recording_active()); relaxed loads on the hot path, benign
  // in-flight-capture semantics while arming (same as generic).
  std::atomic<bool> recording_active_{false};

  // Single-lookup publish context per channel (ADR-0030 D8 L1):
  // resolved lazily on first publish, cleared at the top of wire()
  // because new wiring can add consumers to existing channels.
  struct PublishCtx {
    core::ChannelId id{0};
    std::vector<detail::LineageChannel*> queues;
    detail::HistoryRing* history{nullptr};
    std::uint16_t rec_ch{0};
    bool recorded{false};
    bool resolved{false};
    // History capture gate (ADR-0032): false only on specialized runs
    // for channels with no graph-declared observer; generic wiring
    // leaves it true unconditionally.
    bool history_on{true};
    // Feedback channels (map_to write-back) have two concurrent writers:
    // the seed source thread and the loop-carrying cascade thread. The
    // per-channel push section runs under this lock (ADR-0030 D8 L1b);
    // single-writer channels pay one uncontended acquire.
    mutable base::SpinLock push_lock;
  };
  // Copy-on-write snapshot: wiring/first-publish rebuild the map and
  // atomically swap it in; publishes load the snapshot (refcount) with
  // no lock — the runtime mutex leaves the steady-state path (D8 L1c).
  // Entries are shared_ptr so copies keep each channel's SpinLock
  // identity stable across snapshots.
  std::atomic<std::shared_ptr<const std::unordered_map<std::string, std::shared_ptr<PublishCtx>>>>
      pub_ctx_{
          std::make_shared<const std::unordered_map<std::string, std::shared_ptr<PublishCtx>>>()};

  // Arms on the first run_for of a flow with SLA endpoints (ADR-0029 D6).
  std::unique_ptr<sla::SlaStatsCollector> sla_stats_;

  // Degradation watcher (ADR-0031): guarded by fallback_mutex_; the
  // counters are read by fallback_state() from other threads.
  mutable std::mutex fallback_mutex_;
  FallbackState fallback_{.declared = "", .events = 0, .last_endpoint = "", .last_miss_count = 0};

  // Recorder appends moved off the runtime mutex (ADR-0030 D8 L1b):
  // RecordWriter is not internally synchronized.
  std::mutex recorder_mutex_;

  // Live recording (ADR-0028 v2): writer hooks into publish_bytes.
  std::unique_ptr<record::RecordWriter> recorder_;
  std::unordered_map<std::string, std::uint16_t> recorder_channel_ids_;
  std::uint64_t recording_start_ts_{0};
};

// Defined after FlowRuntime completes: publish reaches into the runtime
// (two-phase lookup — same pattern as flow.h's detail::make_* helpers).
template <typename T>
void OpPub<T>::publish(const T& msg) {
  rt_->publish_op(channel_, &msg, sizeof(T), parent_);
}

namespace detail {

// Fan-out targets of a fast producer, resolved by the builder's
// finalize() once every consumer slot is registered (ADR-0032).
struct FanBinding {
  core::ChannelId id{0};
  HistoryRing* history{nullptr};  // null when capture is narrowed off
  std::vector<LineageChannel*> slots;
};

class FastStageBase {
 public:
  virtual ~FastStageBase() = default;
  virtual void bind_fan(FanBinding binding) = 0;
  [[nodiscard]] virtual const std::string& out_channel() const = 0;
};

// Specialized map stage (ADR-0032): the same DataVisitor + dispatcher
// registration as attach_map, but the per-hop cascade is a straight
// line — lineage popped from the input slot, the operator invoked
// (through the raw pointer when the fn was captureless), one hop
// appended with the stage's own single-writer seq counter, and the
// fan-out performed with install-time constants. While the recorder is
// armed the fan routes through publish_bytes so record files stay
// byte-identical to an interpreted run.
template <typename TIn, typename TOut>
class FastMapStage final : public StageHolder, public FastStageBase {
 public:
  using RawFn = TOut (*)(const TIn&);

  FastMapStage(FlowRuntime& rt, const std::string& in_channel, std::string out_channel,
               std::function<TOut(const TIn&)> fn, LineageChannel* slot, std::size_t depth)
      : rt_(rt), out_channel_(std::move(out_channel)), slot_(slot) {
    fn_ = std::move(fn);
    const RawFn* raw = fn_.template target<RawFn>();
    raw_fn_ = raw != nullptr ? *raw : nullptr;
    buffer_ = std::make_unique<base::CacheBuffer<TIn>>(depth);
    core::DataDispatcher::instance().add_buffer(
        core::channel_id_for(in_channel), buffer_.get(),
        [this] {
          while (TIn* msg = buffer_->try_fetch()) {
            core::Lineage parent = slot_->pop();
            TOut out = raw_fn_ != nullptr ? raw_fn_(*msg) : fn_(*msg);
            fan(std::move(parent), &out, sizeof(TOut));
          }
        },
        this);
  }

  void bind_fan(FanBinding binding) override {
    out_id_ = binding.id;
    history_ = binding.history;
    slots_ = std::move(binding.slots);
  }

  [[nodiscard]] const std::string& out_channel() const override { return out_channel_; }
  [[nodiscard]] const void* dispatcher_owner() const override { return this; }

 private:
  void fan(core::Lineage parent, const void* data, std::size_t size) {
    const std::uint64_t seq = seq_++;
    parent.add_hop(core::LineageHop{.channel = out_channel_, .seq = seq});
    if (rt_.recording_active()) {
      rt_.publish_bytes(out_channel_, data, size, std::move(parent));
      return;
    }
    if (history_ != nullptr) {
      history_->push(seq, data, size, parent);
    }
    for (std::size_t i = 0; i + 1 < slots_.size(); ++i) {
      slots_[i]->push(parent);
    }
    if (!slots_.empty()) {
      slots_.back()->push(std::move(parent));
    }
    core::DataDispatcher::instance().dispatch(out_id_, data, size);
  }

  FlowRuntime& rt_;
  std::string out_channel_;
  LineageChannel* slot_;
  std::function<TOut(const TIn&)> fn_;
  RawFn raw_fn_{nullptr};
  std::unique_ptr<base::CacheBuffer<TIn>> buffer_;
  core::ChannelId out_id_{0};
  HistoryRing* history_{nullptr};
  std::vector<LineageChannel*> slots_;
  std::uint64_t seq_{0};
};

// Specialized join stage (ADR-0032): the dual-input DataVisitor fused
// discipline (fire only when both buffers are non-empty, consume one of
// each) with lineage merged a-then-b exactly like attach_join. The
// single-flight guard serializes concurrent fused firings from the two
// producer threads; the loop then drains every ready pair under one
// guard acquisition.
template <typename TA, typename TB, typename TC>
class FastJoinStage final : public StageHolder, public FastStageBase {
 public:
  using RawFn = TC (*)(const TA&, const TB&);

  FastJoinStage(FlowRuntime& rt, const std::string& in_a, const std::string& in_b,
                std::string out_channel, std::function<TC(const TA&, const TB&)> fn,
                LineageChannel* slot_a, LineageChannel* slot_b, std::size_t depth)
      : rt_(rt), out_channel_(std::move(out_channel)), slot_a_(slot_a), slot_b_(slot_b) {
    fn_ = std::move(fn);
    const RawFn* raw = fn_.template target<RawFn>();
    raw_fn_ = raw != nullptr ? *raw : nullptr;
    const auto box = std::make_shared<core::DataVisitor<TA, TB>*>(nullptr);
    visitor_ = std::make_unique<core::DataVisitor<TA, TB>>(in_a, in_b, depth, [this, box] {
      if (*box == nullptr || drain_guard_.test_and_set(std::memory_order_acquire)) {
        return;
      }
      while (true) {
        TA* a = (*box)->try_fetch_0();
        if (a == nullptr) {
          break;
        }
        TB* b = (*box)->try_fetch_1();
        if (b == nullptr) {
          break;
        }
        core::Lineage merged = slot_a_->pop();
        merged.merge(slot_b_->pop());
        TC out = raw_fn_ != nullptr ? raw_fn_(*a, *b) : fn_(*a, *b);
        fan(std::move(merged), &out, sizeof(TC));
      }
      drain_guard_.clear(std::memory_order_release);
    });
    *box = visitor_.get();
  }

  void bind_fan(FanBinding binding) override {
    out_id_ = binding.id;
    history_ = binding.history;
    slots_ = std::move(binding.slots);
  }

  [[nodiscard]] const std::string& out_channel() const override { return out_channel_; }
  [[nodiscard]] const void* dispatcher_owner() const override { return visitor_.get(); }

 private:
  void fan(core::Lineage merged, const void* data, std::size_t size) {
    const std::uint64_t seq = seq_++;
    merged.add_hop(core::LineageHop{.channel = out_channel_, .seq = seq});
    if (rt_.recording_active()) {
      rt_.publish_bytes(out_channel_, data, size, std::move(merged));
      return;
    }
    if (history_ != nullptr) {
      history_->push(seq, data, size, merged);
    }
    for (std::size_t i = 0; i + 1 < slots_.size(); ++i) {
      slots_[i]->push(merged);
    }
    if (!slots_.empty()) {
      slots_.back()->push(std::move(merged));
    }
    core::DataDispatcher::instance().dispatch(out_id_, data, size);
  }

  FlowRuntime& rt_;
  std::string out_channel_;
  LineageChannel* slot_a_;
  LineageChannel* slot_b_;
  std::function<TC(const TA&, const TB&)> fn_;
  RawFn raw_fn_{nullptr};
  std::unique_ptr<core::DataVisitor<TA, TB>> visitor_;
  core::ChannelId out_id_{0};
  HistoryRing* history_{nullptr};
  std::vector<LineageChannel*> slots_;
  std::uint64_t seq_{0};
  std::atomic_flag drain_guard_;
};

// Specialized sink stage (ADR-0032): terminal consumer, no fan.
template <typename T>
class FastSinkStage final : public StageHolder {
 public:
  FastSinkStage(const std::string& channel, std::function<void(const T&, const core::Lineage&)> fn,
                LineageChannel* slot, std::size_t depth)
      : slot_(slot) {
    fn_ = std::move(fn);
    buffer_ = std::make_unique<base::CacheBuffer<T>>(depth);
    core::DataDispatcher::instance().add_buffer(
        core::channel_id_for(channel), buffer_.get(),
        [this] {
          while (T* msg = buffer_->try_fetch()) {
            fn_(*msg, slot_->pop());
          }
        },
        this);
  }

  [[nodiscard]] const void* dispatcher_owner() const override { return this; }

 private:
  LineageChannel* slot_;
  std::function<void(const T&, const core::Lineage&)> fn_;
  std::unique_ptr<base::CacheBuffer<T>> buffer_;
};

}  // namespace detail

// Specialized install driver (ADR-0032). The plan is computed from the
// Flow itself — the same declaration graph the interpreter walks — so
// there is no IR/runtime drift by construction:
//   - fast_fan(channel): exactly one producer, of kind map or join,
//     and the flow declares no SLA endpoints (v0 keeps SLA-bearing
//     flows fully generic so histogram semantics are trivially exact);
//   - inbox vs queue per input: single-producer channels deliver
//     through the lock-free LineageInbox, multi-producer channels keep
//     the locked LineageQueue;
//   - op/stateful/span/from stages always install generically; their
//     outputs are multi-writer or OpPub-published channels.
class SpecializeBuilder {
 public:
  template <typename TIn, typename TOut>
  void add_map(const std::string& in_channel, const std::string& out_channel,
               std::function<TOut(const TIn&)> fn) {
    if (!fast_fan(out_channel)) {
      rt_.attach_map<TIn, TOut>(in_channel, out_channel, std::move(fn));
      return;
    }
    auto* slot = rt_.register_specialized_slot(in_channel, inbox_input(in_channel));
    auto stage = std::make_unique<detail::FastMapStage<TIn, TOut>>(
        rt_, in_channel, out_channel, std::move(fn), slot, FlowRuntime::kQueueDepth);
    fast_stages_.push_back(stage.get());
    rt_.stages_.push_back(std::move(stage));
  }

  template <typename TA, typename TB, typename TC>
  void add_join(const std::string& in_a, const std::string& in_b, const std::string& out_channel,
                std::function<TC(const TA&, const TB&)> fn) {
    if (!fast_fan(out_channel)) {
      rt_.attach_join<TA, TB, TC>(in_a, in_b, out_channel, std::move(fn));
      return;
    }
    auto* slot_a = rt_.register_specialized_slot(in_a, inbox_input(in_a));
    auto* slot_b = rt_.register_specialized_slot(in_b, inbox_input(in_b));
    auto stage = std::make_unique<detail::FastJoinStage<TA, TB, TC>>(
        rt_, in_a, in_b, out_channel, std::move(fn), slot_a, slot_b, FlowRuntime::kQueueDepth);
    fast_stages_.push_back(stage.get());
    rt_.stages_.push_back(std::move(stage));
  }

  template <typename T>
  void add_sink(const std::string& channel,
                std::function<void(const T&, const core::Lineage&)> fn) {
    auto* slot = rt_.register_specialized_slot(channel, inbox_input(channel));
    rt_.stages_.push_back(std::make_unique<detail::FastSinkStage<T>>(channel, std::move(fn), slot,
                                                                     FlowRuntime::kQueueDepth));
  }

  // Binds every fast producer's fan-out now that all consumers are
  // registered: channel_queues_ holds the full consumer list in wire
  // order (inboxes and generic queues alike), and the forced context
  // resolution freezes id / history pointer for the channel.
  void finalize() {
    if (all_generic_) {
      return;
    }
    for (auto* stage : fast_stages_) {
      const FlowRuntime::PublishCtx& ctx = rt_.resolve_publish_ctx(stage->out_channel());
      detail::FanBinding binding;
      binding.id = ctx.id;
      binding.history = history_on(stage->out_channel()) ? ctx.history : nullptr;
      const auto found = rt_.channel_queues_.find(stage->out_channel());
      if (found != rt_.channel_queues_.end()) {
        binding.slots = found->second;
      }
      stage->bind_fan(std::move(binding));
    }
  }

 private:
  friend class FlowRuntime;

  SpecializeBuilder(FlowRuntime& rt, const Flow& flow) : rt_(rt) {
    all_generic_ = !flow.sla_endpoints().empty();
    if (all_generic_) {
      return;
    }
    std::unordered_map<std::string, std::string> producer_kind;
    const auto produce = [&producer_kind](const std::string& channel, const char* kind) {
      const auto [it, inserted] = producer_kind.try_emplace(channel, kind);
      if (!inserted) {
        it->second = "multi";
      }
    };
    for (const auto& source : flow.sources()) {
      produce(source.channel, "source");
    }
    for (const auto& map_decl : flow.maps()) {
      produce(map_decl.out_channel, "map");
    }
    for (const auto& join_decl : flow.joins()) {
      produce(join_decl.out_channel, "join");
    }
    for (const auto& op_decl : flow.ops()) {
      produce(op_decl.out_channel, "op");
    }
    for (const auto& st_decl : flow.statefuls()) {
      produce(st_decl.out_channel, "stateful");
      produce(st_decl.state_channel, "stateful");
    }
    for (const auto& span_decl : flow.spans()) {
      produce(span_decl.out_channel, "span");
    }
    for (const auto& from_decl : flow.froms()) {
      produce(from_decl.out_channel, "from");
    }
    producer_kind_ = std::move(producer_kind);
    for (const auto& span_decl : flow.spans()) {
      history_observers_.insert(span_decl.data_channel);
    }
    for (const auto& st_decl : flow.statefuls()) {
      history_observers_.insert(st_decl.state_channel);
    }
  }

  [[nodiscard]] bool fast_fan(const std::string& channel) const {
    if (all_generic_) {
      return false;
    }
    const auto it = producer_kind_.find(channel);
    return it != producer_kind_.end() && (it->second == "map" || it->second == "join");
  }

  [[nodiscard]] bool inbox_input(const std::string& channel) const {
    const auto it = producer_kind_.find(channel);
    if (it == producer_kind_.end()) {
      // Tap / externally published channels: writer discipline is
      // unknown, keep the locked queue.
      return false;
    }
    if (it->second == "source") {
      // One timer thread per source declaration (ADR-0021).
      return true;
    }
    // Sole fast map/join producers run single-threaded by construction:
    // linear cascades execute on the driving thread, joins are
    // serialized by their single-flight guard. Generic stages
    // (op/stateful/span/from) keep the locked queue even as sole
    // producers — their handles can fire concurrently when fed by
    // concurrent publishers.
    return fast_fan(channel);
  }

  [[nodiscard]] bool history_on(const std::string& channel) const {
    return history_observers_.contains(channel);
  }

  FlowRuntime& rt_;
  bool all_generic_{false};
  std::unordered_map<std::string, std::string> producer_kind_;
  std::unordered_set<std::string> history_observers_;
  std::vector<detail::FastStageBase*> fast_stages_;
};

namespace detail {

template <typename T>
std::function<void(FlowRuntime&, std::uint64_t)> make_source_drive(
    std::string channel, std::function<T(std::uint64_t)> emit) {
  return
      [channel = std::move(channel), emit = std::move(emit)](FlowRuntime& rt, std::uint64_t tick) {
        const T value = emit(tick);
        auto lin = core::Lineage::rooted(channel, tick);
        rt.publish_bytes(channel, &value, sizeof(T), lin);
      };
}

template <typename TIn, typename TOut>
std::function<void(FlowRuntime&)> make_map_wire(std::string in_channel, std::string out_channel,
                                                std::function<TOut(const TIn&)> fn) {
  return [in_channel = std::move(in_channel), out_channel = std::move(out_channel),
          fn = std::move(fn)](FlowRuntime& rt) {
    rt.template attach_map<TIn, TOut>(in_channel, out_channel, fn);
  };
}

template <typename TA, typename TB, typename TC>
std::function<void(FlowRuntime&)> make_join_wire(std::string in_a, std::string in_b,
                                                 std::string out_channel,
                                                 std::function<TC(const TA&, const TB&)> fn) {
  return [in_a = std::move(in_a), in_b = std::move(in_b), out_channel = std::move(out_channel),
          fn = std::move(fn)](FlowRuntime& rt) {
    rt.template attach_join<TA, TB, TC>(in_a, in_b, out_channel, fn);
  };
}

template <typename TIn, typename TOut, typename TOp>
std::function<void(FlowRuntime&)> make_op_wire(std::string in_channel, std::string out_channel,
                                               TOp impl) {
  return [in_channel = std::move(in_channel), out_channel = std::move(out_channel),
          impl](FlowRuntime& rt) {
    rt.template attach_op<TIn, TOut, TOp>(in_channel, out_channel, impl);
  };
}

template <typename TTrig, typename TData, typename TOut, typename TSpanFn, typename TTimeFn,
          typename TImpl>
std::function<void(FlowRuntime&)> make_span_wire(std::string trig_channel, std::string data_channel,
                                                 std::string out_channel, TSpanFn span_fn,
                                                 TTimeFn time_fn, TImpl impl) {
  return [trig_channel = std::move(trig_channel), data_channel = std::move(data_channel),
          out_channel = std::move(out_channel), span_fn = std::move(span_fn),
          time_fn = std::move(time_fn), impl = std::move(impl)](FlowRuntime& rt) mutable {
    rt.template attach_span<TTrig, TData, TOut, TSpanFn, TTimeFn, TImpl>(
        trig_channel, data_channel, out_channel, std::move(span_fn), std::move(time_fn),
        std::move(impl));
  };
}

template <typename TIn, typename TOut, typename TState, typename TImpl>
std::function<void(FlowRuntime&)> make_stateful_wire(std::string in_channel,
                                                     std::string out_channel,
                                                     std::string state_channel, TImpl impl) {
  return
      [in_channel = std::move(in_channel), out_channel = std::move(out_channel),
       state_channel = std::move(state_channel), impl = std::move(impl)](FlowRuntime& rt) mutable {
        rt.template attach_stateful<TIn, TOut, TState, TImpl>(in_channel, out_channel,
                                                              state_channel, std::move(impl));
      };
}

template <typename TOut>
bool probe_source_shape(std::string_view registry_name) {
  const auto comp =
      core::ComponentFactory::instance().create(registry_name, std::string(registry_name));
  return comp != nullptr && dynamic_cast<core::TimerSourceComponent<TOut>*>(comp.get()) != nullptr;
}

template <typename TIn, typename TOut>
bool probe_component_shape(std::string_view registry_name) {
  const auto comp =
      core::ComponentFactory::instance().create(registry_name, std::string(registry_name));
  return comp != nullptr && dynamic_cast<core::Component<TIn, TOut>*>(comp.get()) != nullptr;
}

template <typename TIn0, typename TIn1, typename TOut>
bool probe_component2_shape(std::string_view registry_name) {
  const auto comp =
      core::ComponentFactory::instance().create(registry_name, std::string(registry_name));
  return comp != nullptr &&
         dynamic_cast<core::TwoInputComponent<TIn0, TIn1, TOut>*>(comp.get()) != nullptr;
}

template <typename TOut>
std::function<void(FlowRuntime&)> make_from_source_wire(std::string registry_name,
                                                        std::string out_channel,
                                                        std::chrono::milliseconds interval) {
  return [registry_name = std::move(registry_name), out_channel = std::move(out_channel),
          interval](FlowRuntime& rt) {
    rt.attach_referenced_source(registry_name, out_channel, interval);
  };
}

template <typename TIn, typename TOut>
std::function<void(FlowRuntime&)> make_from_component_wire(std::string registry_name,
                                                           std::string in_channel,
                                                           std::string out_channel) {
  return [registry_name = std::move(registry_name), in_channel = std::move(in_channel),
          out_channel = std::move(out_channel)](FlowRuntime& rt) {
    rt.attach_referenced_component(registry_name, in_channel, out_channel);
  };
}

template <typename TIn0, typename TIn1, typename TOut>
std::function<void(FlowRuntime&)> make_from_component2_wire(std::string registry_name,
                                                            std::string in_channel0,
                                                            std::string in_channel1,
                                                            std::string out_channel) {
  return [registry_name = std::move(registry_name), in0 = std::move(in_channel0),
          in1 = std::move(in_channel1), out_channel = std::move(out_channel)](FlowRuntime& rt) {
    rt.attach_referenced_component2(registry_name, in0, in1, out_channel);
  };
}

template <typename T>
std::function<void(FlowRuntime&)> make_sink_wire(
    std::string channel, std::function<void(const T&, const core::Lineage&)> fn) {
  return [channel = std::move(channel), fn = std::move(fn)](FlowRuntime& rt) {
    rt.template attach_sink<T>(channel, fn);
  };
}

// Specialize-hook factories (ADR-0032): typed closures created where
// the builder knows the message types; they hand the operator to the
// SpecializeBuilder, which decides fast vs generic per channel.
template <typename TIn, typename TOut>
std::function<void(FlowRuntime&, SpecializeBuilder&)> make_map_specialize(
    std::string in_channel, std::string out_channel, std::function<TOut(const TIn&)> fn) {
  return [in_channel = std::move(in_channel), out_channel = std::move(out_channel),
          fn = std::move(fn)](FlowRuntime& /*rt*/, SpecializeBuilder& plan) {
    plan.template add_map<TIn, TOut>(in_channel, out_channel, fn);
  };
}

template <typename TA, typename TB, typename TC>
std::function<void(FlowRuntime&, SpecializeBuilder&)> make_join_specialize(
    std::string in_a, std::string in_b, std::string out_channel,
    std::function<TC(const TA&, const TB&)> fn) {
  return [in_a = std::move(in_a), in_b = std::move(in_b), out_channel = std::move(out_channel),
          fn = std::move(fn)](FlowRuntime& /*rt*/, SpecializeBuilder& plan) {
    plan.template add_join<TA, TB, TC>(in_a, in_b, out_channel, fn);
  };
}

template <typename T>
std::function<void(FlowRuntime&, SpecializeBuilder&)> make_sink_specialize(
    std::string channel, std::function<void(const T&, const core::Lineage&)> fn) {
  return [channel = std::move(channel), fn = std::move(fn)](FlowRuntime& /*rt*/,
                                                            SpecializeBuilder& plan) {
    plan.template add_sink<T>(channel, fn);
  };
}

}  // namespace detail

}  // namespace tianshu::dsl
