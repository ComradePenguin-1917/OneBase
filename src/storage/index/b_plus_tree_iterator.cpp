#include "onebase/storage/index/b_plus_tree_iterator.h"
#include <stdexcept>
#include "onebase/buffer/buffer_pool_manager.h"
#include "onebase/storage/page/b_plus_tree_leaf_page.h"

namespace onebase {

template class BPlusTreeIterator<int, RID, std::less<int>>;

template <typename KeyType, typename ValueType, typename KeyComparator>
BPLUSTREE_ITERATOR_TYPE::BPlusTreeIterator(page_id_t page_id, int index, BufferPoolManager *bpm)
    : page_id_(page_id), index_(index), bpm_(bpm) {}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::IsEnd() const -> bool {
  return page_id_ == INVALID_PAGE_ID;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::operator*() -> const std::pair<KeyType, ValueType> & {
  if (IsEnd() || bpm_ == nullptr) {
    throw std::runtime_error("BPlusTreeIterator::operator*: invalid iterator");
  }

  Page *page = bpm_->FetchPage(page_id_);
  if (page == nullptr) {
    throw std::runtime_error("BPlusTreeIterator::operator*: page fetch failed");
  }

  using LeafPage = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>;
  auto *leaf_page = reinterpret_cast<LeafPage *>(page->GetData());

  current_.first = leaf_page->KeyAt(index_);
  current_.second = leaf_page->ValueAt(index_);

  bpm_->UnpinPage(page_id_, false);
  return current_;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::operator++() -> BPlusTreeIterator & {
  if (IsEnd() || bpm_ == nullptr) {
    return *this;
  }

  Page *page = bpm_->FetchPage(page_id_);
  if (page == nullptr) {
    page_id_ = INVALID_PAGE_ID;
    return *this;
  }

  using LeafPage = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>;
  auto *leaf_page = reinterpret_cast<LeafPage *>(page->GetData());

  index_++;
  if (index_ >= leaf_page->GetSize()) {
    page_id_ = leaf_page->GetNextPageId();
    index_ = 0;
  }

  bpm_->UnpinPage(page_id_, false);
  return *this;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::operator==(const BPlusTreeIterator &other) const -> bool {
  return page_id_ == other.page_id_ && index_ == other.index_;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::operator!=(const BPlusTreeIterator &other) const -> bool {
  return !(*this == other);
}

}  // namespace onebase
