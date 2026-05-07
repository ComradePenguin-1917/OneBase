#include "onebase/buffer/lru_k_replacer.h"
#include "onebase/common/exception.h"

namespace onebase {

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k)
    : max_frames_(num_frames), k_(k) {}

auto LRUKReplacer::Evict(frame_id_t *frame_id) -> bool {
  std::lock_guard<std::mutex> lock(latch_);

  if (curr_size_ == 0) {
    return false;
  }

  frame_id_t victim_frame = -1;
  size_t max_k_distance = 0;
  size_t earliest_first_access = current_timestamp_;
  bool found_inf_frame = false;

  for (const auto &[fid, entry] : entries_) {
    if (!entry.is_evictable_) {
      continue;
    }

    size_t access_count = entry.history_.size();

    if (access_count < k_) {
      if (!found_inf_frame) {
        found_inf_frame = true;
        victim_frame = fid;
        earliest_first_access = entry.history_.front();
      } else {
        if (entry.history_.front() < earliest_first_access) {
          earliest_first_access = entry.history_.front();
          victim_frame = fid;
        }
      }
    } else {
      if (!found_inf_frame) {
        size_t k_distance = current_timestamp_ - entry.history_.front();
        if (k_distance > max_k_distance) {
          max_k_distance = k_distance;
          victim_frame = fid;
        }
      }
    }
  }

  if (victim_frame == -1) {
    return false;
  }

  *frame_id = victim_frame;
  entries_.erase(victim_frame);
  curr_size_--;

  return true;
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id) {
  std::lock_guard<std::mutex> lock(latch_);

  auto &entry = entries_[frame_id];
  entry.history_.push_back(current_timestamp_);

  if (entry.history_.size() > k_) {
    entry.history_.pop_front();
  }

  current_timestamp_++;
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::lock_guard<std::mutex> lock(latch_);

  auto it = entries_.find(frame_id);
  if (it == entries_.end()) {
    return;
  }

  auto &entry = it->second;
  if (entry.is_evictable_ && !set_evictable) {
    curr_size_--;
  } else if (!entry.is_evictable_ && set_evictable) {
    curr_size_++;
  }

  entry.is_evictable_ = set_evictable;
}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  std::lock_guard<std::mutex> lock(latch_);

  auto it = entries_.find(frame_id);
  if (it == entries_.end()) {
    return;
  }

  if (!it->second.is_evictable_) {
    throw OneBaseException("Cannot remove non-evictable frame");
  }

  curr_size_--;
  entries_.erase(it);
}

auto LRUKReplacer::Size() const -> size_t { return curr_size_; }

}  // namespace onebase
