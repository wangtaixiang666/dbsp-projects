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

  // A frame with fewer than k accesses has an infinite backward k-distance and is
  // therefore always preferred over a frame that already has k accesses. Among the
  // infinite ones the frame with the earliest access timestamp wins; among the
  // others the largest backward k-distance (i.e. the smallest k-th previous
  // timestamp, which is exactly the oldest retained timestamp) wins.
  bool found = false;
  frame_id_t victim = -1;
  bool victim_infinite = false;
  size_t victim_key = 0;

  for (auto &entry : frame_status_) {
    const frame_id_t candidate = entry.first;
    FrameStatus &status = entry.second;
    if (!status.evictable_) {
      continue;
    }
    const bool infinite = status.history_.size() < k_;
    const size_t key = status.history_.front();

    bool better = false;
    if (!found) {
      better = true;
    } else if (infinite != victim_infinite) {
      better = infinite;
    } else if (key != victim_key) {
      better = key < victim_key;
    } else {
      // Deterministic tie-break so that repeated runs behave identically.
      better = candidate < victim;
    }

    if (better) {
      found = true;
      victim = candidate;
      victim_infinite = infinite;
      victim_key = key;
    }
  }

  if (!found) {
    return false;
  }

  frame_status_.erase(victim);
  --curr_size_;
  if (frame_id != nullptr) {
    *frame_id = victim;
  }
  return true;
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id) {
  std::scoped_lock<std::mutex> lock(latch_);
  BUSTUB_ASSERT(frame_id >= 0 && static_cast<size_t>(frame_id) < replacer_size_,
                "frame id out of range in LRUKReplacer::RecordAccess");

  FrameStatus &status = frame_status_[frame_id];
  status.history_.push_back(current_timestamp_++);
  if (status.history_.size() > k_) {
    // Only the k most recent accesses influence the backward k-distance.
    status.history_.pop_front();
  }
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::scoped_lock<std::mutex> lock(latch_);
  BUSTUB_ASSERT(frame_id >= 0 && static_cast<size_t>(frame_id) < replacer_size_,
                "frame id out of range in LRUKReplacer::SetEvictable");

  auto it = frame_status_.find(frame_id);
  if (it == frame_status_.end()) {
    // Unknown frame (never accessed): nothing to toggle.
    return;
  }
  if (it->second.evictable_ == set_evictable) {
    return;
  }
  it->second.evictable_ = set_evictable;
  if (set_evictable) {
    ++curr_size_;
  } else {
    --curr_size_;
  }
}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  std::scoped_lock<std::mutex> lock(latch_);
  BUSTUB_ASSERT(frame_id >= 0 && static_cast<size_t>(frame_id) < replacer_size_,
                "frame id out of range in LRUKReplacer::Remove");

  auto it = frame_status_.find(frame_id);
  if (it == frame_status_.end()) {
    return;
  }
  if (!it->second.evictable_) {
    // A pinned frame must not be discarded by the caller; leave it untouched.
    return;
  }
  frame_status_.erase(it);
  --curr_size_;
}

auto LRUKReplacer::Size() -> size_t {
  std::scoped_lock<std::mutex> lock(latch_);
  return curr_size_;
}

}  // namespace bustub
