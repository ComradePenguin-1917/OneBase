#include "onebase/buffer/buffer_pool_manager.h"
#include "onebase/common/exception.h"
#include "onebase/common/logger.h"

namespace onebase {

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk_manager, size_t replacer_k)
    : pool_size_(pool_size), disk_manager_(disk_manager) {
  pages_ = new Page[pool_size_];
  replacer_ = std::make_unique<LRUKReplacer>(pool_size, replacer_k);
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.emplace_back(static_cast<frame_id_t>(i));
  }
}

BufferPoolManager::~BufferPoolManager() { delete[] pages_; }

auto BufferPoolManager::NewPage(page_id_t *page_id) -> Page * {
  std::lock_guard<std::mutex> lock(latch_);

  frame_id_t frame_id;
  bool has_victim = false;

  if (!free_list_.empty()) {
    frame_id = free_list_.front();
    free_list_.pop_front();
    has_victim = true;
  } else {
    has_victim = replacer_->Evict(&frame_id);
  }

  if (!has_victim) {
    return nullptr;
  }

  Page &victim_page = pages_[frame_id];

  if (victim_page.IsDirty()) {
    disk_manager_->WritePage(victim_page.GetPageId(), victim_page.GetData());
    victim_page.is_dirty_ = false;
  }

  if (victim_page.GetPageId() != INVALID_PAGE_ID) {
    page_table_.erase(victim_page.GetPageId());
  }

  *page_id = disk_manager_->AllocatePage();

  victim_page.ResetMemory();
  victim_page.page_id_ = *page_id;
  victim_page.pin_count_ = 1;
  victim_page.is_dirty_ = false;

  page_table_[*page_id] = frame_id;

  replacer_->RecordAccess(frame_id);
  replacer_->SetEvictable(frame_id, false);

  return &victim_page;
}

auto BufferPoolManager::FetchPage(page_id_t page_id) -> Page * {
  std::lock_guard<std::mutex> lock(latch_);

  auto it = page_table_.find(page_id);
  if (it != page_table_.end()) {
    frame_id_t frame_id = it->second;
    Page &page = pages_[frame_id];
    page.pin_count_++;
    replacer_->RecordAccess(frame_id);
    replacer_->SetEvictable(frame_id, false);
    return &page;
  }

  frame_id_t frame_id;
  bool has_victim = false;

  if (!free_list_.empty()) {
    frame_id = free_list_.front();
    free_list_.pop_front();
    has_victim = true;
  } else {
    has_victim = replacer_->Evict(&frame_id);
  }

  if (!has_victim) {
    return nullptr;
  }

  Page &victim_page = pages_[frame_id];

  if (victim_page.IsDirty()) {
    disk_manager_->WritePage(victim_page.GetPageId(), victim_page.GetData());
    victim_page.is_dirty_ = false;
  }

  if (victim_page.GetPageId() != INVALID_PAGE_ID) {
    page_table_.erase(victim_page.GetPageId());
  }

  disk_manager_->ReadPage(page_id, victim_page.GetData());

  victim_page.page_id_ = page_id;
  victim_page.pin_count_ = 1;
  victim_page.is_dirty_ = false;

  page_table_[page_id] = frame_id;

  replacer_->RecordAccess(frame_id);
  replacer_->SetEvictable(frame_id, false);

  return &victim_page;
}

auto BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) -> bool {
  std::lock_guard<std::mutex> lock(latch_);

  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) {
    return false;
  }

  frame_id_t frame_id = it->second;
  Page &page = pages_[frame_id];

  if (page.GetPinCount() <= 0) {
    return false;
  }

  if (is_dirty) {
    page.is_dirty_ = true;
  }

  page.pin_count_--;

  if (page.GetPinCount() == 0) {
    replacer_->SetEvictable(frame_id, true);
  }

  return true;
}

auto BufferPoolManager::DeletePage(page_id_t page_id) -> bool {
  std::lock_guard<std::mutex> lock(latch_);

  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) {
    return true;
  }

  frame_id_t frame_id = it->second;
  Page &page = pages_[frame_id];

  if (page.GetPinCount() > 0) {
    return false;
  }

  page_table_.erase(page_id);
  replacer_->Remove(frame_id);

  page.ResetMemory();
  page.page_id_ = INVALID_PAGE_ID;
  page.pin_count_ = 0;
  page.is_dirty_ = false;

  free_list_.push_back(frame_id);
  disk_manager_->DeallocatePage(page_id);

  return true;
}

auto BufferPoolManager::FlushPage(page_id_t page_id) -> bool {
  std::lock_guard<std::mutex> lock(latch_);

  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) {
    return false;
  }

  frame_id_t frame_id = it->second;
  Page &page = pages_[frame_id];

  disk_manager_->WritePage(page_id, page.GetData());
  page.is_dirty_ = false;

  return true;
}

void BufferPoolManager::FlushAllPages() {
  std::lock_guard<std::mutex> lock(latch_);

  for (const auto &[page_id, frame_id] : page_table_) {
    Page &page = pages_[frame_id];
    disk_manager_->WritePage(page_id, page.GetData());
    page.is_dirty_ = false;
  }
}

}  // namespace onebase
