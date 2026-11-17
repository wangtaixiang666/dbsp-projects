//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager_instance.cpp
//
// Identification: src/buffer/buffer_pool_manager.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/buffer_pool_manager_instance.h"

#include "common/exception.h"
#include "common/macros.h"

namespace bustub {

BufferPoolManagerInstance::BufferPoolManagerInstance(size_t pool_size, DiskManager *disk_manager, size_t replacer_k,
                                                     LogManager *log_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager), log_manager_(log_manager) {
  // we allocate a consecutive memory space for the buffer pool
  pages_ = new Page[pool_size_];
  page_table_ = new ExtendibleHashTable<page_id_t, frame_id_t>(bucket_size_);
  replacer_ = new LRUKReplacer(pool_size, replacer_k);

  // Initially, every page is in the free list.
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.emplace_back(static_cast<int>(i));
  }
}

BufferPoolManagerInstance::~BufferPoolManagerInstance() {
  delete[] pages_;
  delete page_table_;
  delete replacer_;
}

auto BufferPoolManagerInstance::NewPgImp(page_id_t *page_id) -> Page * {
  std::scoped_lock<std::mutex> lock(latch_);

  // Prefer a frame that has never held a page; only fall back to eviction when the
  // free list is drained. Evict() returns false when every frame is still pinned.
  frame_id_t frame_id = -1;
  if (!free_list_.empty()) {
    frame_id = free_list_.front();
    free_list_.pop_front();
  } else if (!replacer_->Evict(&frame_id)) {
    return nullptr;
  }

  auto *page = &pages_[frame_id];

  // Whatever used to live in this frame must reach the disk before we overwrite it.
  if (page->IsDirty()) {
    disk_manager_->WritePage(page->GetPageId(), page->GetData());
  }
  if (page->GetPageId() != INVALID_PAGE_ID) {
    page_table_->Remove(page->GetPageId());
  }

  // A brand-new page: untouched memory, pinned once, clean.
  page->page_id_ = AllocatePage();
  page->pin_count_ = 1;
  page->is_dirty_ = false;
  page->ResetMemory();

  page_table_->Insert(page->page_id_, frame_id);
  // Record the access first, then pin: SetEvictable(false) is what keeps the frame from
  // being picked as a victim before this caller unpins it.
  replacer_->RecordAccess(frame_id);
  replacer_->SetEvictable(frame_id, false);

  *page_id = page->page_id_;
  return page;
}

auto BufferPoolManagerInstance::FetchPgImp(page_id_t page_id) -> Page * {
  std::scoped_lock<std::mutex> lock(latch_);

  frame_id_t frame_id = -1;
  if (page_table_->Find(page_id, frame_id)) {
    // Cache hit: just pin it again and refresh its access history.
    auto *page = &pages_[frame_id];
    page->pin_count_++;
    replacer_->RecordAccess(frame_id);
    replacer_->SetEvictable(frame_id, false);
    return page;
  }

  if (!free_list_.empty()) {
    frame_id = free_list_.front();
    free_list_.pop_front();
  } else if (!replacer_->Evict(&frame_id)) {
    return nullptr;
  }

  auto *page = &pages_[frame_id];
  if (page->IsDirty()) {
    disk_manager_->WritePage(page->GetPageId(), page->GetData());
  }
  if (page->GetPageId() != INVALID_PAGE_ID) {
    page_table_->Remove(page->GetPageId());
  }

  disk_manager_->ReadPage(page_id, page->GetData());
  page->page_id_ = page_id;
  page->pin_count_ = 1;
  page->is_dirty_ = false;

  page_table_->Insert(page_id, frame_id);
  replacer_->RecordAccess(frame_id);
  replacer_->SetEvictable(frame_id, false);
  return page;
}

auto BufferPoolManagerInstance::UnpinPgImp(page_id_t page_id, bool is_dirty) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);

  frame_id_t frame_id = -1;
  if (!page_table_->Find(page_id, frame_id)) {
    return false;
  }

  auto *page = &pages_[frame_id];
  if (page->pin_count_ <= 0) {
    return false;
  }

  page->pin_count_--;
  // The dirty bit is sticky: unpinning with is_dirty = false never clears it, otherwise a
  // caller that modified the page and then unpinned with false would silently lose writes.
  if (is_dirty) {
    page->is_dirty_ = true;
  }
  if (page->pin_count_ == 0) {
    replacer_->SetEvictable(frame_id, true);
  }
  return true;
}

auto BufferPoolManagerInstance::FlushPgImp(page_id_t page_id) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);

  frame_id_t frame_id = -1;
  if (!page_table_->Find(page_id, frame_id)) {
    return false;
  }

  auto *page = &pages_[frame_id];
  // Flush unconditionally: the caller may want a durable copy even of a clean page.
  disk_manager_->WritePage(page_id, page->GetData());
  page->is_dirty_ = false;
  return true;
}

void BufferPoolManagerInstance::FlushAllPgsImp() {
  std::scoped_lock<std::mutex> lock(latch_);

  for (size_t i = 0; i < pool_size_; i++) {
    auto *page = &pages_[i];
    if (page->GetPageId() != INVALID_PAGE_ID) {
      disk_manager_->WritePage(page->GetPageId(), page->GetData());
      page->is_dirty_ = false;
    }
  }
}

auto BufferPoolManagerInstance::DeletePgImp(page_id_t page_id) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);

  frame_id_t frame_id = -1;
  if (!page_table_->Find(page_id, frame_id)) {
    // Not in the pool means there is nothing to clean up.
    return true;
  }

  auto *page = &pages_[frame_id];
  if (page->pin_count_ > 0) {
    return false;
  }

  page_table_->Remove(page_id);
  // pin_count_ == 0 means Unpin already made the frame evictable, which is what
  // LRUKReplacer::Remove requires.
  replacer_->Remove(frame_id);
  free_list_.push_back(frame_id);

  // The page is being thrown away: reset the frame without writing anything back.
  page->page_id_ = INVALID_PAGE_ID;
  page->pin_count_ = 0;
  page->is_dirty_ = false;
  page->ResetMemory();
  DeallocatePage(page_id);
  return true;
}

auto BufferPoolManagerInstance::AllocatePage() -> page_id_t { return next_page_id_++; }

}  // namespace bustub
