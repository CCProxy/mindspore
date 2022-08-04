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
#ifndef MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_SEGMENT_MIN_CPU_KERNEL_H_
#define MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_SEGMENT_MIN_CPU_KERNEL_H_
#include <vector>
#include <memory>
#include <string>
#include <utility>
#include "plugin/device/cpu/kernel/cpu_kernel.h"
#include "plugin/factory/ms_factory.h"

namespace mindspore {
namespace kernel {
class SegmentMinCPUKernelMod : public DeprecatedNativeCpuKernelMod {
 public:
  SegmentMinCPUKernelMod() = default;
  ~SegmentMinCPUKernelMod() override = default;

  void InitKernel(const CNodePtr &node) override;

  bool Launch(const std::vector<AddressPtr> &inputs, const std::vector<AddressPtr> &workspace,
              const std::vector<AddressPtr> &outputs) override {
    return kernel_func_(this, inputs, workspace, outputs);
  };

  std::vector<KernelAttr> GetOpSupport() override;

 private:
  ShapeVector input_x_shape_;
  ShapeVector segment_ids_shape_;
  ShapeVector output_shape_;
  size_t input_x_num_{0};
  size_t segment_ids_num_{0};
  size_t output_num_{0};
  TypeId output_dtype_{kTypeUnknown};
  using SegmentMinFunc =
    std::function<bool(SegmentMinCPUKernelMod *, const std::vector<kernel::AddressPtr> &,
                       const std::vector<kernel::AddressPtr> &, const std::vector<kernel::AddressPtr> &)>;
  static std::vector<std::pair<KernelAttr, SegmentMinFunc>> func_list_;
  SegmentMinFunc kernel_func_;

  template <typename T>
  std::vector<int64_t> CalcSegmentIds(const T *segment_ids_data_addr) const;

  template <typename T1, typename T2>
  bool LaunchKernel(const std::vector<kernel::AddressPtr> &inputs, const std::vector<kernel::AddressPtr> &workspace,
                    const std::vector<kernel::AddressPtr> &outputs);
};
}  // namespace kernel
}  // namespace mindspore

#endif  // MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_SEGMENT_MIN_CPU_KERNEL_H_
