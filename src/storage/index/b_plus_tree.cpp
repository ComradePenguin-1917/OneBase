#include "onebase/storage/index/b_plus_tree.h"
#include "onebase/storage/index/b_plus_tree_iterator.h"

#include <functional>

#include "onebase/common/exception.h"

namespace onebase {

template class BPlusTree<int, RID, std::less<int>>;

template <typename KeyType, typename ValueType, typename KeyComparator>
BPLUSTREE_TYPE::BPlusTree(std::string name, BufferPoolManager *bpm, const KeyComparator &comparator,
                           int leaf_max_size, int internal_max_size)
    : Index(std::move(name)), bpm_(bpm), comparator_(comparator),
      leaf_max_size_(leaf_max_size), internal_max_size_(internal_max_size) {
  if (leaf_max_size_ == 0) {
    leaf_max_size_ = static_cast<int>(
        (ONEBASE_PAGE_SIZE - sizeof(BPlusTreePage) - sizeof(page_id_t)) /
        (sizeof(KeyType) + sizeof(ValueType)));
  }
  if (internal_max_size_ == 0) {
    internal_max_size_ = static_cast<int>(
        (ONEBASE_PAGE_SIZE - sizeof(BPlusTreePage)) /
        (sizeof(KeyType) + sizeof(page_id_t)));
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::IsEmpty() const -> bool {
  return root_page_id_ == INVALID_PAGE_ID;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::GetValue(const KeyType &key, std::vector<ValueType> *result) -> bool {
  if (IsEmpty()) {
    return false;
  }

  page_id_t page_id = root_page_id_;
  Page *page = bpm_->FetchPage(page_id);
  if (page == nullptr) {
    return false;
  }

  auto *tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());

  while (!tree_page->IsLeafPage()) {
    auto *internal_page = reinterpret_cast<InternalPage *>(page->GetData());
    page_id_t child_page_id = internal_page->Lookup(key, comparator_);
    bpm_->UnpinPage(page_id, false);
    
    page_id = child_page_id;
    page = bpm_->FetchPage(page_id);
    if (page == nullptr) {
      return false;
    }
    tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
  }

  auto *leaf_page = reinterpret_cast<LeafPage *>(page->GetData());
  ValueType value;
  bool found = leaf_page->Lookup(key, &value, comparator_);
  bpm_->UnpinPage(page_id, false);

  if (found) {
    result->push_back(value);
  }
  return found;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value) -> bool {
  if (IsEmpty()) {
    page_id_t new_page_id;
    Page *new_page = bpm_->NewPage(&new_page_id);
    if (new_page == nullptr) {
      return false;
    }
    
    auto *leaf_page = reinterpret_cast<LeafPage *>(new_page->GetData());
    leaf_page->Init(leaf_max_size_);
    leaf_page->Insert(key, value, comparator_);
    
    root_page_id_ = new_page_id;
    bpm_->UnpinPage(new_page_id, true);
    return true;
  }

  page_id_t page_id = root_page_id_;
  Page *page = bpm_->FetchPage(page_id);
  if (page == nullptr) {
    return false;
  }

  auto *tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());

  while (!tree_page->IsLeafPage()) {
    auto *internal_page = reinterpret_cast<InternalPage *>(page->GetData());
    page_id_t child_page_id = internal_page->Lookup(key, comparator_);
    bpm_->UnpinPage(page_id, false);
    
    page_id = child_page_id;
    page = bpm_->FetchPage(page_id);
    if (page == nullptr) {
      return false;
    }
    tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
  }

  auto *leaf_page = reinterpret_cast<LeafPage *>(page->GetData());
  int old_size = leaf_page->GetSize();
  int new_size = leaf_page->Insert(key, value, comparator_);
  
  if (new_size == old_size) {
    bpm_->UnpinPage(page_id, false);
    return false;
  }

  if (leaf_page->GetSize() > leaf_page->GetMaxSize()) {
    Split(leaf_page, page_id);
  } else {
    bpm_->UnpinPage(page_id, true);
  }
  
  return true;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void BPLUSTREE_TYPE::Split(LeafPage *leaf_page, page_id_t leaf_page_id) {
  page_id_t new_page_id;
  Page *new_page = bpm_->NewPage(&new_page_id);
  if (new_page == nullptr) {
    bpm_->UnpinPage(leaf_page_id, true);
    return;
  }

  auto *new_leaf = reinterpret_cast<LeafPage *>(new_page->GetData());
  new_leaf->Init(leaf_max_size_);
  
  leaf_page->MoveHalfTo(new_leaf);
  new_leaf->SetNextPageId(leaf_page->GetNextPageId());
  leaf_page->SetNextPageId(new_page_id);

  KeyType middle_key = new_leaf->KeyAt(0);
  
  if (leaf_page->IsRootPage()) {
    page_id_t new_root_id;
    Page *new_root_page = bpm_->NewPage(&new_root_id);
    if (new_root_page == nullptr) {
      bpm_->UnpinPage(leaf_page_id, true);
      bpm_->UnpinPage(new_page_id, true);
      return;
    }
    
    auto *new_root = reinterpret_cast<InternalPage *>(new_root_page->GetData());
    new_root->Init(internal_max_size_);
    new_root->PopulateNewRoot(leaf_page_id, middle_key, new_page_id);
    
    leaf_page->SetParentPageId(new_root_id);
    new_leaf->SetParentPageId(new_root_id);
    
    root_page_id_ = new_root_id;
    
    bpm_->UnpinPage(leaf_page_id, true);
    bpm_->UnpinPage(new_page_id, true);
    bpm_->UnpinPage(new_root_id, true);
  } else {
    page_id_t parent_id = leaf_page->GetParentPageId();
    new_leaf->SetParentPageId(parent_id);
    
    bpm_->UnpinPage(leaf_page_id, true);
    bpm_->UnpinPage(new_page_id, true);
    
    InsertIntoParent(parent_id, leaf_page_id, middle_key, new_page_id);
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void BPLUSTREE_TYPE::InsertIntoParent(page_id_t parent_id, page_id_t old_child_id,
                                       const KeyType &key, page_id_t new_child_id) {
  Page *parent_page = bpm_->FetchPage(parent_id);
  if (parent_page == nullptr) {
    return;
  }

  auto *parent = reinterpret_cast<InternalPage *>(parent_page->GetData());
  parent->InsertNodeAfter(old_child_id, key, new_child_id);

  if (parent->GetSize() > parent->GetMaxSize()) {
    SplitInternal(parent, parent_id);
  } else {
    bpm_->UnpinPage(parent_id, true);
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void BPLUSTREE_TYPE::SplitInternal(InternalPage *internal_page, page_id_t internal_page_id) {
  page_id_t new_page_id;
  Page *new_page = bpm_->NewPage(&new_page_id);
  if (new_page == nullptr) {
    bpm_->UnpinPage(internal_page_id, true);
    return;
  }

  auto *new_internal = reinterpret_cast<InternalPage *>(new_page->GetData());
  new_internal->Init(internal_max_size_);

  int split_idx = internal_page->GetSize() / 2;
  KeyType middle_key = internal_page->KeyAt(split_idx);
  
  internal_page->MoveHalfTo(new_internal, middle_key);

  for (int i = 0; i < new_internal->GetSize(); i++) {
    page_id_t child_id = new_internal->ValueAt(i);
    Page *child_page = bpm_->FetchPage(child_id);
    if (child_page != nullptr) {
      auto *child = reinterpret_cast<BPlusTreePage *>(child_page->GetData());
      child->SetParentPageId(new_page_id);
      bpm_->UnpinPage(child_id, true);
    }
  }

  if (internal_page->IsRootPage()) {
    page_id_t new_root_id;
    Page *new_root_page = bpm_->NewPage(&new_root_id);
    if (new_root_page == nullptr) {
      bpm_->UnpinPage(internal_page_id, true);
      bpm_->UnpinPage(new_page_id, true);
      return;
    }

    auto *new_root = reinterpret_cast<InternalPage *>(new_root_page->GetData());
    new_root->Init(internal_max_size_);
    new_root->PopulateNewRoot(internal_page_id, middle_key, new_page_id);

    internal_page->SetParentPageId(new_root_id);
    new_internal->SetParentPageId(new_root_id);

    root_page_id_ = new_root_id;

    bpm_->UnpinPage(internal_page_id, true);
    bpm_->UnpinPage(new_page_id, true);
    bpm_->UnpinPage(new_root_id, true);
  } else {
    page_id_t parent_id = internal_page->GetParentPageId();
    new_internal->SetParentPageId(parent_id);

    bpm_->UnpinPage(internal_page_id, true);
    bpm_->UnpinPage(new_page_id, true);

    InsertIntoParent(parent_id, internal_page_id, middle_key, new_page_id);
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void BPLUSTREE_TYPE::Remove(const KeyType &key) {
  if (IsEmpty()) {
    return;
  }

  page_id_t leaf_page_id = FindLeafPage(key);
  if (leaf_page_id == INVALID_PAGE_ID) {
    return;
  }

  Page *page = bpm_->FetchPage(leaf_page_id);
  if (page == nullptr) {
    return;
  }

  auto *leaf_page = reinterpret_cast<LeafPage *>(page->GetData());
  int old_size = leaf_page->GetSize();
  leaf_page->RemoveAndDeleteRecord(key, comparator_);

  if (leaf_page->GetSize() < leaf_page->GetMinSize() && !leaf_page->IsRootPage()) {
    CoalesceOrRedistribute(leaf_page, leaf_page_id);
  } else if (leaf_page->GetSize() == 0 && leaf_page->IsRootPage()) {
    root_page_id_ = INVALID_PAGE_ID;
    bpm_->UnpinPage(leaf_page_id, true);
    bpm_->DeletePage(leaf_page_id);
  } else {
    bpm_->UnpinPage(leaf_page_id, true);
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::FindLeafPage(const KeyType &key) -> page_id_t {
  if (IsEmpty()) {
    return INVALID_PAGE_ID;
  }

  page_id_t page_id = root_page_id_;
  Page *page = bpm_->FetchPage(page_id);
  if (page == nullptr) {
    return INVALID_PAGE_ID;
  }

  auto *tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());

  while (!tree_page->IsLeafPage()) {
    auto *internal_page = reinterpret_cast<InternalPage *>(page->GetData());
    page_id_t child_page_id = internal_page->Lookup(key, comparator_);
    bpm_->UnpinPage(page_id, false);

    page_id = child_page_id;
    page = bpm_->FetchPage(page_id);
    if (page == nullptr) {
      return INVALID_PAGE_ID;
    }
    tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
  }

  bpm_->UnpinPage(page_id, false);
  return page_id;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void BPLUSTREE_TYPE::CoalesceOrRedistribute(LeafPage *leaf_page, page_id_t leaf_page_id) {
  page_id_t parent_id = leaf_page->GetParentPageId();
  Page *parent_page = bpm_->FetchPage(parent_id);
  if (parent_page == nullptr) {
    bpm_->UnpinPage(leaf_page_id, true);
    return;
  }

  auto *parent = reinterpret_cast<InternalPage *>(parent_page->GetData());
  int index = parent->ValueIndex(leaf_page_id);

  page_id_t sibling_id = INVALID_PAGE_ID;
  bool is_left_sibling = false;

  if (index > 0) {
    sibling_id = parent->ValueAt(index - 1);
    is_left_sibling = true;
  } else if (index < parent->GetSize() - 1) {
    sibling_id = parent->ValueAt(index + 1);
    is_left_sibling = false;
  }

  if (sibling_id == INVALID_PAGE_ID) {
    if (parent->IsRootPage() && parent->GetSize() == 1) {
      root_page_id_ = leaf_page_id;
      leaf_page->SetParentPageId(INVALID_PAGE_ID);
      bpm_->UnpinPage(leaf_page_id, true);
      bpm_->UnpinPage(parent_id, true);
      bpm_->DeletePage(parent_id);
      return;
    }
    bpm_->UnpinPage(leaf_page_id, true);
    bpm_->UnpinPage(parent_id, false);
    return;
  }

  Page *sibling_page = bpm_->FetchPage(sibling_id);
  if (sibling_page == nullptr) {
    bpm_->UnpinPage(leaf_page_id, true);
    bpm_->UnpinPage(parent_id, false);
    return;
  }

  auto *sibling = reinterpret_cast<LeafPage *>(sibling_page->GetData());

  if (sibling->GetSize() > sibling->GetMinSize()) {
    if (is_left_sibling) {
      sibling->MoveLastToFrontOf(leaf_page);
      parent->SetKeyAt(index, leaf_page->KeyAt(0));
    } else {
      sibling->MoveFirstToEndOf(leaf_page);
      parent->SetKeyAt(index + 1, sibling->KeyAt(0));
    }
    bpm_->UnpinPage(leaf_page_id, true);
    bpm_->UnpinPage(sibling_id, true);
    bpm_->UnpinPage(parent_id, true);
  } else {
    if (is_left_sibling) {
      leaf_page->MoveAllTo(sibling);
      sibling->SetNextPageId(leaf_page->GetNextPageId());
      parent->Remove(index);
      bpm_->UnpinPage(leaf_page_id, true);
      bpm_->UnpinPage(sibling_id, true);
      bpm_->DeletePage(leaf_page_id);
    } else {
      sibling->MoveAllTo(leaf_page);
      leaf_page->SetNextPageId(sibling->GetNextPageId());
      parent->Remove(index + 1);
      bpm_->UnpinPage(leaf_page_id, true);
      bpm_->UnpinPage(sibling_id, true);
      bpm_->DeletePage(sibling_id);
    }

    if (parent->GetSize() < parent->GetMinSize() && !parent->IsRootPage()) {
      CoalesceOrRedistributeInternal(parent, parent_id);
    } else if (parent->IsRootPage() && parent->GetSize() == 1) {
      page_id_t new_root_id = parent->RemoveAndReturnOnlyChild();
      root_page_id_ = new_root_id;
      
      Page *new_root_page = bpm_->FetchPage(new_root_id);
      if (new_root_page != nullptr) {
        auto *new_root = reinterpret_cast<BPlusTreePage *>(new_root_page->GetData());
        new_root->SetParentPageId(INVALID_PAGE_ID);
        bpm_->UnpinPage(new_root_id, true);
      }
      
      bpm_->UnpinPage(parent_id, true);
      bpm_->DeletePage(parent_id);
    } else {
      bpm_->UnpinPage(parent_id, true);
    }
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void BPLUSTREE_TYPE::CoalesceOrRedistributeInternal(InternalPage *internal_page, page_id_t internal_page_id) {
  page_id_t parent_id = internal_page->GetParentPageId();
  Page *parent_page = bpm_->FetchPage(parent_id);
  if (parent_page == nullptr) {
    bpm_->UnpinPage(internal_page_id, true);
    return;
  }

  auto *parent = reinterpret_cast<InternalPage *>(parent_page->GetData());
  int index = parent->ValueIndex(internal_page_id);

  page_id_t sibling_id = INVALID_PAGE_ID;
  bool is_left_sibling = false;

  if (index > 0) {
    sibling_id = parent->ValueAt(index - 1);
    is_left_sibling = true;
  } else if (index < parent->GetSize() - 1) {
    sibling_id = parent->ValueAt(index + 1);
    is_left_sibling = false;
  }

  if (sibling_id == INVALID_PAGE_ID) {
    bpm_->UnpinPage(internal_page_id, true);
    bpm_->UnpinPage(parent_id, false);
    return;
  }

  Page *sibling_page = bpm_->FetchPage(sibling_id);
  if (sibling_page == nullptr) {
    bpm_->UnpinPage(internal_page_id, true);
    bpm_->UnpinPage(parent_id, false);
    return;
  }

  auto *sibling = reinterpret_cast<InternalPage *>(sibling_page->GetData());

  if (sibling->GetSize() > sibling->GetMinSize()) {
    KeyType middle_key;
    if (is_left_sibling) {
      middle_key = parent->KeyAt(index);
      sibling->MoveLastToFrontOf(internal_page, middle_key);
      parent->SetKeyAt(index, internal_page->KeyAt(0));
      
      page_id_t moved_child_id = internal_page->ValueAt(0);
      Page *child_page = bpm_->FetchPage(moved_child_id);
      if (child_page != nullptr) {
        auto *child = reinterpret_cast<BPlusTreePage *>(child_page->GetData());
        child->SetParentPageId(internal_page_id);
        bpm_->UnpinPage(moved_child_id, true);
      }
    } else {
      middle_key = parent->KeyAt(index + 1);
      sibling->MoveFirstToEndOf(internal_page, middle_key);
      parent->SetKeyAt(index + 1, sibling->KeyAt(0));
      
      page_id_t moved_child_id = internal_page->ValueAt(internal_page->GetSize() - 1);
      Page *child_page = bpm_->FetchPage(moved_child_id);
      if (child_page != nullptr) {
        auto *child = reinterpret_cast<BPlusTreePage *>(child_page->GetData());
        child->SetParentPageId(internal_page_id);
        bpm_->UnpinPage(moved_child_id, true);
      }
    }
    bpm_->UnpinPage(internal_page_id, true);
    bpm_->UnpinPage(sibling_id, true);
    bpm_->UnpinPage(parent_id, true);
  } else {
    KeyType middle_key;
    if (is_left_sibling) {
      middle_key = parent->KeyAt(index);
      internal_page->MoveAllTo(sibling, middle_key);
      
      for (int i = 0; i < internal_page->GetSize(); i++) {
        page_id_t child_id = sibling->ValueAt(sibling->GetSize() - internal_page->GetSize() + i);
        Page *child_page = bpm_->FetchPage(child_id);
        if (child_page != nullptr) {
          auto *child = reinterpret_cast<BPlusTreePage *>(child_page->GetData());
          child->SetParentPageId(sibling_id);
          bpm_->UnpinPage(child_id, true);
        }
      }
      
      parent->Remove(index);
      bpm_->UnpinPage(internal_page_id, true);
      bpm_->UnpinPage(sibling_id, true);
      bpm_->DeletePage(internal_page_id);
    } else {
      middle_key = parent->KeyAt(index + 1);
      sibling->MoveAllTo(internal_page, middle_key);
      
      for (int i = 0; i < sibling->GetSize(); i++) {
        page_id_t child_id = internal_page->ValueAt(internal_page->GetSize() - sibling->GetSize() + i);
        Page *child_page = bpm_->FetchPage(child_id);
        if (child_page != nullptr) {
          auto *child = reinterpret_cast<BPlusTreePage *>(child_page->GetData());
          child->SetParentPageId(internal_page_id);
          bpm_->UnpinPage(child_id, true);
        }
      }
      
      parent->Remove(index + 1);
      bpm_->UnpinPage(internal_page_id, true);
      bpm_->UnpinPage(sibling_id, true);
      bpm_->DeletePage(sibling_id);
    }

    if (parent->GetSize() < parent->GetMinSize() && !parent->IsRootPage()) {
      CoalesceOrRedistributeInternal(parent, parent_id);
    } else if (parent->IsRootPage() && parent->GetSize() == 1) {
      page_id_t new_root_id = parent->RemoveAndReturnOnlyChild();
      root_page_id_ = new_root_id;
      
      Page *new_root_page = bpm_->FetchPage(new_root_id);
      if (new_root_page != nullptr) {
        auto *new_root = reinterpret_cast<BPlusTreePage *>(new_root_page->GetData());
        new_root->SetParentPageId(INVALID_PAGE_ID);
        bpm_->UnpinPage(new_root_id, true);
      }
      
      bpm_->UnpinPage(parent_id, true);
      bpm_->DeletePage(parent_id);
    } else {
      bpm_->UnpinPage(parent_id, true);
    }
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::Begin() -> Iterator {
  if (IsEmpty()) {
    return End();
  }

  page_id_t page_id = root_page_id_;
  Page *page = bpm_->FetchPage(page_id);
  if (page == nullptr) {
    return End();
  }

  auto *tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());

  while (!tree_page->IsLeafPage()) {
    auto *internal_page = reinterpret_cast<InternalPage *>(page->GetData());
    page_id_t child_page_id = internal_page->ValueAt(0);
    bpm_->UnpinPage(page_id, false);

    page_id = child_page_id;
    page = bpm_->FetchPage(page_id);
    if (page == nullptr) {
      return End();
    }
    tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
  }

  bpm_->UnpinPage(page_id, false);
  return Iterator(page_id, 0, bpm_);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> Iterator {
  if (IsEmpty()) {
    return End();
  }

  page_id_t page_id = root_page_id_;
  Page *page = bpm_->FetchPage(page_id);
  if (page == nullptr) {
    return End();
  }

  auto *tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());

  while (!tree_page->IsLeafPage()) {
    auto *internal_page = reinterpret_cast<InternalPage *>(page->GetData());
    page_id_t child_page_id = internal_page->Lookup(key, comparator_);
    bpm_->UnpinPage(page_id, false);

    page_id = child_page_id;
    page = bpm_->FetchPage(page_id);
    if (page == nullptr) {
      return End();
    }
    tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
  }

  auto *leaf_page = reinterpret_cast<LeafPage *>(page->GetData());
  int index = leaf_page->KeyIndex(key, comparator_);
  
  bpm_->UnpinPage(page_id, false);
  
  if (index >= leaf_page->GetSize()) {
    return Iterator(leaf_page->GetNextPageId(), 0, bpm_);
  }
  
  return Iterator(page_id, index, bpm_);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::End() -> Iterator {
  return Iterator(INVALID_PAGE_ID, 0);
}

}  // namespace onebase
