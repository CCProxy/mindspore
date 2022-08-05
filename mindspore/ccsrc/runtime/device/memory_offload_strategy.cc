/**
 * Copyright 2021 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "runtime/device/memory_offload_strategy.h"
#include <vector>
#include <map>
#include <memory>
#include <utility>
#include <algorithm>
#include "utils/log_adapter.h"

namespace mindspore {
namespace device {
constexpr size_t kFirstGetMemEventIndex = 1;
constexpr size_t kInitOrMallocMemEventIndex = 0;

MemEventPtrList &MemOffloadStrategy::GetPreComputeEvents(size_t step) {
  if (pre_compute_events_.size() <= step) {
    MS_LOG_EXCEPTION << "Index out of pre event range, index:" << step << ", event size:" << pre_compute_events_.size();
  }
  return pre_compute_events_[step];
}

MemEventPtrList &MemOffloadStrategy::GetPostComputeEvents(size_t step) {
  if (post_compute_events_.size() <= step) {
    MS_LOG_EXCEPTION << "Index out of post event range, index:" << step
                     << ", event size:" << post_compute_events_.size();
  }
  return post_compute_events_[step];
}

void MemOffloadStrategy::Execute() {
  CountMemUsage();
  CheckMemSize();
  if (need_swap_) {
    GenEventSpan();
    GenSwapEventSet();
  } else {
    GenContinuousMemAllocSteps();
  }
  GenComputeMemEvents();
}

void MemOffloadStrategy::CountMemUsage() {
  if (!min_mem_used_.empty()) {
    return;
  }
  if (mem_events_.empty() || total_step_ == 0) {
    return;
  }
  min_mem_used_.resize(total_step_, 0);
  std::vector<size_t> total_mem_used(total_step_, 0);
  size_t high_priority_mem_size = 0;
  MS_EXCEPTION_IF_NULL(continuous_mem_info_helper_);
  for (auto &item : mem_events_) {
    auto &mem_events = item.second;
    if (mem_events.empty()) {
      continue;
    }
    auto first_event = mem_events[kInitOrMallocMemEventIndex];
    const bool is_high_priority = IsHighPriorityMem(item.first);
    if (continuous_mem_info_helper_->IsContinuousInputMem(item.first)) {
      continue;
    } else if (is_high_priority) {
      high_priority_mem_size += first_event->mem_size;
    } else {
      auto last_event = mem_events[mem_events.size() - 1];
      for (size_t start_index = first_event->index; start_index <= last_event->index; ++start_index) {
        total_mem_used[start_index] += first_event->mem_size;
      }
    }

    // Calculate the minimum memory size for kernel execution.
    for (const auto &event : mem_events) {
      MS_EXCEPTION_IF_NULL(event);
      if (event->type != kGet) {
        continue;
      }
      min_mem_used_[event->index] += first_event->mem_size;
    }
  }
  CountContinuousMemUsage(&total_mem_used);
  min_mem_needed_ = *(std::max_element(min_mem_used_.begin(), min_mem_used_.end()));
  mem_used_without_swap_ = *(std::max_element(total_mem_used.begin(), total_mem_used.end())) + high_priority_mem_size;
  if (mem_size_ < min_mem_needed_) {
    MS_LOG(EXCEPTION) << "Out of memory, as available mem size is " << mem_size_ << " while graph needs at least "
                      << min_mem_needed_;
  }
}

bool MemOffloadStrategy::IsHighPriorityMem(const void *key) const {
  auto iter = mem_priority_.find(key);
  if (iter != mem_priority_.end()) {
    return iter->second == kMemPriorityHigh;
  }
  return false;
}

void MemOffloadStrategy::CheckMemSize() {
  if (mem_size_ < mem_used_without_swap_ || !manual_offload_keys_.empty()) {
    need_swap_ = true;
  }

  MS_LOG(INFO) << "Available mem size: " << mem_size_ << ", graph needs mem size: " << mem_used_without_swap_
               << " without swap, and needs at least " << min_mem_needed_ << " with swap.";
}

void MemOffloadStrategy::GenEventSpan() {
  if (!event_span_.empty()) {
    return;
  }
  for (auto &item : mem_events_) {
    auto &tensor_events = item.second;
    if (tensor_events.size() <= 1) {
      continue;
    }
    const bool is_high_priority = IsHighPriorityMem(item.first);
    for (size_t i = kFirstGetMemEventIndex; i < tensor_events.size(); ++i) {
      auto &event = tensor_events[i];
      MS_EXCEPTION_IF_NULL(event);
      if (event->type != kGet) {
        MS_LOG(EXCEPTION) << "Event should be Get except fist event.";
      }
      auto latest_get_event = tensor_events[i - 1];
      if (i == kFirstGetMemEventIndex && is_high_priority) {
        latest_get_event = tensor_events[tensor_events.size() - 1];
      }
      auto span = GetSpanBetweenMemEvents(latest_get_event->index, event->index);
      // High priority memory that is only used once in a total step
      if (is_high_priority && span == 0 && latest_get_event == event) {
        span = total_step_;
      }
      if (span > 1) {
        const size_t span_mul_size = (span - 1) * event->mem_size;
        (void)event_span_.emplace(span_mul_size, std::make_pair(event, span));
      }
    }
  }
}

void MemOffloadStrategy::GenSwapEventSet() {
  swap_events_.clear();
  // manual offload strategy
  if (!manual_offload_keys_.empty()) {
    for (const auto &iter : event_span_) {
      auto &event = iter.second.first;
      if (manual_offload_keys_.find(event->key) != manual_offload_keys_.end()) {
        (void)swap_events_.emplace(event);
      }
    }
    return;
  }
  // greedy span filter
  continuous_mem_info_helper_->ClearContinuousMallocIndex();
  std::vector<size_t> cur_mem_used(min_mem_used_.begin(), min_mem_used_.end());

  auto compare_total_size = [](const ContinuousMemInfoPtr &l, const ContinuousMemInfoPtr &r) -> bool {
    return l->total_size_ < r->total_size_;
  };
  auto all_continuous_mem_info = continuous_mem_info_helper_->GetAllContinuousMemInfo();
  std::sort(all_continuous_mem_info.begin(), all_continuous_mem_info.end(), compare_total_size);
  std::set<std::shared_ptr<MemEvent>> events_no_need_swap;
  for (const auto &continuous_mem_info : all_continuous_mem_info) {
    GenContinuousMemSwapEvent(continuous_mem_info, &cur_mem_used, &events_no_need_swap);
  }
  for (const auto &iter : event_span_) {
    const auto &event = iter.second.first;
    if (events_no_need_swap.count(event) > 0) {
      continue;
    }
    auto span = iter.second.second;
    AddToSwapEventSetIfOutOfMem(event, span, &cur_mem_used);
  }
}

void MemOffloadStrategy::AddToSwapEventSetIfOutOfMem(const std::shared_ptr<MemEvent> &event, size_t span,
                                                     std::vector<size_t> *mem_used) {
  const auto start_index = (GetPreMemEventIndex(event->index, span) + 1) % total_step_;
  bool revert = false;
  size_t cur_index = start_index;
  while (cur_index != event->index) {
    (*mem_used)[cur_index] += event->mem_size;
    if (mem_used->at(cur_index) > mem_size_) {
      revert = true;
    }
    cur_index += 1;
    if (cur_index >= total_step_) {
      cur_index = 0;
    }
  }
  if (revert) {
    cur_index = start_index;
    while (cur_index != event->index) {
      (*mem_used)[cur_index] -= event->mem_size;
      cur_index += 1;
      if (cur_index >= total_step_) {
        cur_index = 0;
      }
    }
    (void)swap_events_.emplace(event);
  }
}

void MemOffloadStrategy::GenContinuousMemSwapEvent(const ContinuousMemInfoPtr &continuous_mem_info,
                                                   std::vector<size_t> *mem_used,
                                                   std::set<std::shared_ptr<MemEvent>> *events_no_need_swap) {
  MS_EXCEPTION_IF_NULL(continuous_mem_info);
  if (continuous_mem_info->key_index_map_.empty()) {
    return;
  }
  const size_t continuous_mem_used_index = continuous_mem_info->compute_index_;
  if (!continuous_mem_info->is_input_) {
    continuous_mem_info_helper_->AddContinuousMallocIndex(continuous_mem_info, continuous_mem_info->compute_index_);
    return;
  }
  const auto max_span_mem_in_device = GetMaxSpanForContinuousMem(continuous_mem_info, *mem_used);
  size_t first_malloc_span = 0;
  size_t first_malloc_size_dup = 0;
  for (const auto &key_index : continuous_mem_info->key_index_map_) {
    const auto &events_iter = mem_events_.find(key_index.first);
    if (events_iter == mem_events_.end() || events_iter->second.empty()) {
      MS_LOG(EXCEPTION) << "Can not find events for continuous input memory, device address key: " << key_index.first;
    }
    size_t swap_in_event_index = kFirstGetMemEventIndex;
    size_t swap_in_span = 0;
    const bool is_high_priority = IsHighPriorityMem(key_index.first);
    for (size_t i = kFirstGetMemEventIndex; i < events_iter->second.size(); ++i) {
      const auto &mem_event = events_iter->second[i];
      if (!is_high_priority && mem_event->index > continuous_mem_used_index) {
        continue;
      }
      const size_t span = GetSpanBetweenMemEvents(mem_event->index, continuous_mem_used_index);
      // Find the max span than less than or equal to max_span_mem_in_device.
      if (span <= max_span_mem_in_device) {
        if (span >= swap_in_span) {
          swap_in_span = span;
          swap_in_event_index = i;
        }
        (void)events_no_need_swap->insert(mem_event);
      }
    }
    if (swap_in_event_index != kFirstGetMemEventIndex || is_high_priority) {
      (void)swap_events_.insert(events_iter->second[swap_in_event_index]);
    }
    // Find the earliest index that continuous memory should be allocated
    if (swap_in_span > first_malloc_span) {
      first_malloc_span = swap_in_span;
      first_malloc_size_dup = events_iter->second[swap_in_event_index]->mem_size;
    } else if (swap_in_span == first_malloc_span) {
      // Accumulate the memory size that already added to mem_used.
      first_malloc_size_dup += events_iter->second[swap_in_event_index]->mem_size;
    }
  }
  for (size_t span = 1; span <= first_malloc_span; ++span) {
    size_t index = GetPreMemEventIndex(continuous_mem_used_index, span);
    (*mem_used)[index] += continuous_mem_info->total_size_;
  }
  size_t index = GetPreMemEventIndex(continuous_mem_used_index, first_malloc_span);
  (*mem_used)[index] -= first_malloc_size_dup;
  continuous_mem_info_helper_->AddContinuousMallocIndex(continuous_mem_info, index);
}

size_t MemOffloadStrategy::GetMaxSpanForContinuousMem(const ContinuousMemInfoPtr &continuous_mem_info,
                                                      const std::vector<size_t> &mem_used) const {
  const size_t continuous_mem_used_index = continuous_mem_info->compute_index_;
  size_t earliest_malloc_index = GetFirstMallocIndex(continuous_mem_info);
  size_t max_span_mem_in_device = GetSpanBetweenMemEvents(earliest_malloc_index, continuous_mem_used_index);

  for (size_t span = 1; span <= max_span_mem_in_device; ++span) {
    size_t cur_index = GetPreMemEventIndex(continuous_mem_used_index, span);
    if (mem_used[cur_index] + continuous_mem_info->total_size_ > mem_size_) {
      max_span_mem_in_device = span - 1;
      break;
    }
  }
  return max_span_mem_in_device;
}

size_t MemOffloadStrategy::GetFirstMallocIndex(const ContinuousMemInfoPtr &continuous_mem_info) const {
  size_t earliest_malloc_index = continuous_mem_info->compute_index_;
  for (const auto &key_index : continuous_mem_info->key_index_map_) {
    const auto &events_iter = mem_events_.find(key_index.first);
    if (events_iter == mem_events_.end() || events_iter->second.empty()) {
      MS_LOG(EXCEPTION) << "Can not find events for continuous input memory, device address key: " << key_index.first;
    }
    const auto &first_event = events_iter->second[kInitOrMallocMemEventIndex];
    if (first_event->index < earliest_malloc_index) {
      earliest_malloc_index = first_event->index;
    }
  }
  return earliest_malloc_index;
}

void MemOffloadStrategy::GenContinuousMemAllocSteps() {
  for (const auto &continuous_mem_info : continuous_mem_info_helper_->GetAllContinuousMemInfo()) {
    GenContinuousMemAllocStep(continuous_mem_info);
  }
}

void MemOffloadStrategy::GenContinuousMemAllocStep(const ContinuousMemInfoPtr &continuous_mem_info) {
  if (!continuous_mem_info->is_input_) {
    continuous_mem_info_helper_->AddContinuousMallocIndex(continuous_mem_info, continuous_mem_info->compute_index_);
  } else {
    const size_t earliest_malloc_index = GetFirstMallocIndex(continuous_mem_info);
    continuous_mem_info_helper_->AddContinuousMallocIndex(continuous_mem_info, earliest_malloc_index);
  }
}

void MemOffloadStrategy::GenComputeMemEvents() {
  pre_compute_events_.clear();
  post_compute_events_.clear();
  pre_compute_events_.resize(total_step_);
  post_compute_events_.resize(total_step_);
  for (auto &item : mem_events_) {
    auto &mem_events = item.second;
    // No need to generate events for memory that has only one event, which means it is never used by any kernel.
    if (mem_events.size() <= 1) {
      continue;
    }

    const bool is_high_priority = IsHighPriorityMem(item.first);
    auto first_event = mem_events[kInitOrMallocMemEventIndex];
    MS_EXCEPTION_IF_NULL(first_event);
    const auto &first_get_event = mem_events[kFirstGetMemEventIndex];
    MS_EXCEPTION_IF_NULL(first_get_event);
    if (is_high_priority && swap_events_.find(first_get_event) != swap_events_.end()) {
      first_event->index = first_get_event->index;
    }
    if ((first_event->type == kInit || first_event->type == kMalloc) && first_event->index < total_step_) {
      (void)pre_compute_events_[first_event->index].emplace_back(first_event);
    } else {
      MS_LOG_EXCEPTION << "First event should be init or malloc!";
    }

    const auto &last_event = mem_events[mem_events.size() - 1];
    size_t pre_index = is_high_priority ? last_event->index : first_event->index;
    for (size_t i = kFirstGetMemEventIndex; i < mem_events.size(); ++i) {
      auto &event = mem_events[i];
      MS_EXCEPTION_IF_NULL(event);
      if (need_swap_ && swap_events_.find(event) != swap_events_.end()) {
        auto swap_out_event = std::make_shared<MemEvent>(kSwapOut, pre_index);
        swap_out_event->key = item.first;
        swap_out_event->mem_size = first_event->mem_size;
        (void)post_compute_events_[pre_index].emplace_back(swap_out_event);
        // avoid swap-in-event follow init-event
        if (i != kFirstGetMemEventIndex || first_event->type != kInit) {
          auto swap_in_event = std::make_shared<MemEvent>(kSwapIn, event->index);
          swap_in_event->key = item.first;
          swap_in_event->mem_size = first_event->mem_size;
          (void)pre_compute_events_[event->index].emplace_back(swap_in_event);
        }
      }
      if (event->index < pre_compute_events_.size()) {
        (void)pre_compute_events_[event->index].emplace_back(event);
      }
      pre_index = event->index;
    }
    if (!is_high_priority) {
      GenFreeEvent(last_event);
    }
  }
}

void MemOffloadStrategy::GenFreeEvent(const std::shared_ptr<MemEvent> &last_event) {
  MS_EXCEPTION_IF_NULL(last_event);
  auto free_event = std::make_shared<MemEvent>(kFree, last_event->index);
  free_event->key = last_event->key;
  if (last_event->index < post_compute_events_.size()) {
    (void)post_compute_events_[last_event->index].emplace_back(free_event);
  }
}

std::shared_ptr<ContinuousMemInfo> ContinuousMemInfoHelper::GetContinuousMemInfo(const void *address_key) {
  const auto &key_compute_index_iter = key_continuous_info_map_.find(address_key);
  if (key_compute_index_iter == key_continuous_info_map_.end()) {
    return nullptr;
  }
  return key_compute_index_iter->second;
}

std::vector<ContinuousMemInfoPtr> ContinuousMemInfoHelper::GetAllContinuousMemInfo() {
  std::vector<ContinuousMemInfoPtr> all_continuous_mem_info(input_continuous_mem_info_.size() +
                                                            output_continuous_mem_info_.size());
  (void)std::copy(input_continuous_mem_info_.begin(), input_continuous_mem_info_.end(),
                  all_continuous_mem_info.begin());
  (void)std::copy_backward(output_continuous_mem_info_.begin(), output_continuous_mem_info_.end(),
                           all_continuous_mem_info.end());
  return all_continuous_mem_info;
}

bool ContinuousMemInfoHelper::IsContinuousMem(const void *address_key) {
  const auto continuous_mem_info = GetContinuousMemInfo(address_key);
  return (continuous_mem_info != nullptr);
}

bool ContinuousMemInfoHelper::IsContinuousInputMem(const void *address_key) {
  const auto continuous_mem_info = GetContinuousMemInfo(address_key);
  return (continuous_mem_info != nullptr && continuous_mem_info->is_input_);
}

void ContinuousMemInfoHelper::AddContinuousMemInfo(bool is_input, size_t compute_index, size_t total_size,
                                                   const std::vector<size_t> &align_size_list,
                                                   const std::vector<const void *> &address_key_list) {
  if (align_size_list.size() != address_key_list.size()) {
    MS_LOG(EXCEPTION) << "Number of align size[" << align_size_list.size()
                      << "] is supposed to be equal to number of address[" << address_key_list.size() << "]";
  }
  ContinuousMemInfoPtr continuous_mem_info =
    std::make_shared<ContinuousMemInfo>(is_input, total_size, compute_index, align_size_list);
  for (size_t i = 0; i < address_key_list.size(); i += 1) {
    auto key = address_key_list[i];
    MS_EXCEPTION_IF_NULL(key);
    (void)continuous_mem_info->key_index_map_.emplace(key, i);
    (void)key_continuous_info_map_.emplace(key, continuous_mem_info);
  }
  if (is_input) {
    (void)input_continuous_mem_info_.insert(continuous_mem_info);
  } else {
    (void)output_continuous_mem_info_.insert(continuous_mem_info);
  }
  (void)index_continuous_info_map_[compute_index].emplace_back(continuous_mem_info);
}

void MemOffloadStrategy::CountContinuousMemUsage(std::vector<size_t> *total_mem_used) {
  const auto &input_continuous_mem_info_ = continuous_mem_info_helper_->GetAllContinuousMemInfo();
  for (const auto &continuous_mem_info : input_continuous_mem_info_) {
    if (!continuous_mem_info->is_input_ || continuous_mem_info->key_index_map_.empty()) {
      continue;
    }
    const auto &compute_index = continuous_mem_info->compute_index_;
    size_t earliest_malloc_index = SIZE_MAX;
    for (const auto &key_index : continuous_mem_info->key_index_map_) {
      const auto &key = key_index.first;
      const auto &events_iter = mem_events_.find(key);
      if (events_iter == mem_events_.end() || events_iter->second.empty()) {
        MS_LOG(EXCEPTION) << "Can not find memory events of continuous input memory, device address key: " << key;
      }
      const auto &mem_events = events_iter->second;
      const auto &first_event = mem_events[kInitOrMallocMemEventIndex];
      if (first_event->index < earliest_malloc_index) {
        earliest_malloc_index = first_event->index;
      }
      const auto &last_events = mem_events[mem_events.size() - 1];
      const auto end_step = IsHighPriorityMem(key) ? total_step_ - 1 : last_events->index;
      const auto mem_size = last_events->mem_size;
      for (size_t start_index = compute_index + 1; start_index <= end_step; start_index += 1) {
        (*total_mem_used)[start_index] += mem_size;
      }
    }
    for (size_t start_index = earliest_malloc_index; start_index <= compute_index; ++start_index) {
      (*total_mem_used)[start_index] += continuous_mem_info->total_size_;
    }
  }
}
}  // namespace device
}  // namespace mindspore
