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
#ifndef MINDSPORE_LITE_SRC_RUNTIME_KERNEL_CPU_FP32_SPARSE_RESHAPE_FP32_H_
#define MINDSPORE_LITE_SRC_RUNTIME_KERNEL_CPU_FP32_SPARSE_RESHAPE_FP32_H_

#include <vector>
#include "src/litert/lite_kernel.h"

using mindspore::lite::InnerContext;

namespace mindspore::kernel {
class SparseReshapeCPUKernel : public LiteKernel {
 public:
  SparseReshapeCPUKernel(OpParameter *parameter, const std::vector<lite::Tensor *> &inputs,
                         const std::vector<lite::Tensor *> &outputs, const lite::InnerContext *ctx)
      : LiteKernel(parameter, inputs, outputs, ctx) {}
  ~SparseReshapeCPUKernel() = default;

  int Prepare() override;
  int IndexConvert();
  int ReSize() override { return RET_OK; }
  int Run() override;
  int SoftCopyInputToOutput(lite::Tensor *src_tensor, lite::Tensor *dst_tensor);
};
}  // namespace mindspore::kernel
#endif  // MINDSPORE_LITE_SRC_RUNTIME_KERNEL_CPU_FP32_SPARSE_RESHAPE_FP32_H_
