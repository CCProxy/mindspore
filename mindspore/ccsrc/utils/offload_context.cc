/**
 * Copyright 2023 Huawei Technologies Co., Ltd
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

#include "include/common/utils/offload_context.h"

#include <memory>
#include <mutex>

#include "utils/log_adapter.h"
#include "utils/ms_context.h"
#include "include/common/utils/utils.h"
namespace mindspore {
namespace {
constexpr char kMemAvailable[] = "MemAvailable";
constexpr char kOffloadPath[] = "./offload/";
constexpr char kOffloadParam[] = "";
constexpr size_t kAioBlockSize = 1 << 20;
constexpr size_t kAioQueueDepth = 1024;
}  // namespace
std::shared_ptr<OffloadContext> OffloadContext::GetInstance() {
  static std::once_flag init_flag;
  static std::shared_ptr<OffloadContext> inst_context_ = nullptr;
  std::call_once(init_flag, [&]() {
    if (inst_context_ == nullptr) {
      inst_context_.reset(new (std::nothrow) OffloadContext());
      MS_EXCEPTION_IF_NULL(inst_context_);
    }
  });
  return inst_context_;
}

void OffloadContext::set_offload_param(const std::string &offload_param) { offload_param_ = offload_param; }

void OffloadContext::set_offload_path(const std::string &offload_path) { offload_path_ = offload_path; }

void OffloadContext::set_offload_checkpoint(const std::string &offload_checkpoint) {
  offload_checkpoint_ = offload_checkpoint;
}

void OffloadContext::set_offload_ddr_size(size_t offload_ddr_size) { offload_ddr_size_ = offload_ddr_size; }

size_t OffloadContext::offload_ddr_size() {
  if (offload_ddr_size_ == 0) {
    offload_ddr_size_ = mindspore::GetSystemMemorySize(kMemAvailable);
    MS_LOG(WARNING) << "Offload ddr size is not set, please set this via the context.set_offload_context() method.";
  }
  return offload_ddr_size_;
}

void OffloadContext::set_offload_disk_size(size_t offload_disk_size) { offload_disk_size_ = offload_disk_size; }

void OffloadContext::set_enable_aio(bool enable_aio) { enable_aio_ = enable_aio; }

bool OffloadContext::enable_aio() {
  if (enable_aio_) {
    auto context = MsContext::GetInstance();
    MS_EXCEPTION_IF_NULL(context);
    const std::string &target = context->get_param<std::string>(MS_CTX_DEVICE_TARGET);
    if (target == kAscendDevice && enable_pinned_mem()) {
      MS_LOG(INFO) << "On ascend devices, enable aio and enable pinned mem cannot be turned on at the same time.";
      enable_aio_ = false;
    }
  }
  return enable_aio_;
}

void OffloadContext::set_aio_block_size(size_t aio_block_size) { aio_block_size_ = aio_block_size; }

void OffloadContext::set_aio_queue_depth(size_t aio_queue_depth) { aio_queue_depth_ = aio_queue_depth; }

void OffloadContext::set_enable_pinned_mem(bool enable_pinned_mem) { enable_pinned_mem_ = enable_pinned_mem; }

OffloadContext::OffloadContext()
    : offload_param_(kOffloadParam),
      offload_path_(kOffloadPath),
      offload_checkpoint_(kOffloadParam),
      offload_ddr_size_(0),
      offload_disk_size_(0),
      enable_aio_(true),
      aio_block_size_(kAioBlockSize),
      aio_queue_depth_(kAioQueueDepth),
      enable_pinned_mem_(true) {}
}  // namespace mindspore
