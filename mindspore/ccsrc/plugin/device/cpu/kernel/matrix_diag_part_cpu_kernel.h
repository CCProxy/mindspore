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

#ifndef MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_EIGEN_MATRIX_DIAG_PART_H
#define MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_EIGEN_MATRIX_DIAG_PART_H
#include <vector>
#include <complex>
#include <utility>
#include "plugin/device/cpu/kernel/cpu_kernel.h"
#include "plugin/factory/ms_factory.h"

namespace mindspore {
namespace kernel {
class MatrixDiagPartCpuKernelMod : public DeprecatedNativeCpuKernelMod {
 public:
  MatrixDiagPartCpuKernelMod() = default;
  ~MatrixDiagPartCpuKernelMod() override = default;
  void InitKernel(const CNodePtr &kernel_node) override;
  bool Launch(const std::vector<AddressPtr> &inputs, const std::vector<AddressPtr> &,
              const std::vector<AddressPtr> &outputs) override {
    return kernel_func_(this, inputs, outputs);
  }

  std::vector<KernelAttr> GetOpSupport() override;

 private:
  template <typename T>
  bool LaunchKernel(const std::vector<kernel::AddressPtr> &inputs, const std::vector<kernel::AddressPtr> &outputs);
  using MatrixDiagPartFunc = std::function<bool(MatrixDiagPartCpuKernelMod *, const std::vector<kernel::AddressPtr> &,
                                                const std::vector<kernel::AddressPtr> &)>;
  static std::vector<std::pair<KernelAttr, MatrixDiagPartFunc>> func_list_;
  MatrixDiagPartFunc kernel_func_;

  // <Super_matrix_diag_align, Sub_matrix_diag_align>
  std::pair<MatrixDiag::Alignment, MatrixDiag::Alignment> alignment_{MatrixDiag::RIGHT, MatrixDiag::LEFT};
  ShapeVector shapes_{};
  int64_t out_range_size_{1};
  size_t dim_size_{1};
  int64_t m_{1};
  int64_t n_{1};
  ShapeVector out_shapes_{};
  CNodeWeakPtr node_wpt_;
};
}  // namespace kernel
}  // namespace mindspore
#endif  // MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_EIGEN_MATRIX_DIAG_PART_H