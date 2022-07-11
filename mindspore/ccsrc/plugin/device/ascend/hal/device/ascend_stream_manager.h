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

#ifndef MINDSPORE_CCSRC_RUNTIME_DEVICE_ASCEND_ASCEND_STREAM_MANAGER_H_
#define MINDSPORE_CCSRC_RUNTIME_DEVICE_ASCEND_ASCEND_STREAM_MANAGER_H_

#include <memory>
#include "utils/hash_map.h"
#include "runtime/event.h"

namespace mindspore {
namespace device {
namespace ascend {
class AscendStreamMng {
 public:
  static AscendStreamMng &GetInstance() {
    static AscendStreamMng instance;
    return instance;
  }

  void ResetResource() {
    cur_stream_num_ = 0;
    cur_event_num_ = 0;
  }

  uint32_t ApplyNewStream() { return cur_stream_num_++; }

  uint32_t ApplyNewEvent() { return cur_event_num_++; }

  rtEvent_t ApplyRtEvent() const {
    auto rt_resource = std::make_shared<rtEvent_t>();
    auto ret = rtEventCreate(rt_resource.get());
    if (ret != RT_ERROR_NONE) {
      MS_LOG(ERROR) << "rtEventCreate failed, ret:" << ret;
      *rt_resource = nullptr;
    }
    return *rt_resource;
  }

  void DeleteEvent() {
    if (cur_event_num_ == 0) {
      MS_LOG(WARNING) << "total event num is 0, no event to delete";
    } else {
      --cur_event_num_;
    }
  }

  void DeleteStream() {
    if (cur_stream_num_ == 0) {
      MS_LOG(WARNING) << " total stream num is 0, no stream to delete";
    } else {
      --cur_stream_num_;
    }
  }

  uint32_t cur_stream_num() const { return cur_stream_num_; }

  uint32_t GetCurAllocStreamId() const {
    if (cur_stream_num_ == 0) {
      MS_LOG(EXCEPTION) << "stream nums is 0, no stream id should be get";
    }
    return cur_stream_num_ - 1;
  }

  uint32_t cur_event_num() const { return cur_event_num_; }

 private:
  uint32_t cur_stream_num_{0};
  uint32_t cur_event_num_{0};
};
}  // namespace ascend
}  // namespace device
}  // namespace mindspore

#endif  // MINDSPORE_CCSRC_RUNTIME_DEVICE_ASCEND_ASCEND_STREAM_MANAGER_H_
