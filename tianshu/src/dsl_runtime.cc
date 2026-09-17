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

#include "tianshu/dsl/dsl_runtime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

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

namespace {

// The message's own channel-local seq: publish paths always close every
// branch with a hop on the publishing channel (rooted for sources), so
// the last hop of the first branch — or the root when hopless — is it.
std::uint64_t own_seq_of(const core::Lineage& lin) {
  if (lin.branches().empty()) {
    return 0;
  }
  const auto& branch = lin.branches().front();
  return branch.hops.empty() ? branch.root.seq : branch.hops.back().seq;
}

}  // namespace
void FlowRuntime::publish_bytes(const std::string& channel, const void* data, std::size_t size,
                                const core::Lineage& lineage) {
  publish_impl(channel, data, size, &lineage, nullptr);
}

void FlowRuntime::publish_bytes(const std::string& channel, const void* data, std::size_t size,
                                core::Lineage&& lineage) {
  publish_impl(channel, data, size, nullptr, &lineage);
}

const FlowRuntime::PublishCtx& FlowRuntime::resolve_publish_ctx(const std::string& channel) {
  // Snapshot resolution (ADR-0030 D8 L1c): steady state is one atomic
  // load + one hash; a first publish on a channel builds the entry
  // under the runtime mutex and publishes a copy-on-write snapshot.
  // Entry shared_ptrs keep each channel's SpinLock identity stable
  // across snapshot swaps.
  //
  // Pointer-identity fast path (ADR-0032): drive loops and replay
  // publish through one stable string object per channel, so a
  // thread_local {owner, key, content-snapshot, epoch} cache skips the
  // hash on the steady-state entry path. The owner check defeats
  // cross-runtime address reuse; the epoch check (bumped at every
  // snapshot swap) keeps the cache coherent across wiring rounds and
  // recorder arming; the content snapshot defeats same-address reuse
  // by a different string.
  struct PtrCache {
    const FlowRuntime* owner{nullptr};
    const std::string* key{nullptr};
    std::uint64_t epoch{0};
    std::size_t len{0};
    std::array<char, 48> bytes{};
    std::shared_ptr<PublishCtx> ctx;
  };
  static thread_local PtrCache cache;
  const std::uint64_t epoch = ctx_epoch_.load(std::memory_order_acquire);
  const std::size_t cmp_len = std::min(channel.size(), cache.bytes.size());
  if (cache.owner == this && cache.epoch == epoch && cache.key == &channel &&
      cache.len == channel.size() &&
      channel.compare(0, cmp_len, cache.bytes.data(), cmp_len) == 0) {
    return *cache.ctx;
  }
  auto fill_cache = [this, &channel, epoch, cmp_len](const std::shared_ptr<PublishCtx>& ctx) {
    cache.owner = this;
    cache.key = &channel;
    cache.epoch = epoch;
    cache.len = channel.size();
    cache.bytes.fill('\0');
    std::copy_n(channel.begin(), cmp_len, cache.bytes.begin());
    cache.ctx = ctx;
  };
  auto snapshot = pub_ctx_.load();
  auto ctx_entry = snapshot->find(channel);
  if (ctx_entry != snapshot->end() && epoch == ctx_epoch_.load(std::memory_order_acquire)) {
    fill_cache(ctx_entry->second);
    return *ctx_entry->second;
  }
  const std::scoped_lock lock(mutex_);
  snapshot = pub_ctx_.load();
  ctx_entry = snapshot->find(channel);
  if (ctx_entry != snapshot->end()) {
    fill_cache(ctx_entry->second);
    return *ctx_entry->second;
  }
  auto resolved = std::make_shared<PublishCtx>();
  resolved->id = core::channel_id_for(channel);
  const auto queues_it = channel_queues_.find(channel);
  if (queues_it != channel_queues_.end()) {
    resolved->queues = queues_it->second;
  }
  resolved->history = &histories_.try_emplace(channel, kHistoryDepth).first->second;
  resolved->history_on = history_observers_.empty() || history_observers_.contains(channel);
  if (recorder_ != nullptr) {
    const auto id_it = recorder_channel_ids_.find(channel);
    resolved->rec_ch = id_it != recorder_channel_ids_.end()
                           ? id_it->second
                           : recorder_->add_channel(channel, 0, "");
    if (id_it == recorder_channel_ids_.end()) {
      recorder_channel_ids_[channel] = resolved->rec_ch;
    }
    resolved->recorded = true;
  }
  auto fresh =
      std::make_shared<std::unordered_map<std::string, std::shared_ptr<PublishCtx>>>(*snapshot);
  (*fresh)[channel] = std::move(resolved);
  pub_ctx_.store(
      std::shared_ptr<const std::unordered_map<std::string, std::shared_ptr<PublishCtx>>>(
          std::move(fresh)));
  const auto& entry = *(*pub_ctx_.load()).at(channel);
  fill_cache((*pub_ctx_.load()).at(channel));
  return entry;
}

void FlowRuntime::publish_impl(const std::string& channel, const void* data, std::size_t size,
                               const core::Lineage* lineage_copy, core::Lineage* lineage_move) {
  // SLA runtime defense (ADR-0029 D6): the v0 cascade is synchronous
  // within one source tick, so a thread-local birth timestamp carries
  // the e2e origin exactly. Flows without SLA endpoints pay one null
  // check; uninstrumented channels pay a map miss.
  static thread_local std::uint64_t sla_born_ns = 0;
  const bool sla_outermost = sla_stats_ != nullptr && sla_born_ns == 0;
  std::uint64_t sla_now_ns = 0;
  if (sla_stats_ != nullptr) {
    sla_now_ns = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                std::chrono::steady_clock::now().time_since_epoch())
                                                .count());
    if (sla_outermost) {
      sla_born_ns = sla_now_ns;
    }
  }
  const core::Lineage& lineage = lineage_copy != nullptr ? *lineage_copy : *lineage_move;

  // Single-lookup publish context (ADR-0030 D8 L1): one hash instead of
  // queues/history/recorder/dispatcher-id lookups per message. Built
  // lazily under the mutex; invalidated whenever wire() adds consumers.
  // Steady-state lock-free publish (ADR-0030 D8 L1b): the context is
  // resolved under the runtime mutex once (unordered_map nodes are
  // stable, pointers to queues/history outlive the wiring round), then
  // every push happens outside it. Per-channel single-writer holds by
  // the v0 synchronous cascade: one channel, one producing stage, one
  // thread per message. The recorder keeps its own lock below.
  const PublishCtx& ctx = resolve_publish_ctx(channel);

  // Live recording (ADR-0028 v2) reads the lineage; it runs BEFORE the
  // final destination moves from it (ADR-0030 D8 L2).
  if (ctx.recorded) {
    const auto now_ns =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                       std::chrono::steady_clock::now().time_since_epoch())
                                       .count());
    const std::scoped_lock recorder_lock(recorder_mutex_);
    recorder_->append(ctx.rec_ch, own_seq_of(lineage), now_ns, data, size, &lineage);
  }

  // History first: it copies while the lineage is still intact; the
  // last consumer queue then receives the move. With no consumers the
  // history itself takes the move. The per-channel lock covers feedback
  // channels' second writer; uncontended elsewhere. Specialized runs
  // skip the capture on channels with no graph-declared observer
  // (ADR-0032 narrowing); on those channels a move-only lineage with
  // no consumers is simply dropped.
  {
    const std::scoped_lock push_guard(ctx.push_lock);
    if (ctx.history_on) {
      if (lineage_move != nullptr && ctx.queues.empty()) {
        ctx.history->push(own_seq_of(lineage), data, size, std::move(*lineage_move));
      } else {
        ctx.history->push(own_seq_of(lineage), data, size, lineage);
      }
    }

    for (std::size_t i = 0; i < ctx.queues.size(); ++i) {
      const bool last = i + 1 == ctx.queues.size();
      if (last && lineage_move != nullptr) {
        ctx.queues[i]->push(std::move(*lineage_move));
      } else {
        ctx.queues[i]->push(lineage);
      }
    }
  }
  core::DataDispatcher::instance().dispatch(ctx.id, data, size);

  if (sla_stats_ != nullptr) {
    sla_stats_->record_if_endpoint(channel, std::chrono::nanoseconds{sla_now_ns - sla_born_ns});
    if (sla_outermost) {
      sla_born_ns = 0;
    }
  }
}

const detail::HistoryRing* FlowRuntime::history(const std::string& channel) const {
  const std::scoped_lock lock(mutex_);
  const auto it = histories_.find(channel);
  return it == histories_.end() ? nullptr : &it->second;
}

std::vector<sla::SlaEndpointStats> FlowRuntime::sla_snapshot() const {
  return sla_stats_ != nullptr ? sla_stats_->snapshot() : std::vector<sla::SlaEndpointStats>{};
}

FlowRuntime::FallbackState FlowRuntime::fallback_state() const {
  const std::scoped_lock lock(fallback_mutex_);
  return fallback_;
}

void FlowRuntime::start_recording(const std::string& path, record::Compression compression) {
  if (recorder_ != nullptr) {
    return;
  }
  recorder_ = std::make_unique<record::RecordWriter>(path, compression);
  recorder_channel_ids_.clear();
  recording_start_ts_ =
      static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
  // Arm the fast-path fallback and drop resolved publish contexts so
  // every channel re-resolves with recorded=true. Without this, a
  // recorder armed after the first publish (or after a specialized
  // install, which pre-resolves its fan channels) would silently skip
  // capture on the already-resolved entries.
  recording_active_.store(true, std::memory_order_relaxed);
  {
    const std::scoped_lock lock(mutex_);
    pub_ctx_ =
        std::make_shared<const std::unordered_map<std::string, std::shared_ptr<PublishCtx>>>();
    ++ctx_epoch_;
  }
}

bool FlowRuntime::stop_recording() {
  if (recorder_ == nullptr) {
    return false;
  }
  const bool ok = recorder_->finish();
  recorder_.reset();
  recorder_channel_ids_.clear();
  recording_active_.store(false, std::memory_order_relaxed);
  {
    const std::scoped_lock lock(mutex_);
    pub_ctx_ =
        std::make_shared<const std::unordered_map<std::string, std::shared_ptr<PublishCtx>>>();
    ++ctx_epoch_;
  }
  return ok;
}

bool FlowRuntime::is_recording() const { return recorder_ != nullptr; }

bool FlowRuntime::record_to(const std::string& path) const {
  RecordFile file(path);
  const std::scoped_lock lock(mutex_);
  for (const auto& [channel, ring] : histories_) {
    for (const auto& entry : ring.entries()) {
      const RecordedMessage rec{
          .channel = channel,
          .seq = entry.seq,
          .bytes = std::vector<std::uint8_t>(entry.bytes.begin(), entry.bytes.end()),
          .lineage_text = entry.lineage.describe()};
      file.append(rec);
    }
  }
  return file.save();
}

void FlowRuntime::replay_from(const std::vector<RecordedMessage>& records) {
  // Records arrive in capture order, so a straight re-publish
  // reproduces the original cascade (slice query's offline substrate).
  for (const auto& rec : records) {
    publish_bytes(rec.channel, rec.bytes.data(), rec.bytes.size(),
                  core::Lineage::rooted(rec.channel, rec.seq));
  }
}

std::shared_ptr<detail::LineageQueue> FlowRuntime::register_lineage_queue(
    const std::string& channel) {
  {
    // Consumer registration invalidates publish snapshots: the compiled
    // install path replays stage closures without entering wire(), so
    // the snapshot invalidation must live HERE, not only in wire().
    const std::scoped_lock lock(mutex_);
    pub_ctx_ =
        std::make_shared<const std::unordered_map<std::string, std::shared_ptr<PublishCtx>>>();
    ++ctx_epoch_;
  }
  auto lineage_queue = std::make_shared<detail::LineageQueue>(kQueueDepth * 2);
  const std::scoped_lock lock(mutex_);
  channel_queues_[channel].push_back(lineage_queue.get());
  lineage_queues_.push_back(lineage_queue);
  return lineage_queue;
}

detail::LineageChannel* FlowRuntime::register_specialized_slot(const std::string& channel,
                                                               bool inbox) {
  std::unique_ptr<detail::LineageChannel> slot;
  if (inbox) {
    slot = std::make_unique<detail::LineageInbox>(kQueueDepth * 2);
  } else {
    slot = std::make_unique<detail::LineageQueue>(kQueueDepth * 2);
  }
  const std::scoped_lock lock(mutex_);
  pub_ctx_ = std::make_shared<const std::unordered_map<std::string, std::shared_ptr<PublishCtx>>>();
  ++ctx_epoch_;
  channel_queues_[channel].push_back(slot.get());
  owned_slots_.push_back(std::move(slot));
  return owned_slots_.back().get();
}

FlowRuntime::~FlowRuntime() {
  // Deregister dispatcher sinks before the stage buffers die: the
  // dispatcher is a process-wide singleton, and leftover entries are
  // dangling pointers that a later runtime publishing on the same
  // channel id would call into (two sequentially destroyed runtimes
  // with identical flow names crash the second publish otherwise).
  auto& dispatcher = core::DataDispatcher::instance();
  for (const auto& stage : stages_) {
    dispatcher.remove_owner(stage->dispatcher_owner());
  }
  for (const auto& comp : components_) {
    comp->shutdown();
  }
}

void FlowRuntime::attach_referenced_source(const std::string& registry_name,
                                           const std::string& out_channel,
                                           std::chrono::milliseconds interval) {
  auto comp = core::ComponentFactory::instance().create(registry_name, registry_name);
  if (comp == nullptr) {
    return;
  }
  if (bridge_node_ == nullptr) {
    bridge_node_ = std::make_unique<core::Node>(transport::TransportMode::kIntra);
  }
  comp->set_out_channel_override(out_channel);
  if (!comp->launch(*bridge_node_, {}, interval)) {
    return;
  }
  attach_bridge_reader(out_channel);
  components_.emplace_back(std::move(comp));
  const auto held = components_.back();
  init_hooks_.emplace_back([held] { held->init(); });
}

void FlowRuntime::attach_referenced_component(const std::string& registry_name,
                                              const std::string& in_channel,
                                              const std::string& out_channel) {
  auto comp = core::ComponentFactory::instance().create(registry_name, registry_name);
  if (comp == nullptr) {
    return;
  }
  if (bridge_node_ == nullptr) {
    bridge_node_ = std::make_unique<core::Node>(transport::TransportMode::kIntra);
  }
  comp->set_out_channel_override(out_channel);
  if (!comp->launch(*bridge_node_, {in_channel}, {})) {
    return;
  }
  // Lineage pairing (ADR-0025 correction): the lineage_queue pops 1:1 with the
  // component's FIFO consumption, so every publish inside proc carries
  // its triggering input's lineage as parent — the loop unrolls across
  // the component boundary.
  const auto lineage_queue = register_lineage_queue(in_channel);
  comp->set_input_lineage_provider([lineage_queue] { return lineage_queue->pop(); });
  attach_bridge_reader(out_channel);
  components_.emplace_back(std::move(comp));
  const auto held = components_.back();
  init_hooks_.emplace_back([held] { held->init(); });
}

void FlowRuntime::attach_referenced_component2(const std::string& registry_name,
                                               const std::string& in0, const std::string& in1,
                                               const std::string& out_channel) {
  auto comp = core::ComponentFactory::instance().create(registry_name, registry_name);
  if (comp == nullptr) {
    return;
  }
  if (bridge_node_ == nullptr) {
    bridge_node_ = std::make_unique<core::Node>(transport::TransportMode::kIntra);
  }
  comp->set_out_channel_override(out_channel);
  if (!comp->launch(*bridge_node_, {in0, in1}, {})) {
    return;
  }
  // Lineage pairing for the fused pair (ADR-0025 correction applied to
  // TwoInputComponent): one lineage pops from EACH input queue per
  // consumed (msg0, msg1) pair and the branches merge — same DAG
  // provenance rule as a DSL join.
  const auto q0 = register_lineage_queue(in0);
  const auto q1 = register_lineage_queue(in1);
  comp->set_input_lineage_provider([q0, q1] {
    core::Lineage lin = q0->pop();
    lin.merge(q1->pop());
    return lin;
  });
  attach_bridge_reader(out_channel);
  components_.emplace_back(std::move(comp));
  const auto held = components_.back();
  init_hooks_.emplace_back([held] { held->init(); });
}

void FlowRuntime::publish_derived(core::Lineage parent, const std::string& channel,
                                  const void* data, std::size_t size) {
  parent.add_hop({.channel = channel, .seq = next_seq(channel)});
  publish_bytes(channel, data, size, std::move(parent));
}

void FlowRuntime::attach_bridge_reader(const std::string& out_channel) {
  auto reader = bridge_node_->create_reader(out_channel);
  reader->set_callback([this, out_channel](const transport::Message& msg) {
    // Component output with a parent lineage (published inside proc,
    // ADR-0025 correction): derive from it; lineage-free outputs (no
    // provider) and init-time publishes (empty parent) root at the
    // channel — same rule as publish_op.
    const auto* parent = static_cast<const core::Lineage*>(msg.lineage_ptr);
    if (parent != nullptr && !parent->empty()) {
      publish_derived(*parent, out_channel, msg.data, msg.size);
      return;
    }
    publish_bytes(out_channel, msg.data, msg.size,
                  core::Lineage::rooted(out_channel, next_seq(out_channel)));
  });
  bridge_readers_.push_back(std::move(reader));
}

void FlowRuntime::publish_op(const std::string& channel, const void* data, std::size_t size,
                             const core::Lineage& parent) {
  if (parent.empty()) {
    publish_bytes(channel, data, size, core::Lineage::rooted(channel, next_seq(channel)));
    return;
  }
  publish_derived(parent, channel, data, size);
}

std::uint64_t FlowRuntime::next_seq(const std::string& channel) {
  const std::scoped_lock lock(mutex_);
  return seq_counters_[channel]++;
}

namespace {

void drive_source(const Flow::SourceDecl& source, std::chrono::milliseconds duration,
                  std::chrono::steady_clock::time_point start, FlowRuntime* rt) {
  const auto interval = source.interval;
  std::uint64_t tick = 0;
  auto next = start + interval;
  while (std::chrono::steady_clock::now() - start < duration) {
    std::this_thread::sleep_until(next);
    if (std::chrono::steady_clock::now() - start >= duration) {
      return;
    }
    source.drive(*rt, tick);
    ++tick;
    next += interval;
  }
}

}  // namespace

void FlowRuntime::run_for(const Flow& flow, std::chrono::milliseconds duration) {
  wire(flow);
  run_sources(flow, duration);
}

void FlowRuntime::wire(const Flow& flow) {
  {
    const std::scoped_lock lock(mutex_);
    pub_ctx_ =
        std::make_shared<const std::unordered_map<std::string, std::shared_ptr<PublishCtx>>>();
    ++ctx_epoch_;
    history_observers_.clear();
  }
  if (!flow.sla_endpoints().empty() && sla_stats_ == nullptr) {
    sla_stats_ = std::make_unique<sla::SlaStatsCollector>();
    for (const auto& endpoint : flow.sla_endpoints()) {
      sla_stats_->add_endpoint(endpoint.channel, endpoint.deadline);
    }
  }
  for (const auto& map_decl : flow.maps()) {
    map_decl.wire(*this);
  }
  for (const auto& join_decl : flow.joins()) {
    join_decl.wire(*this);
  }
  for (const auto& op_decl : flow.ops()) {
    op_decl.wire(*this);
  }
  for (const auto& st_decl : flow.statefuls()) {
    st_decl.wire(*this);
  }
  for (const auto& span_decl : flow.spans()) {
    span_decl.wire(*this);
  }
  for (const auto& from_decl : flow.froms()) {
    from_decl.wire(*this);
  }
  for (const auto& sink_decl : flow.sinks()) {
    sink_decl.wire(*this);
  }
}

SpecializeBuilder FlowRuntime::begin_specialize(const Flow& flow) {
  {
    const std::scoped_lock lock(mutex_);
    pub_ctx_ =
        std::make_shared<const std::unordered_map<std::string, std::shared_ptr<PublishCtx>>>();
    ++ctx_epoch_;
    history_observers_.clear();
  }
  // Same SLA arming discipline as wire(): compiled artifacts install
  // through here, so a flow with endpoints gets its histograms armed.
  // Those flows stay fully generic in v0 (ADR-0032), which keeps the
  // thread_local born-stamp logic of publish_impl as the single
  // implementation.
  if (!flow.sla_endpoints().empty() && sla_stats_ == nullptr) {
    sla_stats_ = std::make_unique<sla::SlaStatsCollector>();
    for (const auto& endpoint : flow.sla_endpoints()) {
      sla_stats_->add_endpoint(endpoint.channel, endpoint.deadline);
    }
  } else {
    // History-capture narrowing applies only to non-SLA flows; SLA
    // flows run the generic path in full.
    for (const auto& span_decl : flow.spans()) {
      history_observers_.insert(span_decl.data_channel);
    }
    for (const auto& st_decl : flow.statefuls()) {
      history_observers_.insert(st_decl.state_channel);
    }
  }
  return {*this, flow};
}

void FlowRuntime::wire_specialized(const Flow& flow) {
  SpecializeBuilder plan = begin_specialize(flow);
  for (const auto& map_decl : flow.maps()) {
    if (map_decl.specialize) {
      map_decl.specialize(*this, plan);
    } else {
      map_decl.wire(*this);
    }
  }
  for (const auto& join_decl : flow.joins()) {
    if (join_decl.specialize) {
      join_decl.specialize(*this, plan);
    } else {
      join_decl.wire(*this);
    }
  }
  for (const auto& op_decl : flow.ops()) {
    op_decl.wire(*this);
  }
  for (const auto& st_decl : flow.statefuls()) {
    st_decl.wire(*this);
  }
  for (const auto& span_decl : flow.spans()) {
    span_decl.wire(*this);
  }
  for (const auto& from_decl : flow.froms()) {
    from_decl.wire(*this);
  }
  for (const auto& sink_decl : flow.sinks()) {
    if (sink_decl.specialize) {
      sink_decl.specialize(*this, plan);
    } else {
      sink_decl.wire(*this);
    }
  }
  plan.finalize();
}

void FlowRuntime::run_sources(const Flow& flow, std::chrono::milliseconds duration) {
  // Bootstrap publications LAST: every consumer lineage_queue of a box output
  // channel is registered by the time on_init fires (ADR-0024).
  for (const auto& hook : init_hooks_) {
    hook();
  }

  const auto start = std::chrono::steady_clock::now();
  std::vector<std::thread> source_threads;
  source_threads.reserve(flow.sources().size());
  for (const auto& source : flow.sources()) {
    source_threads.emplace_back(drive_source, std::cref(source), duration, start, this);
  }

  // Degradation watcher (ADR-0031 v0): while a fallback is declared and
  // SLA endpoints are armed, sample the miss counters once per window and
  // fire an event for every window with fresh misses.
  constexpr auto kWindow = std::chrono::milliseconds(20);
  constexpr std::uint64_t kWindowMisses = 1;
  const bool watch_fallback = !flow.fallback_flow().empty() && sla_stats_ != nullptr;
  std::thread fallback_thread;
  std::unordered_map<std::string, std::uint64_t> last_misses;
  if (watch_fallback) {
    {
      const std::scoped_lock lock(fallback_mutex_);
      fallback_.declared = flow.fallback_flow();
    }
    fallback_thread = std::thread([this, duration, start, &last_misses, kWindow] {
      for (auto now = std::chrono::steady_clock::now(); now - start < duration;
           now = std::chrono::steady_clock::now()) {
        std::this_thread::sleep_until(now + kWindow);
        for (const auto& ep : sla_stats_->snapshot()) {
          const std::uint64_t delta = ep.miss_count - last_misses[ep.endpoint];
          last_misses[ep.endpoint] = ep.miss_count;
          if (delta >= kWindowMisses) {
            const std::scoped_lock lock(fallback_mutex_);
            ++fallback_.events;
            fallback_.last_endpoint = ep.endpoint;
            fallback_.last_miss_count = ep.miss_count;
          }
        }
      }
    });
  }

  for (auto& thread : source_threads) {
    thread.join();
  }
  if (fallback_thread.joinable()) {
    fallback_thread.join();
  }
  // Referenced timer components drive themselves on their own threads;
  // the runtime stays alive for the full duration so they are not torn
  // down early (pure-DSL flows have already waited via the joins).
  if (!components_.empty()) {
    std::this_thread::sleep_until(start + duration);
    // Quiesce BEFORE returning: joining the timer threads drains every
    // in-flight cascade, so teardown never races a running callback.
    for (const auto& comp : components_) {
      comp->quiesce();
    }
  }
}

}  // namespace tianshu::dsl
