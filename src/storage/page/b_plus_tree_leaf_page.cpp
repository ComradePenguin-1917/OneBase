#include "onebase/storage/page/b_plus_tree_leaf_page.h"
#include <functional>
#include "onebase/common/exception.h"

namespace onebase {

template class BPlusTreeLeafPage<int, RID, std::less<int>>;

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_LEAF_PAGE_TYPE::Init(int max_size) {
  SetPageType(IndexPageType::LEAF_PAGE);
  SetMaxSize(max_size);
  SetSize(0);
  SetParentPageId(INVALID_PAGE_ID);
  next_page_id_ = INVALID_PAGE_ID;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_LEAF_PAGE_TYPE::KeyAt(int index) const -> KeyType {
  return array_[index].first;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_LEAF_PAGE_TYPE::ValueAt(int index) const -> ValueType {
  return array_[index].second;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_LEAF_PAGE_TYPE::KeyIndex(const KeyType &key, const KeyComparator &comparator) const -> int {
  int low = 0;
  int high = GetSize();
  while (low < high) {
    int mid = low + (high - low) / 2;
    if (comparator(array_[mid].first, key)) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  return low;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_LEAF_PAGE_TYPE::Lookup(const KeyType &key, ValueType *value,
                                         const KeyComparator &comparator) const -> bool {
  int idx = KeyIndex(key, comparator);
  if (idx < GetSize() && !comparator(key, array_[idx].first) && !comparator(array_[idx].first, key)) {
    *value = array_[idx].second;
    return true;
  }
  return false;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_LEAF_PAGE_TYPE::Insert(const KeyType &key, const ValueType &value,
                                         const KeyComparator &comparator) -> int {
  int idx = KeyIndex(key, comparator);
  if (idx < GetSize() && !comparator(key, array_[idx].first) && !comparator(array_[idx].first, key)) {
    return GetSize();
  }
  for (int i = GetSize(); i > idx; i--) {
    array_[i] = array_[i - 1];
  }
  array_[idx].first = key;
  array_[idx].second = value;
  IncreaseSize(1);
  return GetSize();
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_LEAF_PAGE_TYPE::RemoveAndDeleteRecord(const KeyType &key,
                                                        const KeyComparator &comparator) -> int {
  int idx = KeyIndex(key, comparator);
  if (idx >= GetSize() || comparator(key, array_[idx].first) || comparator(array_[idx].first, key)) {
    return GetSize();
  }
  for (int i = idx; i < GetSize() - 1; i++) {
    array_[i] = array_[i + 1];
  }
  IncreaseSize(-1);
  return GetSize();
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveHalfTo(BPlusTreeLeafPage *recipient) {
  int split_idx = GetSize() / 2;
  int move_count = GetSize() - split_idx;
  
  for (int i = 0; i < move_count; i++) {
    recipient->array_[i] = array_[split_idx + i];
  }
  recipient->SetSize(move_count);
  SetSize(split_idx);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveAllTo(BPlusTreeLeafPage *recipient) {
  int start = recipient->GetSize();
  for (int i = 0; i < GetSize(); i++) {
    recipient->array_[start + i] = array_[i];
  }
  recipient->IncreaseSize(GetSize());
  SetSize(0);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveFirstToEndOf(BPlusTreeLeafPage *recipient) {
  int recipient_size = recipient->GetSize();
  recipient->array_[recipient_size] = array_[0];
  recipient->IncreaseSize(1);
  
  for (int i = 0; i < GetSize() - 1; i++) {
    array_[i] = array_[i + 1];
  }
  IncreaseSize(-1);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveLastToFrontOf(BPlusTreeLeafPage *recipient) {
  for (int i = recipient->GetSize(); i > 0; i--) {
    recipient->array_[i] = recipient->array_[i - 1];
  }
  recipient->array_[0] = array_[GetSize() - 1];
  recipient->IncreaseSize(1);
  IncreaseSize(-1);
}

}  // namespace onebase
