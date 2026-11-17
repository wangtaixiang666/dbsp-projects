//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// extendible_hash_table.cpp
//
// Identification: src/container/hash/extendible_hash_table.cpp
//
// Copyright (c) 2022, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <cassert>
#include <cstdlib>
#include <functional>
#include <list>
#include <memory>
#include <utility>

#include "container/hash/extendible_hash_table.h"
#include "storage/page/page.h"

namespace bustub {

template <typename K, typename V>
ExtendibleHashTable<K, V>::ExtendibleHashTable(size_t bucket_size) : bucket_size_(bucket_size) {
  // The directory starts with exactly one bucket of local depth 0 referenced by index 0.
  // Leaving dir_ empty here would make every GetLocalDepth(0)/Find() dereference nothing.
  dir_.push_back(std::make_shared<Bucket>(bucket_size_, 0));
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::IndexOf(const K &key) -> size_t {
  const size_t mask = global_depth_ == 0 ? size_t{0} : ((size_t{1} << global_depth_) - 1);
  return std::hash<K>()(key) & mask;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetGlobalDepth() const -> int {
  std::scoped_lock<std::mutex> lock(latch_);
  return GetGlobalDepthInternal();
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetGlobalDepthInternal() const -> int {
  return global_depth_;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetLocalDepth(int dir_index) const -> int {
  std::scoped_lock<std::mutex> lock(latch_);
  return GetLocalDepthInternal(dir_index);
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetLocalDepthInternal(int dir_index) const -> int {
  return dir_[dir_index]->GetDepth();
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetNumBuckets() const -> int {
  std::scoped_lock<std::mutex> lock(latch_);
  return GetNumBucketsInternal();
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetNumBucketsInternal() const -> int {
  return num_buckets_;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Find(const K &key, V &value) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);
  return dir_[IndexOf(key)]->Find(key, value);
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Remove(const K &key) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);
  return dir_[IndexOf(key)]->Remove(key);
}

template <typename K, typename V>
void ExtendibleHashTable<K, V>::Insert(const K &key, const V &value) {
  std::scoped_lock<std::mutex> lock(latch_);

  // After a split the key may still land in a full bucket (all keys sharing the same
  // low bits), so keep splitting until the insert finally succeeds.
  while (true) {
    auto bucket = dir_[IndexOf(key)];

    // Either the key was inserted, or its value was replaced in place.
    if (bucket->Insert(key, value)) {
      return;
    }

    // The bucket is full and the key is new, so it must be split.
    if (bucket->GetDepth() == GetGlobalDepthInternal()) {
      // Local depth == global depth: the directory has to double first, otherwise the
      // extra bit of the new local depth could not be addressed.
      const size_t old_size = dir_.size();
      dir_.reserve(old_size * 2);
      for (size_t i = 0; i < old_size; i++) {
        dir_.push_back(dir_[i]);
      }
      global_depth_++;
    }

    // Split off a new bucket: both halves get local depth = old depth + 1, and the bit at
    // position (new_depth - 1) decides which half a directory slot belongs to.
    const int new_depth = bucket->GetDepth() + 1;
    auto new_bucket = std::make_shared<Bucket>(bucket_size_, new_depth);
    bucket->IncrementDepth();

    const size_t split_bit = size_t{1} << (new_depth - 1);
    for (size_t i = 0; i < dir_.size(); i++) {
      if (dir_[i] == bucket && (i & split_bit) != 0) {
        dir_[i] = new_bucket;
      }
    }
    num_buckets_++;

    // Push every key-value pair back through the (now deeper) directory.
    RedistributeBucket(bucket);
  }
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::RedistributeBucket(std::shared_ptr<Bucket> bucket) -> void {
  auto items = std::move(bucket->GetItems());
  bucket->GetItems().clear();
  for (auto &item : items) {
    // push_back on purpose: the target bucket may temporarily hold more than bucket_size_
    // entries, which is exactly what triggers another split on the next Insert attempt.
    dir_[IndexOf(item.first)]->GetItems().push_back(std::move(item));
  }
}

//===--------------------------------------------------------------------===//
// Bucket
//===--------------------------------------------------------------------===//
template <typename K, typename V>
ExtendibleHashTable<K, V>::Bucket::Bucket(size_t array_size, int depth) : size_(array_size), depth_(depth) {}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Bucket::Find(const K &key, V &value) -> bool {
  for (const auto &item : list_) {
    if (item.first == key) {
      value = item.second;
      return true;
    }
  }
  return false;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Bucket::Remove(const K &key) -> bool {
  for (auto it = list_.begin(); it != list_.end(); ++it) {
    if (it->first == key) {
      list_.erase(it);
      return true;
    }
  }
  return false;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Bucket::Insert(const K &key, const V &value) -> bool {
  for (auto &item : list_) {
    if (item.first == key) {
      item.second = value;
      return true;
    }
  }
  // Use >= instead of IsFull() (which tests ==): a bucket can hold more than size_ entries
  // right after a redistribution that failed to separate the colliding keys.
  if (list_.size() >= size_) {
    return false;
  }
  list_.emplace_back(key, value);
  return true;
}

template class ExtendibleHashTable<page_id_t, Page *>;
template class ExtendibleHashTable<Page *, std::list<Page *>::iterator>;
template class ExtendibleHashTable<int, int>;
// test purpose
template class ExtendibleHashTable<int, std::string>;
template class ExtendibleHashTable<int, std::list<int>::iterator>;

}  // namespace bustub
