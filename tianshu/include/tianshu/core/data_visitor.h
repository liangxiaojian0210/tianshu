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

// DataVisitor: multi-input data accessor feeding a fusion callback.
//
// Design (per L4-CORE-5, cyber-equivalent):
//   - Owns one CacheBuffer<T> per input channel
//   - Registers each buffer with DataDispatcher under channel_id
//   - On every dispatch, tries to fuse: all inputs have data -> invoke fuser
//   - AllLatest semantics (L4-COMP-6): fire when every buffer is non-empty,
//     consuming one message from each (matches cyber AllLatest)
//   - Channel id = FNV-1a of the channel name (stable across processes)

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <utility>

#include "tianshu/base/cache_buffer.h"
#include "tianshu/core/data_dispatcher.h"

namespace tianshu::core {

namespace detail {

inline constexpr std::uint64_t kFnvBasis = 14695981039346656037ULL;
inline constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

}  // namespace detail

inline ChannelId channel_id_for(std::string_view name) {
  std::uint64_t hash = detail::kFnvBasis;
  for (const char c : name) {
    hash ^= static_cast<std::uint8_t>(c);
    hash *= detail::kFnvPrime;
  }
  return hash;
}

template <typename... Ts>
class DataVisitor;

template <typename T0>
class DataVisitor<T0> {
 public:
  // on_fuse is stored as a member so the registered notify captures only
  // `this`: the dispatcher copies the notify std::function per sink per
  // dispatch, and a small-capture callable stays inside its inline buffer
  // (no heap clone, no captured-state deep copy on the delivery path).
  DataVisitor(std::string_view ch0, std::size_t depth, std::function<void()> on_fuse)
      : buffer0_(std::make_unique<base::CacheBuffer<T0>>(depth)), on_fuse_(std::move(on_fuse)) {
    const ChannelId id0 = channel_id_for(ch0);
    DataDispatcher::instance().add_buffer(
        id0, buffer0_.get(),
        [this] {
          if (!buffer0_->empty()) {
            on_fuse_();
          }
        },
        this);
  }

  ~DataVisitor() { DataDispatcher::instance().remove_owner(this); }

  DataVisitor(const DataVisitor&) = delete;
  DataVisitor& operator=(const DataVisitor&) = delete;

  T0* try_fetch_0() { return buffer0_->try_fetch(); }

 private:
  std::unique_ptr<base::CacheBuffer<T0>> buffer0_;
  std::function<void()> on_fuse_;
};

template <typename T0, typename T1>
class DataVisitor<T0, T1> {
 public:
  DataVisitor(std::string_view ch0, std::string_view ch1, std::size_t depth,
              std::function<void()> on_fuse)
      : buffer0_(std::make_unique<base::CacheBuffer<T0>>(depth)),
        buffer1_(std::make_unique<base::CacheBuffer<T1>>(depth)),
        on_fuse_(std::move(on_fuse)) {
    const auto fused = [this] {
      if (!buffer0_->empty() && !buffer1_->empty()) {
        on_fuse_();
      }
    };
    DataDispatcher::instance().add_buffer(channel_id_for(ch0), buffer0_.get(), fused, this);
    DataDispatcher::instance().add_buffer(channel_id_for(ch1), buffer1_.get(), fused, this);
  }

  ~DataVisitor() { DataDispatcher::instance().remove_owner(this); }

  DataVisitor(const DataVisitor&) = delete;
  DataVisitor& operator=(const DataVisitor&) = delete;

  T0* try_fetch_0() { return buffer0_->try_fetch(); }
  T1* try_fetch_1() { return buffer1_->try_fetch(); }

 private:
  std::unique_ptr<base::CacheBuffer<T0>> buffer0_;
  std::unique_ptr<base::CacheBuffer<T1>> buffer1_;
  std::function<void()> on_fuse_;
};

template <typename T0, typename T1, typename T2>
class DataVisitor<T0, T1, T2> {
 public:
  DataVisitor(std::string_view ch0, std::string_view ch1, std::string_view ch2, std::size_t depth,
              std::function<void()> on_fuse)
      : buffer0_(std::make_unique<base::CacheBuffer<T0>>(depth)),
        buffer1_(std::make_unique<base::CacheBuffer<T1>>(depth)),
        buffer2_(std::make_unique<base::CacheBuffer<T2>>(depth)),
        on_fuse_(std::move(on_fuse)) {
    const auto fused = [this] {
      if (!buffer0_->empty() && !buffer1_->empty() && !buffer2_->empty()) {
        on_fuse_();
      }
    };
    DataDispatcher::instance().add_buffer(channel_id_for(ch0), buffer0_.get(), fused, this);
    DataDispatcher::instance().add_buffer(channel_id_for(ch1), buffer1_.get(), fused, this);
    DataDispatcher::instance().add_buffer(channel_id_for(ch2), buffer2_.get(), fused, this);
  }

  ~DataVisitor() { DataDispatcher::instance().remove_owner(this); }

  DataVisitor(const DataVisitor&) = delete;
  DataVisitor& operator=(const DataVisitor&) = delete;

  T0* try_fetch_0() { return buffer0_->try_fetch(); }
  T1* try_fetch_1() { return buffer1_->try_fetch(); }
  T2* try_fetch_2() { return buffer2_->try_fetch(); }

 private:
  std::unique_ptr<base::CacheBuffer<T0>> buffer0_;
  std::unique_ptr<base::CacheBuffer<T1>> buffer1_;
  std::unique_ptr<base::CacheBuffer<T2>> buffer2_;
  std::function<void()> on_fuse_;
};

template <typename T0, typename T1, typename T2, typename T3>
class DataVisitor<T0, T1, T2, T3> {
 public:
  DataVisitor(std::string_view ch0, std::string_view ch1, std::string_view ch2,
              std::string_view ch3, std::size_t depth, std::function<void()> on_fuse)
      : buffer0_(std::make_unique<base::CacheBuffer<T0>>(depth)),
        buffer1_(std::make_unique<base::CacheBuffer<T1>>(depth)),
        buffer2_(std::make_unique<base::CacheBuffer<T2>>(depth)),
        buffer3_(std::make_unique<base::CacheBuffer<T3>>(depth)),
        on_fuse_(std::move(on_fuse)) {
    const auto fused = [this] {
      if (!buffer0_->empty() && !buffer1_->empty() && !buffer2_->empty() && !buffer3_->empty()) {
        on_fuse_();
      }
    };
    DataDispatcher::instance().add_buffer(channel_id_for(ch0), buffer0_.get(), fused, this);
    DataDispatcher::instance().add_buffer(channel_id_for(ch1), buffer1_.get(), fused, this);
    DataDispatcher::instance().add_buffer(channel_id_for(ch2), buffer2_.get(), fused, this);
    DataDispatcher::instance().add_buffer(channel_id_for(ch3), buffer3_.get(), fused, this);
  }

  ~DataVisitor() { DataDispatcher::instance().remove_owner(this); }

  DataVisitor(const DataVisitor&) = delete;
  DataVisitor& operator=(const DataVisitor&) = delete;

  T0* try_fetch_0() { return buffer0_->try_fetch(); }
  T1* try_fetch_1() { return buffer1_->try_fetch(); }
  T2* try_fetch_2() { return buffer2_->try_fetch(); }
  T3* try_fetch_3() { return buffer3_->try_fetch(); }

 private:
  std::unique_ptr<base::CacheBuffer<T0>> buffer0_;
  std::unique_ptr<base::CacheBuffer<T1>> buffer1_;
  std::unique_ptr<base::CacheBuffer<T2>> buffer2_;
  std::unique_ptr<base::CacheBuffer<T3>> buffer3_;
  std::function<void()> on_fuse_;
};

}  // namespace tianshu::core
