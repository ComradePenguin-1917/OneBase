#include "onebase/concurrency/lock_manager.h"
#include "onebase/common/exception.h"

namespace onebase {

auto LockManager::LockShared(Transaction *txn, const RID &rid) -> bool {
  std::unique_lock<std::mutex> lock(latch_);
  
  if (txn->GetState() == TransactionState::SHRINKING) {
    txn->SetState(TransactionState::ABORTED);
    return false;
  }
  
  if (txn->GetState() == TransactionState::ABORTED) {
    return false;
  }
  
  if (txn->IsSharedLocked(rid) || txn->IsExclusiveLocked(rid)) {
    return true;
  }
  
  auto &queue = lock_table_[rid];
  queue.request_queue_.emplace_back(txn->GetTransactionId(), LockMode::SHARED);
  auto it = std::prev(queue.request_queue_.end());
  
  queue.cv_.wait(lock, [&]() {
    if (txn->GetState() == TransactionState::ABORTED) {
      return true;
    }
    for (const auto &req : queue.request_queue_) {
      if (req.granted_ && req.lock_mode_ == LockMode::EXCLUSIVE) {
        return false;
      }
    }
    return true;
  });
  if (txn->GetState() == TransactionState::ABORTED) {
    queue.request_queue_.erase(it);
    return false;
  }
  
  it->granted_ = true;
  txn->GetSharedLockSet()->insert(rid);
  return true;
}

auto LockManager::LockExclusive(Transaction *txn, const RID &rid) -> bool {
  std::unique_lock<std::mutex> lock(latch_);
  
  if (txn->GetState() == TransactionState::SHRINKING) {
    txn->SetState(TransactionState::ABORTED);
    return false;
  }
  
  if (txn->GetState() == TransactionState::ABORTED) {
    return false;
  }
  
  if (txn->IsExclusiveLocked(rid)) {
    return true;
  }
  
  auto &queue = lock_table_[rid];
  queue.request_queue_.emplace_back(txn->GetTransactionId(), LockMode::EXCLUSIVE);
  auto it = std::prev(queue.request_queue_.end());
  
  queue.cv_.wait(lock, [&]() {
    if (txn->GetState() == TransactionState::ABORTED) {
      return true;
    }
    for (const auto &req : queue.request_queue_) {
      if (req.granted_ && req.txn_id_ != txn->GetTransactionId()) {
        return false;
      }
    }
    return true;
  });
  if (txn->GetState() == TransactionState::ABORTED) {
    queue.request_queue_.erase(it);
    return false;
  }
  
  it->granted_ = true;
  txn->GetExclusiveLockSet()->insert(rid);
  return true;
}

auto LockManager::LockUpgrade(Transaction *txn, const RID &rid) -> bool {
  std::unique_lock<std::mutex> lock(latch_);
  
  if (txn->GetState() == TransactionState::SHRINKING) {
    txn->SetState(TransactionState::ABORTED);
    return false;
  }
  
  if (txn->GetState() == TransactionState::ABORTED) {
    return false;
  }
  
  auto &queue = lock_table_[rid];
  
  if (queue.upgrading_) {
    txn->SetState(TransactionState::ABORTED);
    return false;
  }
  
  queue.upgrading_ = true;
  
  std::list<LockRequest>::iterator it;
  for (it = queue.request_queue_.begin(); it != queue.request_queue_.end(); ++it) {
    if (it->txn_id_ == txn->GetTransactionId() && it->granted_) {
      break;
    }
  }
  
  if (it == queue.request_queue_.end()) {
    queue.upgrading_ = false;
    return false;
  }
  
  it->lock_mode_ = LockMode::EXCLUSIVE;
  it->granted_ = false;
  
  queue.cv_.wait(lock, [&]() {
    if (txn->GetState() == TransactionState::ABORTED) {
      return true;
    }
    for (const auto &req : queue.request_queue_) {
      if (req.granted_ && req.txn_id_ != txn->GetTransactionId()) {
        return false;
      }
    }
    return true;
  });
  queue.upgrading_ = false;
  if (txn->GetState() == TransactionState::ABORTED) {
    return false;
  }
  
  it->granted_ = true;
  txn->GetSharedLockSet()->erase(rid);
  txn->GetExclusiveLockSet()->insert(rid);
  return true;
}

auto LockManager::Unlock(Transaction *txn, const RID &rid) -> bool {
  std::unique_lock<std::mutex> lock(latch_);
  
  auto &queue = lock_table_[rid];
  
  for (auto it = queue.request_queue_.begin(); it != queue.request_queue_.end(); ++it) {
    if (it->txn_id_ == txn->GetTransactionId()) {
      queue.request_queue_.erase(it);
      break;
    }
  }
  
  if (txn->GetState() == TransactionState::GROWING) {
    txn->SetState(TransactionState::SHRINKING);
  }
  
  txn->GetSharedLockSet()->erase(rid);
  txn->GetExclusiveLockSet()->erase(rid);
  
  queue.cv_.notify_all();
  
  return true;
}

}  // namespace onebase
