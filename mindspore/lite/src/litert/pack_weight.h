/**
 * Copyright 2022 Huawei Technologies Co., Ltd
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

#ifndef MINDSPORE_LITE_SRC_RUNTIME_PACK_WEIGHT_H_
#define MINDSPORE_LITE_SRC_RUNTIME_PACK_WEIGHT_H_
#include <map>
#include <string>
#include <algorithm>
#include <utility>
#include <vector>
#include <set>
#include <mutex>
#include <unordered_map>
#include <memory>
#include "src/tensor.h"
#include "src/litert/lite_session.h"
namespace mindspore::lite {
struct ModelConstWeight {
  // origin tensor data <-> packed tensor data
  std::map<const void *, void *> origin_and_packed_pair;
  std::shared_ptr<Allocator> allocator = nullptr;
  int numa_id = -1;
  std::unordered_map<int, void *> tensors_data;
  std::set<void *> fp16_fp32_data;
};

class PackWeight {
 public:
  PackWeight() = default;
  ~PackWeight();
  STATUS InitWeightManagerByBuf(const char *model_buf, size_t model_size, int numa_id = -1, bool copy_buf = false);
  char *GetNumaModelBuf(const char *model_buf, int numa_id);
  STATUS StoreOriginTensorData(const char *model_buf, const void *origin_tensor_data);
  void *GetPackData(const void *tensor_data, const size_t size, bool *is_packed);
  STATUS ReplaceOriginTensorData(const char *model_buf, std::vector<Tensor *> *tensors, int tensor_index);
  void *ReplaceFp16Data(void *origin_fp16_data, size_t size);

 private:
  void FreePackedWeight(ModelConstWeight *weight);
  void FreeTensorData(ModelConstWeight *weight);
  void FreeFp16ToFp32Data(ModelConstWeight *weight);

  bool copy_buf_ = false;
  std::mutex mtx_weight_;
  std::unordered_map<const char *, ModelConstWeight *> buf_model_weight_;
  std::unordered_map<const char *, std::vector<int>> numa_model_buf_;
  std::unordered_map<const char *, std::vector<char *>> model_buf_map_;
  std::unordered_map<void *, void *> fp16_fp32_data_pair_;
};
}  // namespace mindspore::lite
#endif  // MINDSPORE_LITE_SRC_RUNTIME_PACK_WEIGHT_H_
