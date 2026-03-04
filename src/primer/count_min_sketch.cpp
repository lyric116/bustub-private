//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// count_min_sketch.cpp
//
// Identification: src/primer/count_min_sketch.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "primer/count_min_sketch.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace bustub {

/**
 * Constructor for the count-min sketch.
 *
 * @param width The width of the sketch matrix.
 * @param depth The depth of the sketch matrix.
 * @throws std::invalid_argument if width or depth are zero.
 */
template <typename KeyType>
CountMinSketch<KeyType>::CountMinSketch(uint32_t width, uint32_t depth)
    : width_(width), depth_(depth), sketch_(static_cast<size_t>(width) * depth) {
  if (width_ == 0 || depth_ == 0) {
    throw std::invalid_argument("CountMinSketch width and depth must be greater than zero.");
  }
  for (auto &counter : sketch_) {
    counter.store(0, std::memory_order_relaxed);
  }

  /** @spring2026 PLEASE DO NOT MODIFY THE FOLLOWING */
  // Initialize seeded hash functions
  hash_functions_.reserve(depth_);
  for (size_t i = 0; i < depth_; i++) {
    hash_functions_.push_back(this->HashFunction(i));
  }
}

template <typename KeyType>
CountMinSketch<KeyType>::CountMinSketch(CountMinSketch &&other) noexcept
    : width_(other.width_), depth_(other.depth_), sketch_(std::move(other.sketch_)) {
  {
    auto guard = std::lock_guard<std::mutex>(other.top_k_latch_);
    top_k_initialized_ = other.top_k_initialized_;
    initial_k_ = other.initial_k_;
    item_set_ = std::move(other.item_set_);
    top_k_heap_ = std::move(other.top_k_heap_);
    other.top_k_initialized_ = false;
    other.initial_k_ = 0;
  }

  hash_functions_.reserve(depth_);
  for (size_t i = 0; i < depth_; i++) {
    hash_functions_.push_back(this->HashFunction(i));
  }
  other.width_ = 0;
  other.depth_ = 0;
  other.hash_functions_.clear();
}

template <typename KeyType>
auto CountMinSketch<KeyType>::operator=(CountMinSketch &&other) noexcept -> CountMinSketch & {
  if (this == &other) {
    return *this;
  }

  auto guard = std::scoped_lock(top_k_latch_, other.top_k_latch_);
  width_ = other.width_;
  depth_ = other.depth_;
  sketch_ = std::move(other.sketch_);
  top_k_initialized_ = other.top_k_initialized_;
  initial_k_ = other.initial_k_;
  item_set_ = std::move(other.item_set_);
  top_k_heap_ = std::move(other.top_k_heap_);

  hash_functions_.clear();
  hash_functions_.reserve(depth_);
  for (size_t i = 0; i < depth_; i++) {
    hash_functions_.push_back(this->HashFunction(i));
  }

  other.width_ = 0;
  other.depth_ = 0;
  other.top_k_initialized_ = false;
  other.initial_k_ = 0;
  other.hash_functions_.clear();

  return *this;
}

template <typename KeyType>
void CountMinSketch<KeyType>::Insert(const KeyType &item) {
  for (size_t row = 0; row < depth_; row++) {
    auto col = hash_functions_[row](item);
    auto idx = row * width_ + col;
    sketch_[idx].fetch_add(1, std::memory_order_relaxed);
  }

  auto should_track_top_k = false;
  {
    std::lock_guard<std::mutex> guard(top_k_latch_);
    should_track_top_k = top_k_initialized_ && initial_k_ > 0;
  }
  if (!should_track_top_k) {
    return;
  }

  {
    std::lock_guard<std::mutex> guard(top_k_latch_);
    item_set_.insert(item);

    std::vector<std::pair<KeyType, uint32_t>> tracked_items;
    tracked_items.reserve(item_set_.size());
    for (const auto &candidate : item_set_) {
      tracked_items.emplace_back(candidate, Count(candidate));
    }

    std::stable_sort(tracked_items.begin(), tracked_items.end(),
                     [](const auto &lhs, const auto &rhs) { return lhs.second > rhs.second; });
    if (tracked_items.size() > initial_k_) {
      tracked_items.resize(initial_k_);
    }

    top_k_heap_ = std::move(tracked_items);
  }
}

template <typename KeyType>
void CountMinSketch<KeyType>::Merge(const CountMinSketch<KeyType> &other) {
  if (width_ != other.width_ || depth_ != other.depth_) {
    throw std::invalid_argument("Incompatible CountMinSketch dimensions for merge.");
  }
  for (size_t idx = 0; idx < sketch_.size(); idx++) {
    auto other_count = other.sketch_[idx].load(std::memory_order_relaxed);
    sketch_[idx].fetch_add(other_count, std::memory_order_relaxed);
  }

  auto guard = std::scoped_lock(top_k_latch_, other.top_k_latch_);
  if (!top_k_initialized_ && other.top_k_initialized_) {
    top_k_initialized_ = true;
    initial_k_ = other.initial_k_;
  }
  item_set_.insert(other.item_set_.begin(), other.item_set_.end());

  if (top_k_initialized_ && initial_k_ > 0) {
    std::vector<std::pair<KeyType, uint32_t>> tracked_items;
    tracked_items.reserve(item_set_.size());
    for (const auto &candidate : item_set_) {
      tracked_items.emplace_back(candidate, Count(candidate));
    }
    std::stable_sort(tracked_items.begin(), tracked_items.end(),
                     [](const auto &lhs, const auto &rhs) { return lhs.second > rhs.second; });
    if (tracked_items.size() > initial_k_) {
      tracked_items.resize(initial_k_);
    }
    top_k_heap_ = std::move(tracked_items);
  }
}

template <typename KeyType>
auto CountMinSketch<KeyType>::Count(const KeyType &item) const -> uint32_t {
  if (depth_ == 0 || width_ == 0) {
    return 0;
  }

  auto min_count = std::numeric_limits<uint32_t>::max();
  for (size_t row = 0; row < depth_; row++) {
    auto col = hash_functions_[row](item);
    auto idx = row * width_ + col;
    auto count = sketch_[idx].load(std::memory_order_relaxed);
    min_count = std::min(min_count, count);
  }
  return min_count;
}

template <typename KeyType>
void CountMinSketch<KeyType>::Clear() {
  for (auto &counter : sketch_) {
    counter.store(0, std::memory_order_relaxed);
  }

  std::lock_guard<std::mutex> guard(top_k_latch_);
  item_set_.clear();
  top_k_heap_.clear();
  top_k_initialized_ = false;
  initial_k_ = 0;
}

template <typename KeyType>
auto CountMinSketch<KeyType>::TopK(uint16_t k, const std::vector<KeyType> &candidates)
    -> std::vector<std::pair<KeyType, uint32_t>> {
  uint16_t capped_k = k;
  {
    std::lock_guard<std::mutex> guard(top_k_latch_);
    if (!top_k_initialized_) {
      top_k_initialized_ = true;
      initial_k_ = k;
    }
    capped_k = std::min(k, initial_k_);

    for (const auto &candidate : candidates) {
      item_set_.insert(candidate);
    }

    if (top_k_initialized_ && initial_k_ > 0) {
      std::vector<std::pair<KeyType, uint32_t>> tracked_items;
      tracked_items.reserve(item_set_.size());
      for (const auto &candidate : item_set_) {
        tracked_items.emplace_back(candidate, Count(candidate));
      }
      std::stable_sort(tracked_items.begin(), tracked_items.end(),
                       [](const auto &lhs, const auto &rhs) { return lhs.second > rhs.second; });
      if (tracked_items.size() > initial_k_) {
        tracked_items.resize(initial_k_);
      }
      top_k_heap_ = std::move(tracked_items);
    }
  }

  std::vector<std::pair<KeyType, uint32_t>> counts;
  counts.reserve(candidates.size());
  for (const auto &candidate : candidates) {
    counts.emplace_back(candidate, Count(candidate));
  }
  std::stable_sort(counts.begin(), counts.end(),
                   [](const auto &lhs, const auto &rhs) { return lhs.second > rhs.second; });

  auto top_k = static_cast<size_t>(capped_k);
  if (top_k < counts.size()) {
    counts.resize(top_k);
  }
  return counts;
}

// Explicit instantiations for all types used in tests
template class CountMinSketch<std::string>;
template class CountMinSketch<int64_t>;  // For int64_t tests
template class CountMinSketch<int>;      // This covers both int and int32_t
}  // namespace bustub
