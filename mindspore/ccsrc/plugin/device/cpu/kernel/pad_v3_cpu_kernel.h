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

#ifndef MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_PAD_V3_CPU_KERNEL_H_
#define MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_PAD_V3_CPU_KERNEL_H_

#include <algorithm>
#include <array>
#include <complex>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "plugin/device/cpu/kernel/cpu_kernel.h"
#include "plugin/factory/ms_factory.h"

namespace mindspore {
namespace kernel {
class PadV3CpuKernelMod : public DeprecatedNativeCpuKernelMod {
 public:
  PadV3CpuKernelMod() = default;
  ~PadV3CpuKernelMod() override = default;

  void InitKernel(const CNodePtr &kernel_node) override;

  bool Launch(const std::vector<AddressPtr> &inputs, const std::vector<AddressPtr> &workspace,
              const std::vector<AddressPtr> &outputs) override {
    return kernel_func_(this, inputs, workspace, outputs);
  };

 protected:
  std::vector<KernelAttr> GetOpSupport() override;

 private:
  template <typename T, typename S>
  bool LaunchKernel(const std::vector<kernel::AddressPtr> &inputs, const std::vector<kernel::AddressPtr> &workspace,
                    const std::vector<kernel::AddressPtr> &outputs);

  template <typename S>
  bool GetPaddings(const std::vector<AddressPtr> &inputs);

  template <typename T>
  void ConstantModeCompute(T *input_ptr, T *output_ptr, T constant_values);

  template <typename T>
  void OtherModeCompute(T *input_ptr, T *output_ptr, int64_t p);

  template <typename T>
  void OtherModeCompute1D(T *input_ptr, T *output_ptr, int64_t p);

  template <typename T>
  void OtherModeCompute2D(T *input_ptr, T *output_ptr, int64_t p);

  template <typename T>
  void OtherModeCompute3D(T *input_ptr, T *output_ptr, int64_t p);

  int64_t IndexCalculate(int64_t pad_value, int64_t now, int64_t input_value, int64_t o_start, int64_t i_start);

  using SelectFunc =
    std::function<bool(PadV3CpuKernelMod *, const std::vector<kernel::AddressPtr> &,
                       const std::vector<kernel::AddressPtr> &, const std::vector<kernel::AddressPtr> &)>;
  static std::vector<std::pair<KernelAttr, SelectFunc>> func_list_;
  SelectFunc kernel_func_;

  bool paddings_contiguous_;
  int64_t parallelSliceNum_{1};
  int64_t paddings_num_{0};
  int64_t input_dim_{0};
  std::string mode_ = "constant";
  std::string kernel_name_;
  std::vector<int64_t> paddings_;
  std::vector<int64_t> input_shape_;
  std::vector<int64_t> output_shape_;
};
}  // namespace kernel
}  // namespace mindspore

#endif  // MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_PAD_V3_CPU_KERNEL_H_
