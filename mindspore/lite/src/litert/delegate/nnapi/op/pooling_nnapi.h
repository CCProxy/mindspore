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
#ifndef MINDSPORE_LITE_SRC_LITERT_DELEGATE_NNAPI_OP_POOLING_NNAPI_H_
#define MINDSPORE_LITE_SRC_LITERT_DELEGATE_NNAPI_OP_POOLING_NNAPI_H_

#include <string>
#include <vector>
#include "src/litert/delegate/nnapi/op/nnapi_op.h"

namespace mindspore {
namespace lite {
class NNAPIPooling : public NNAPIOp {
 public:
  NNAPIPooling(const std::string &name, const schema::Primitive *primitive,
               const std::vector<mindspore::MSTensor> &in_tensors, const std::vector<mindspore::MSTensor> &out_tensors,
               schema::QuantType quant_type)
      : NNAPIOp(name, primitive, in_tensors, out_tensors, quant_type) {}

  ~NNAPIPooling() override {}

  bool IsSupport() override { return true; };
  int InitParams() override;
  int AddOpToNNAPIModel(ANeuralNetworksModel *nnapi_model, std::vector<mindspore::MSTensor> *all_tensors) override;

 private:
  int SetPoolingParams(const flatbuffers::Vector<int64_t> *pads, const flatbuffers::Vector<int64_t> *strides,
                       const flatbuffers::Vector<int64_t> *kernels, bool is_global);
  int act_type_;
  std::vector<int> pad_list_;
  std::vector<int> strides_;
  std::vector<int> kernel_size_ = {1, 1};
};
}  // namespace lite
}  // namespace mindspore
#endif  // MINDSPORE_LITE_SRC_LITERT_DELEGATE_NNAPI_OP_POOLING_NNAPI_H_
