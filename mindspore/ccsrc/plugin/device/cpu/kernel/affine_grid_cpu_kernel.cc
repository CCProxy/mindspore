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

#include "plugin/device/cpu/kernel/affine_grid_cpu_kernel.h"
#include <string>
#include <algorithm>
#include "plugin/device/cpu/hal/device/cpu_device_address.h"
#include "utils/ms_utils.h"
#include "kernel/common_utils.h"

namespace mindspore {
namespace kernel {
namespace {
constexpr size_t kAffineGridInputsNum = 2;
constexpr size_t kAffineGridOutputsNum = 1;
constexpr size_t kRowNum12 = 12;
constexpr size_t kRowNum6 = 6;
enum kColNum : size_t {
  kColNum0 = 0,
  kColNum1,
  kColNum2,
  kColNum3,
  kColNum4,
  kColNum5,
  kColNum6,
  kColNum7,
  kColNum8,
  kColNum9,
  kColNum10,
  kColNum11,
  kColNum12
};
}  // namespace
void AffineGridCpuKernelMod::InitKernel(const CNodePtr &kernel_node) {
  MS_EXCEPTION_IF_NULL(kernel_node);
  size_t input_num = common::AnfAlgo::GetInputTensorNum(kernel_node);
  CHECK_KERNEL_INPUTS_NUM(input_num, kAffineGridInputsNum, kernel_name_);
  size_t output_num = common::AnfAlgo::GetOutputTensorNum(kernel_node);
  CHECK_KERNEL_OUTPUTS_NUM(output_num, kAffineGridOutputsNum, kernel_name_);
  output_size_dims_ = AnfAlgo::GetInputDeviceShape(kernel_node, 1);
  output_type_ = AnfAlgo::GetOutputDeviceDataType(kernel_node, 0);
  align_corners_ = common::AnfAlgo::GetNodeAttr<bool>(kernel_node, "align_corners");
  cnode_ptr_ = kernel_node;
  auto kernel_attr = GetKernelAttrFromNode(kernel_node);
  auto [is_match, index] = MatchKernelAttr(kernel_attr, GetOpSupport());
  if (!is_match) {
    MS_LOG(EXCEPTION) << "For AffineGrid, this kernel data type: " << kernel_attr << " is not support.";
  }
  kernel_func_ = func_list_[index].second;
}

template <typename T>
void AffineGridCpuKernelMod::LaunchKernel_3D(const std::vector<kernel::AddressPtr> &inputs,
                                             const std::vector<kernel::AddressPtr> &outputs) {
  std::vector<int64_t> out_shape;
  auto data_theta = reinterpret_cast<T *>(inputs[0]->addr);
  MS_EXCEPTION_IF_NULL(data_theta);
  auto output_size_data = reinterpret_cast<int32_t *>(inputs[1]->addr);
  MS_EXCEPTION_IF_NULL(output_size_data);
  auto output_y = reinterpret_cast<T *>(outputs[0]->addr);
  MS_EXCEPTION_IF_NULL(output_y);
  size_t N = output_size_data[0];
  size_t H = output_size_data[2];
  size_t W = output_size_data[3];
  out_shape.push_back(N);
  out_shape.push_back(H);
  out_shape.push_back(W);
  out_shape.push_back(2);
  Eigen::VectorXd vecX, vecY;
  vecX.setZero(W, 1);
  vecY.setZero(H, 1);
  if (W != 1) {
    vecX = Eigen::VectorXd::LinSpaced(vecX.size(), -1.0, 1.0);
  }
  if (H != 1) {
    vecY = Eigen::VectorXd::LinSpaced(vecY.size(), -1.0, 1.0);
  }
  if (!align_corners_) {
    double xw = static_cast<double>((W - 1)) / static_cast<double>(W);
    double yh = static_cast<double>((H - 1)) / static_cast<double>(H);
    for (size_t i = 0; i < W; i++) {
      vecX[i] = vecX[i] * xw;
    }
    for (size_t i = 0; i < H; i++) {
      vecY[i] = vecY[i] * yh;
    }
  }
  Eigen::MatrixXf all(W * H, kColNum3);
  int64_t row = 0;
  for (size_t i = 0; i < H; i++) {
    for (size_t j = 0; j < W; j++) {
      all(row, 0) = vecX(j);
      all(row, 1) = vecY(i);
      all(row, 2) = 1.0;
      row += 1;
    }
  }
  Eigen::MatrixXf theta(3, 2);
  Eigen::MatrixXf result(H * W, 2);
  float result0;
  float result1;
  int64_t k_num = 0;
  for (size_t n = 0; n < N; n++) {
    theta(0, 0) = static_cast<float>(*(data_theta + (n * kRowNum6) + kColNum0));
    theta(1, 0) = static_cast<float>(*(data_theta + (n * kRowNum6) + kColNum1));
    theta(2, 0) = static_cast<float>(*(data_theta + (n * kRowNum6) + kColNum2));
    theta(0, 1) = static_cast<float>(*(data_theta + (n * kRowNum6) + kColNum3));
    theta(1, 1) = static_cast<float>(*(data_theta + (n * kRowNum6) + kColNum4));
    theta(2, 1) = static_cast<float>(*(data_theta + (n * kRowNum6) + kColNum5));
    result = all * theta;
    for (size_t k = 0; k < H * W; k++) {
      result0 = result(k, 0);
      result1 = result(k, 1);
      *(output_y + k_num) = static_cast<T>(result0);
      *(output_y + k_num + 1) = static_cast<T>(result1);
      k_num += 2;
    }
  }
  common::AnfAlgo::SetOutputInferTypeAndShape({output_type_}, {out_shape}, cnode_ptr_.lock().get());
}

template <typename T>
void AffineGridCpuKernelMod::LaunchKernel_4D(const std::vector<kernel::AddressPtr> &inputs,
                                             const std::vector<kernel::AddressPtr> &outputs) {
  std::vector<int64_t> out_shape;
  auto data_theta = reinterpret_cast<T *>(inputs[0]->addr);
  MS_EXCEPTION_IF_NULL(data_theta);
  auto output_size_data = reinterpret_cast<int32_t *>(inputs[1]->addr);
  MS_EXCEPTION_IF_NULL(output_size_data);
  auto output_y = reinterpret_cast<T *>(outputs[0]->addr);
  MS_EXCEPTION_IF_NULL(output_y);
  size_t N = output_size_data[0];
  size_t D = output_size_data[2];
  size_t H = output_size_data[3];
  size_t W = output_size_data[4];
  out_shape.push_back(N);
  out_shape.push_back(D);
  out_shape.push_back(H);
  out_shape.push_back(W);
  out_shape.push_back(3);
  Eigen::VectorXd vecX, vecY, vecZ;
  vecX.setZero(W, 1);
  vecY.setZero(H, 1);
  vecZ.setZero(D, 1);
  if (W != 1) {
    vecX = Eigen::VectorXd::LinSpaced(vecX.size(), -1.0, 1.0);
  }
  if (H != 1) {
    vecY = Eigen::VectorXd::LinSpaced(vecY.size(), -1.0, 1.0);
  }
  if (D != 1) {
    vecZ = Eigen::VectorXd::LinSpaced(vecZ.size(), -1.0, 1.0);
  }
  if (!align_corners_) {
    double xw = static_cast<double>((W - 1)) / static_cast<double>(W);
    double yh = static_cast<double>((H - 1)) / static_cast<double>(H);
    double zd = static_cast<double>((D - 1)) / static_cast<double>(D);
    for (size_t i = 0; i < W; i++) {
      vecX[i] = vecX[i] * xw;
    }
    for (size_t i = 0; i < H; i++) {
      vecY[i] = vecY[i] * yh;
    }
    for (size_t i = 0; i < D; i++) {
      vecZ[i] = vecZ[i] * zd;
    }
  }
  Eigen::MatrixXf all1(D * H * W, kColNum4);
  int row = 0;
  for (size_t i = 0; i < D; i++) {
    for (size_t j = 0; j < H; j++) {
      for (size_t k = 0; k < W; k++) {
        all1(row, 0) = vecX(k);
        all1(row, 1) = vecY(j);
        all1(row, 2) = vecZ(i);
        all1(row, 3) = 1.0;
        row += 1;
      }
    }
  }
  Eigen::MatrixXf theta(4, 3);
  Eigen::MatrixXf result(D * H * W, 3);
  float result0;
  float result1;
  float result2;
  int64_t k_num = 0;
  for (size_t n = 0; n < N; n++) {
    theta(0, 0) = static_cast<float>(*(data_theta + kColNum0 + (n * kRowNum12)));
    theta(1, 0) = static_cast<float>(*(data_theta + kColNum1 + (n * kRowNum12)));
    theta(2, 0) = static_cast<float>(*(data_theta + kColNum2 + (n * kRowNum12)));
    theta(3, 0) = static_cast<float>(*(data_theta + kColNum3 + (n * kRowNum12)));
    theta(0, 1) = static_cast<float>(*(data_theta + kColNum4 + (n * kRowNum12)));
    theta(1, 1) = static_cast<float>(*(data_theta + kColNum5 + (n * kRowNum12)));
    theta(2, 1) = static_cast<float>(*(data_theta + kColNum6 + (n * kRowNum12)));
    theta(3, 1) = static_cast<float>(*(data_theta + kColNum7 + (n * kRowNum12)));
    theta(0, 2) = static_cast<float>(*(data_theta + kColNum8 + (n * kRowNum12)));
    theta(1, 2) = static_cast<float>(*(data_theta + kColNum9 + (n * kRowNum12)));
    theta(2, 2) = static_cast<float>(*(data_theta + kColNum10 + (n * kRowNum12)));
    theta(3, 2) = static_cast<float>(*(data_theta + kColNum11 + (n * kRowNum12)));
    result = all1 * theta;
    for (size_t k = 0; k < D * H * W; k++) {
      result0 = result(k, 0);
      result1 = result(k, 1);
      result2 = result(k, 2);
      *(output_y + k_num) = static_cast<T>(result0);
      *(output_y + k_num + kColNum1) = static_cast<T>(result1);
      *(output_y + k_num + kColNum2) = static_cast<T>(result2);
      k_num += 3;
    }
  }
  common::AnfAlgo::SetOutputInferTypeAndShape({output_type_}, {out_shape}, cnode_ptr_.lock().get());
}

template <typename T>
bool AffineGridCpuKernelMod::LaunchKernel(const std::vector<kernel::AddressPtr> &inputs,
                                          const std::vector<kernel::AddressPtr> &,
                                          const std::vector<kernel::AddressPtr> &outputs) {
  if (output_size_dims_[0] == 4) {
    LaunchKernel_3D<T>(inputs, outputs);
  } else if (output_size_dims_[0] == 5) {
    LaunchKernel_4D<T>(inputs, outputs);
  }
  return true;
}

std::vector<std::pair<KernelAttr, AffineGridCpuKernelMod::AffineGridFunc>> AffineGridCpuKernelMod::func_list_ = {
  {KernelAttr().AddInputAttr(kNumberTypeFloat32).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeFloat32),
   &AffineGridCpuKernelMod::LaunchKernel<float>},
  {KernelAttr().AddInputAttr(kNumberTypeFloat16).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeFloat16),
   &AffineGridCpuKernelMod::LaunchKernel<float16>}};

std::vector<KernelAttr> AffineGridCpuKernelMod::GetOpSupport() {
  std::vector<KernelAttr> support_list;
  std::transform(func_list_.begin(), func_list_.end(), std::back_inserter(support_list),
                 [](const std::pair<KernelAttr, AffineGridFunc> &item) { return item.first; });
  return support_list;
}

MS_KERNEL_FACTORY_REG(NativeCpuKernelMod, AffineGrid, AffineGridCpuKernelMod);
}  // namespace kernel
}  // namespace mindspore
