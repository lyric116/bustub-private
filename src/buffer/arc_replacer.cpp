// :bustub-keep-private:
//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// arc_replacer.cpp
//
// Identification: src/buffer/arc_replacer.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/arc_replacer.h"

#include <optional>

#include "common/config.h"

namespace bustub {

/**
 *
 * TODO(P1): Add implementation
 *
 * @brief a new ArcReplacer, with lists initialized to be empty and target size to 0
 * @param num_frames the maximum number of frames the ArcReplacer will be required to cache
 */
ArcReplacer::ArcReplacer(size_t num_frames) : replacer_size_(num_frames) {
  alive_map_.reserve(num_frames);
  ghost_map_.reserve(num_frames * 2);
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Performs the Replace operation as described by the writeup
 * that evicts from either mfu_ or mru_ into its corresponding ghost list
 * according to balancing policy.
 *
 * If you wish to refer to the original ARC paper, please note that there are
 * two changes in our implementation:
 * 1. When the size of mru_ equals the target size, we don't check
 * the last access as the paper did when deciding which list to evict from.
 * This is fine since the original decision is stated to be arbitrary.
 * 2. Entries that are not evictable are skipped. If all entries from the desired side
 * (mru_ / mfu_) are pinned, we instead try victimize the other side (mfu_ / mru_),
 * and move it to its corresponding ghost list (mfu_ghost_ / mru_ghost_).
 *
 * @return frame id of the evicted frame, or std::nullopt if cannot evict
 */
auto ArcReplacer::Evict() -> std::optional<frame_id_t> {
  std::scoped_lock guard(latch_);

  if (curr_size_ == 0) {
    return std::nullopt;
  }

  const bool evict_mru_first = mru_.size() >= mru_target_size_;
  auto victim = evict_mru_first ? TryEvictFromMru() : TryEvictFromMfu();
  if (victim.has_value()) {
    return victim;
  }

  return evict_mru_first ? TryEvictFromMfu() : TryEvictFromMru();
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Record access to a frame, adjusting ARC bookkeeping accordingly
 * by bring the accessed page to the front of mfu_ if it exists in any of the lists
 * or the front of mru_ if it does not.
 *
 * Performs the operations EXCEPT REPLACE described in original paper, which is
 * handled by `Evict()`.
 *
 * Consider the following four cases, handle accordingly:
 * 1. Access hits mru_ or mfu_
 * 2/3. Access hits mru_ghost_ / mfu_ghost_
 * 4. Access misses all the lists
 *
 * This routine performs all changes to the four lists as preperation
 * for `Evict()` to simply find and evict a victim into ghost lists.
 *
 * Note that frame_id is used as identifier for alive pages and
 * page_id is used as identifier for the ghost pages, since page_id is
 * the unique identifier to the page after it's dead.
 * Using page_id for alive pages should be the same since it's one to one mapping,
 * but using frame_id is slightly more intuitive.
 *
 * @param frame_id id of frame that received a new access.
 * @param page_id id of page that is mapped to the frame.
 * @param access_type type of access that was received. This parameter is only needed for
 * leaderboard tests.
 */
void ArcReplacer::RecordAccess(frame_id_t frame_id, page_id_t page_id, [[maybe_unused]] AccessType access_type) {
  std::scoped_lock guard(latch_);

  if (!IsValidFrameId(frame_id)) {
    throw std::out_of_range("ArcReplacer::RecordAccess invalid frame_id");
  }

  const bool is_scan = access_type == AccessType::Scan;

  auto alive_it = alive_map_.find(frame_id);
  if (alive_it != alive_map_.end()) {
    if (alive_it->second.page_id_ != page_id) {
      if (alive_it->second.evictable_) {
        curr_size_ -= 1;
      }
      if (alive_it->second.arc_status_ == ArcStatus::MRU) {
        mru_.erase(alive_it->second.iter_);
      } else {
        mfu_.erase(alive_it->second.iter_);
      }
      alive_map_.erase(alive_it);
      alive_it = alive_map_.end();
    } else {
      if (is_scan) {
        if (alive_it->second.arc_status_ == ArcStatus::MFU) {
          mfu_.erase(alive_it->second.iter_);
          mru_.push_back(frame_id);
          alive_it->second.arc_status_ = ArcStatus::MRU;
          alive_it->second.iter_ = std::prev(mru_.end());
        } else {
          mru_.erase(alive_it->second.iter_);
          mru_.push_back(frame_id);
          alive_it->second.iter_ = std::prev(mru_.end());
        }
      } else {
        MoveAliveToMfu(frame_id);
      }
      return;
    }
  }

  auto ghost_it = ghost_map_.find(page_id);
  if (ghost_it != ghost_map_.end()) {
    if (ghost_it->second.arc_status_ == ArcStatus::MRU_GHOST) {
      const size_t b1_size = mru_ghost_.size();
      const size_t b2_size = mfu_ghost_.size();
      const size_t delta = std::max(static_cast<size_t>(1), b2_size / std::max(static_cast<size_t>(1), b1_size));
      mru_target_size_ = std::min(replacer_size_, mru_target_size_ + delta);
    } else {
      const size_t b1_size = mru_ghost_.size();
      const size_t b2_size = mfu_ghost_.size();
      const size_t delta = std::max(static_cast<size_t>(1), b1_size / std::max(static_cast<size_t>(1), b2_size));
      mru_target_size_ = mru_target_size_ > delta ? mru_target_size_ - delta : 0;
    }
    RemoveGhostEntry(page_id);
    if (is_scan) {
      InsertToMru(frame_id, page_id);
    } else {
      InsertToMfu(frame_id, page_id);
    }
    return;
  }

  const size_t left_size = mru_.size() + mru_ghost_.size();
  if (left_size == replacer_size_) {
    if (mru_.size() < replacer_size_ && !mru_ghost_.empty()) {
      const auto old = mru_ghost_.front();
      mru_ghost_.pop_front();
      ghost_map_.erase(old);
    }
  } else if (left_size < replacer_size_) {
    const size_t total_size = mru_.size() + mfu_.size() + mru_ghost_.size() + mfu_ghost_.size();
    if (total_size >= replacer_size_ && total_size == 2 * replacer_size_ && !mfu_ghost_.empty()) {
      const auto old = mfu_ghost_.back();
      mfu_ghost_.pop_back();
      ghost_map_.erase(old);
    }
  }

  InsertToMru(frame_id, page_id);
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Toggle whether a frame is evictable or non-evictable. This function also
 * controls replacer's size. Note that size is equal to number of evictable entries.
 *
 * If a frame was previously evictable and is to be set to non-evictable, then size should
 * decrement. If a frame was previously non-evictable and is to be set to evictable,
 * then size should increment.
 *
 * If frame id is invalid, throw an exception or abort the process.
 *
 * For other scenarios, this function should terminate without modifying anything.
 *
 * @param frame_id id of frame whose 'evictable' status will be modified
 * @param set_evictable whether the given frame is evictable or not
 */
void ArcReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::scoped_lock guard(latch_);

  if (!IsValidFrameId(frame_id)) {
    throw std::out_of_range("ArcReplacer::SetEvictable invalid frame_id");
  }

  auto it = alive_map_.find(frame_id);
  if (it == alive_map_.end()) {
    return;
  }

  if (it->second.evictable_ == set_evictable) {
    return;
  }

  it->second.evictable_ = set_evictable;
  if (set_evictable) {
    curr_size_ += 1;
  } else {
    curr_size_ -= 1;
  }
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Remove an evictable frame from replacer.
 * This function should also decrement replacer's size if removal is successful.
 *
 * Note that this is different from evicting a frame, which always remove the frame
 * decided by the ARC algorithm.
 *
 * If Remove is called on a non-evictable frame, throw an exception or abort the
 * process.
 *
 * If specified frame is not found, directly return from this function.
 *
 * @param frame_id id of frame to be removed
 */
void ArcReplacer::Remove(frame_id_t frame_id) {
  std::scoped_lock guard(latch_);

  auto it = alive_map_.find(frame_id);
  if (it == alive_map_.end()) {
    return;
  }

  if (!it->second.evictable_) {
    throw std::runtime_error("ArcReplacer::Remove cannot remove non-evictable frame");
  }

  if (it->second.arc_status_ == ArcStatus::MRU) {
    mru_.erase(it->second.iter_);
  } else {
    mfu_.erase(it->second.iter_);
  }
  curr_size_ -= 1;
  alive_map_.erase(it);
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Return replacer's size, which tracks the number of evictable frames.
 *
 * @return size_t
 */
auto ArcReplacer::Size() -> size_t {
  std::scoped_lock guard(latch_);
  return curr_size_;
}

auto ArcReplacer::TryEvictFromMru() -> std::optional<frame_id_t> {
  for (auto it = mru_.begin(); it != mru_.end();) {
    const auto fid = *it;
    auto alive_it = alive_map_.find(fid);
    if (alive_it == alive_map_.end() || alive_it->second.arc_status_ != ArcStatus::MRU) {
      it = mru_.erase(it);
      continue;
    }
    if (!alive_it->second.evictable_) {
      ++it;
      continue;
    }

    const auto page_id = alive_it->second.page_id_;
    it = mru_.erase(it);
    alive_map_.erase(alive_it);
    curr_size_ -= 1;
    InsertMruGhost(page_id);
    return fid;
  }
  return std::nullopt;
}

auto ArcReplacer::TryEvictFromMfu() -> std::optional<frame_id_t> {
  auto it = mfu_.end();
  while (it != mfu_.begin()) {
    --it;
    const auto fid = *it;
    auto alive_it = alive_map_.find(fid);
    if (alive_it == alive_map_.end() || alive_it->second.arc_status_ != ArcStatus::MFU) {
      it = mfu_.erase(it);
      continue;
    }
    if (!alive_it->second.evictable_) {
      continue;
    }

    const auto page_id = alive_it->second.page_id_;
    mfu_.erase(it);
    alive_map_.erase(alive_it);
    curr_size_ -= 1;
    InsertMfuGhost(page_id);
    return fid;
  }
  return std::nullopt;
}

void ArcReplacer::RemoveGhostEntry(page_id_t page_id) {
  auto it = ghost_map_.find(page_id);
  if (it == ghost_map_.end()) {
    return;
  }
  if (it->second.arc_status_ == ArcStatus::MRU_GHOST) {
    mru_ghost_.erase(it->second.iter_);
  } else {
    mfu_ghost_.erase(it->second.iter_);
  }
  ghost_map_.erase(it);
}

void ArcReplacer::InsertMruGhost(page_id_t page_id) {
  RemoveGhostEntry(page_id);
  mru_ghost_.push_back(page_id);
  ghost_map_[page_id] = GhostEntry{ArcStatus::MRU_GHOST, std::prev(mru_ghost_.end())};
}

void ArcReplacer::InsertMfuGhost(page_id_t page_id) {
  RemoveGhostEntry(page_id);
  mfu_ghost_.push_front(page_id);
  ghost_map_[page_id] = GhostEntry{ArcStatus::MFU_GHOST, mfu_ghost_.begin()};
}

void ArcReplacer::InsertToMru(frame_id_t frame_id, page_id_t page_id) {
  mru_.push_back(frame_id);
  alive_map_[frame_id] = AliveEntry{page_id, false, ArcStatus::MRU, std::prev(mru_.end())};
}

void ArcReplacer::InsertToMfu(frame_id_t frame_id, page_id_t page_id) {
  mfu_.push_front(frame_id);
  alive_map_[frame_id] = AliveEntry{page_id, false, ArcStatus::MFU, mfu_.begin()};
}

void ArcReplacer::MoveAliveToMfu(frame_id_t frame_id) {
  auto it = alive_map_.find(frame_id);
  if (it == alive_map_.end()) {
    return;
  }

  if (it->second.arc_status_ == ArcStatus::MRU) {
    mru_.erase(it->second.iter_);
  } else {
    mfu_.erase(it->second.iter_);
  }
  mfu_.push_front(frame_id);
  it->second.arc_status_ = ArcStatus::MFU;
  it->second.iter_ = mfu_.begin();
}

auto ArcReplacer::IsValidFrameId(frame_id_t frame_id) const -> bool { return frame_id >= 0; }

}  // namespace bustub
