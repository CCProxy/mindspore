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

#include "plugin/device/gpu/kernel/math/lp_norm_gpu_kernel.h"
#include <functional>
#include <utility>
#include <string>
#include <algorithm>
#include <set>
#include "abstract/utils.h"
#include "kernel/common_utils.h"
#include "mindspore/core/ops/lp_norm.h"
#include "plugin/device/gpu/kernel/cuda_impl/cuda_ops/lp_norm_impl.cuh"

namespace mindspore {
namespace kernel {
bool LpNormGpuKernelMod::GetLpNormAttr(const BaseOperatorPtr &base_operator) {
  if (kernel_name_ != prim::kPrimLpNorm->name()) {
    MS_LOG(ERROR) << "For '" << prim::kPrimLpNorm->name() << "' , it's kernel name must be equal to LpNorm, but got "
                  << kernel_name_;
    return false;
  }
  auto kernel_ptr = std::make_shared<ops::LpNorm>(base_operator->GetPrim());

  axis_ = kernel_ptr->get_axis();
  p_ = static_cast<float>(kernel_ptr->get_p());
  if (p_ == 0.0f) {
    MS_LOG(ERROR) << "For '" << kernel_name_ << "', it's op attribute 'p' equals to zero is invalid.";
    return false;
  }
  epsilon_ = kernel_ptr->get_epsilon();
  return true;
}

bool LpNormGpuKernelMod::Init(const BaseOperatorPtr &base_operator, const std::vector<KernelTensorPtr> &inputs,
                              const std::vector<KernelTensorPtr> &outputs) {
  kernel_name_ = base_operator->name();
  if (inputs.empty() || outputs.empty()) {
    MS_LOG(ERROR) << "For '" << kernel_name_ << "', it got empty inputs or outputs, which is invalid.";
    return false;
  }
  auto kernel_attr = GetKernelAttrFromTensors(inputs, outputs);
  auto [is_match, index] = MatchKernelAttr(kernel_attr, GetOpSupport());
  if (!is_match) {
    MS_LOG(ERROR) << "For '" << kernel_name_ << "', it does not support this kernel type: " << kernel_attr;
    return false;
  }
  kernel_func_ = func_list_[index].second;
  return GetLpNormAttr(base_operator);
}

int LpNormGpuKernelMod::Resize(const BaseOperatorPtr &base_operator, const std::vector<KernelTensorPtr> &inputs,
                               const std::vector<KernelTensorPtr> &outputs,
                               const std::map<uint32_t, tensor::TensorPtr> &inputsOnHost) {
  if (int ret = KernelMod::Resize(base_operator, inputs, outputs); ret != KRET_OK) {
    return ret;
  }
  unit_size_ = abstract::TypeIdSize(inputs.at(kIndex0)->GetDtype());

  input_shape_.clear();
  auto input_shape = inputs.at(kIndex0)->GetShapeVector();
  (void)std::transform(input_shape.begin(), input_shape.end(), std::back_inserter(input_shape_), LongToSize);
  input_elements_ = std::accumulate(input_shape_.begin(), input_shape_.end(), 1, std::multiplies<size_t>());
  is_null_input_ = (input_elements_ == 0);
  if (is_null_input_) {
    return KRET_OK;
  }

  output_shape_.clear();
  auto output_shape = outputs.at(kIndex0)->GetShapeVector();
  // Ignore dim equal to one.
  for (const auto &dim : output_shape) {
    if (dim != 1) {
      output_shape_.emplace_back(LongToSize(dim));
    }
  }

  output_axis_.clear();
  std::set<size_t> axis_set(axis_.begin(), axis_.end());
  for (size_t i = 0; i < input_shape_.size(); ++i) {
    if (!axis_set.count(i)) {
      output_axis_.emplace_back(i);
    }
  }

  output_stride_.clear();
  output_stride_.resize(output_shape_.size());
  output_stride_[output_stride_.size() - 1] = 1;
  for (int i = static_cast<int>(output_stride_.size() - 2); i >= 0; --i) {
    output_stride_[i] = output_stride_[i + 1] * output_shape[i + 1];
  }
  output_elements_ = std::accumulate(output_shape_.begin(), output_shape_.end(), 1, std::multiplies<size_t>());
  InitWorkSpaceSizeList();
  return KRET_OK;
}

template <typename T>
bool LpNormGpuKernelMod::LaunchKernel(const std::vector<AddressPtr> &inputs, const std::vector<AddressPtr> &workspace,
                                      const std::vector<AddressPtr> &outputs) {
  auto input = reinterpret_cast<T *>(inputs.at(kIndex0)->addr);

  auto device_input_shape = reinterpret_cast<size_t *>(workspace.at(kIndex0)->addr);
  auto device_axis_output = reinterpret_cast<size_t *>(workspace.at(kIndex1)->addr);
  auto device_output_stride = reinterpret_cast<size_t *>(workspace.at(kIndex2)->addr);
  auto output = reinterpret_cast<T *>(outputs.at(kIndex0)->addr);

  CHECK_CUDA_RET_WITH_EXCEPT_NOTRACE(
    cudaMemcpyAsync(device_input_shape, &input_shape_[0], input_shape_.size() * sizeof(size_t), cudaMemcpyHostToDevice,
                    reinterpret_cast<cudaStream_t>(cuda_stream_)),
    "LpNormGpuKernelMod cudaMemcpyAsync input_shape_ failed");

  CHECK_CUDA_RET_WITH_EXCEPT_NOTRACE(
    cudaMemcpyAsync(device_axis_output, &output_axis_[0], output_axis_.size() * sizeof(size_t), cudaMemcpyHostToDevice,
                    reinterpret_cast<cudaStream_t>(cuda_stream_)),
    "LpNormGpuKernelMod cudaMemcpyAsync output_axis_ failed");

  CHECK_CUDA_RET_WITH_EXCEPT_NOTRACE(
    cudaMemcpyAsync(device_output_stride, &output_stride_[0], output_stride_.size() * sizeof(size_t),
                    cudaMemcpyHostToDevice, reinterpret_cast<cudaStream_t>(cuda_stream_)),
    "LpNormGpuKernelMod cudaMemcpyAsync output_shape_ failed");

  // The workspace for device output high precision.
  if constexpr (std::is_same_v<T, half>) {
    CHECK_CUDA_RET_WITH_EXCEPT_NOTRACE(cudaStreamSynchronize(reinterpret_cast<cudaStream_t>(cuda_stream_)),
                                       "cudaStremSynchronize failed");
    constexpr auto high_precision_unit = 2;
    size_t device_output_stride_size = output_elements_ * unit_size_ * high_precision_unit;
    auto middle_output = reinterpret_cast<float *>(
      device::gpu::GPUMemoryAllocator::GetInstance().AllocTensorMem(device_output_stride_size));
    CHECK_CUDA_RET_WITH_ERROR_NOTRACE(cudaMemset(middle_output, 0, device_output_stride_size),
                                      "LpNormGpuKernelMod failed  to set cuda memory to zeros.");
    CalLpNorm(input, device_input_shape, input_shape_.size(), input_elements_, device_axis_output, device_output_stride,
              output_axis_.size(), output_elements_, p_, epsilon_, middle_output, output, device_id_,
              reinterpret_cast<cudaStream_t>(cuda_stream_));
  } else {
    CalLpNorm(input, device_input_shape, input_shape_.size(), input_elements_, device_axis_output, device_output_stride,
              output_axis_.size(), output_elements_, p_, epsilon_, nullptr, output, device_id_,
              reinterpret_cast<cudaStream_t>(cuda_stream_));
  }
  return true;
}

std::vector<std::pair<KernelAttr, LpNormGpuKernelMod::LpNormFunc>> LpNormGpuKernelMod::func_list_ = {
  {KernelAttr().AddInputAttr(kNumberTypeFloat16).AddOutputAttr(kNumberTypeFloat16),
   &LpNormGpuKernelMod::LaunchKernel<half>},
  {KernelAttr().AddInputAttr(kNumberTypeFloat32).AddOutputAttr(kNumberTypeFloat32),
   &LpNormGpuKernelMod::LaunchKernel<float>}};

std::vector<KernelAttr> LpNormGpuKernelMod::GetOpSupport() {
  std::vector<KernelAttr> support_list;
  (void)std::transform(func_list_.begin(), func_list_.end(), std::back_inserter(support_list),
                       [](const std::pair<KernelAttr, LpNormFunc> &pair) { return pair.first; });
  return support_list;
}

MS_KERNEL_FACTORY_REG(NativeGpuKernelMod, LpNorm, LpNormGpuKernelMod);
}  // namespace kernel
}  // namespace mindspore
