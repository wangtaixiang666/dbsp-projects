//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lru_k_replacer.cpp
//
// Identification: src/buffer/lru_k_replacer.cpp
//
// Copyright (c) 2015-2022, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/lru_k_replacer.h"

namespace bustub {

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k) : replacer_size_(num_frames), k_(k) {}

auto LRUKReplacer::Evict(frame_id_t *frame_id) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);

  // Among all evictable frames pick the one with the largest backward k-distance.
  // Frames accessed fewer than k times have an infinite distance, so they always win.
  // Ties inside a tier are broken by the earliest timestamp (plain LRU); timestamps are
  // globally unique, so the choice is deterministic.
  bool has_victim = false;
  bool victim_infinite = false;
  size_t victim_front_ts = 0;
  frame_id_t victim = INVALID_PAGE_ID;

  for (const auto &[fid, entry] : node_store_) {
    if (!entry.evictable_ || entry.history_.empty()) {
      continue;
    }

    const bool infinite = entry.history_.size() < k_;
    const size_t front_ts = entry.history_.front();

    // Better victim if: no victim yet, or this one is infinite while the current is not,
    // or they sit in the same tier and this one was touched earlier.
    const bool better =
        !has_victim || (infinite && !victim_infinite) || (infinite == victim_infinite && front_ts < victim_front_ts);
    if (better) {
      has_victim = true;
      victim_infinite = infinite;
      victim_front_ts = front_ts;
      victim = fid;
    }
  }

  if (!has_victim) {
    return false;
  }

  // An evicted frame must not carry its access history into its next life.
  node_store_.erase(victim);
  curr_size_--;
  *frame_id = victim;
  return true;
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id) {
  std::scoped_lock<std::mutex> lock(latch_);
  BUSTUB_ASSERT(frame_id >= 0 && static_cast<size_t>(frame_id) < replacer_size_, "invalid frame id");

  current_timestamp_++;

  auto &entry = node_store_[frame_id];
  entry.history_.push_back(current_timestamp_);

  // Only the k most recent accesses matter for the backward k-distance.
  if (entry.history_.size() > k_) {
    entry.history_.pop_front();
  }
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::scoped_lock<std::mutex> lock(latch_);
  BUSTUB_ASSERT(frame_id >= 0 && static_cast<size_t>(frame_id) < replacer_size_, "invalid frame id");

  auto it = node_store_.find(frame_id);
  if (it == node_store_.end() || it->second.evictable_ == set_evictable) {
    // Unknown frame, or already in the requested state: nothing to do.
    return;
  }

  it->second.evictable_ = set_evictable;
  if (set_evictable) {
    curr_size_++;
  } else {
    curr_size_--;
  }
}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  std::scoped_lock<std::mutex> lock(latch_);
  BUSTUB_ASSERT(frame_id >= 0 && static_cast<size_t>(frame_id) < replacer_size_, "invalid frame id");

  auto it = node_store_.find(frame_id);
  if (it == node_store_.end()) {
    return;
  }

  BUSTUB_ASSERT(it->second.evictable_, "cannot remove a non-evictable frame");

  node_store_.erase(it);
  curr_size_--;
}

auto LRUKReplacer::Size() -> size_t {
  std::scoped_lock<std::mutex> lock(latch_);
  return curr_size_;
}

}  // namespace bustub
