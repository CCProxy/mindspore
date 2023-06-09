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

#ifndef MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_RAGGED_TENSOR_TO_SPARSE_CPU_KERNEL_H_
#define MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_RAGGED_TENSOR_TO_SPARSE_CPU_KERNEL_H_

#include <map>
#include <memory>
#include <vector>
#include <utility>
#include "plugin/device/cpu/kernel/cpu_kernel.h"
#include "plugin/factory/ms_factory.h"

namespace mindspore {
namespace kernel {
class RaggedTensorToSparseCpuKernelMod : public NativeCpuKernelMod {
 public:
  RaggedTensorToSparseCpuKernelMod() = default;
  ~RaggedTensorToSparseCpuKernelMod() override = default;

  bool Init(const BaseOperatorPtr &base_operator, const std::vector<KernelTensorPtr> &inputs,
            const std::vector<KernelTensorPtr> &outputs) override;
  bool Launch(const std::vector<AddressPtr> &inputs, const std::vector<AddressPtr> &workspace,
              const std::vector<AddressPtr> &outputs) override;
  int Resize(
    const BaseOperatorPtr &base_operator, const std::vector<KernelTensorPtr> &inputs,
    const std::vector<KernelTensorPtr> &outputs,
    const std::map<uint32_t, tensor::TensorPtr> &inputsOnHost = std::map<uint32_t, tensor::TensorPtr>()) override;

 protected:
  std::vector<KernelAttr> GetOpSupport() override {
    static std::vector<KernelAttr> support_list = {KernelAttr().AddSkipCheckAttr(true)};
    return support_list;
  }

 private:
  template <typename T1>
  void ValidateInputs(const std::vector<std::vector<T1>> &input1) const;

  template <typename T1, typename T2>
  bool LaunchKernel(const std::vector<kernel::AddressPtr> &inputs, const std::vector<kernel::AddressPtr> &workspace,
                    const std::vector<kernel::AddressPtr> &outputs);

  template <typename T1>
  void Update(const std::vector<std::vector<T1>> &input1, int64_t *output1_ptr,
              const std::vector<std::vector<int64_t>> &index_suffixes, std::vector<int64_t> index_prefix);

  template <typename T2>
  void OutPutSparseValues(const std::vector<kernel::AddressPtr> &inputs,
                          const std::vector<kernel::AddressPtr> &workspace,
                          const std::vector<kernel::AddressPtr> &outputs) const;

  template <typename T1>
  void OutPutSparseDenseShape(const std::vector<std::vector<T1>> &input1, int64_t *output3_ptr);

  int64_t n_;
  std::vector<int64_t> input2_shape_;
  std::vector<int64_t> output1_shape_;
  size_t input_num_;
  TypeId splits_type_{kTypeUnknown};
  TypeId values_type_{kTypeUnknown};
};
}  // namespace kernel
}  // namespace mindspore

#endif  // MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_RAGGED_TENSOR_TO_SPARSE_CPU_KERNEL_H_
