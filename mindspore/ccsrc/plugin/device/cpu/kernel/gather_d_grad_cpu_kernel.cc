/**
 * Copyright 2020-2022 Huawei Technologies Co., Ltd
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

#include "plugin/device/cpu/kernel/gather_d_grad_cpu_kernel.h"
#include <algorithm>
#include <functional>
#include <utility>
#include "plugin/device/cpu/hal/device/cpu_device_address.h"

namespace mindspore {
namespace kernel {
namespace {
constexpr size_t kStaticInputNum = 2;
constexpr size_t kDynInputNum = 3;
constexpr auto kAttrDim = "dim";

size_t get_element_num(const std::vector<size_t> &shape) {
  return std::accumulate(shape.begin(), shape.end(), static_cast<std::size_t>(1), std::multiplies<size_t>());
}

template <typename I, typename T>
void GatherDGradCopyTask(size_t cur, std::vector<size_t> *pos, T *input, I *index, const int &dim, T *output,
                         const std::vector<size_t> &output_shape, const std::vector<size_t> &out_cargo_size,
                         const std::vector<size_t> &input_cargo_size) {
  for (size_t i = 0; i < LongToSize(output_shape[cur]); ++i) {
    (*pos)[cur] = i;
    if (cur == output_shape.size() - 1) {
      size_t input_offset = 0;
      size_t out_offset = 0;
      // out offset
      for (size_t j = 0; j < output_shape.size(); ++j) {
        out_offset += (*pos)[j] * out_cargo_size[j];
      }
      // input offset
      size_t cur_index = (*pos)[dim];
      (*pos)[dim] = index[out_offset];
      for (size_t j = 0; j < output_shape.size(); ++j) {
        input_offset += (*pos)[j] * input_cargo_size[j];
      }
      // do copy
      input[input_offset] += output[out_offset];
      (*pos)[dim] = cur_index;
    } else {
      // CopyTask
      GatherDGradCopyTask(cur + 1, pos, input, index, dim, output, output_shape, out_cargo_size, input_cargo_size);
    }
  }
}
}  // namespace

bool GatherDGradCpuKernelMod::Init(const BaseOperatorPtr &base_operator, const std::vector<KernelTensorPtr> &inputs,
                                   const std::vector<KernelTensorPtr> &outputs) {
  kernel_name_ = base_operator->name();
  size_t input_num = inputs.size();
  if (input_num == kStaticInputNum) {
    index_idx_ = 0;
    grad_idx_ = 1;
  } else if (input_num == kDynInputNum) {
    index_idx_ = 1;
    constexpr size_t kDynGradIdx = 2;
    grad_idx_ = kDynGradIdx;
  } else {
    MS_LOG(ERROR) << "For '" << kernel_name_ << "', the number of inputs must be 2 or 3, but got " << input_num;
    return false;
  }

  axis_ = GetValue<int64_t>(base_operator->GetAttr(kAttrDim));
  if (auto ret = MatchKernelFunc(base_operator, inputs, outputs); !ret) {
    return ret;
  }
  return true;
}

int GatherDGradCpuKernelMod::Resize(const BaseOperatorPtr &base_operator, const std::vector<KernelTensorPtr> &inputs,
                                    const std::vector<KernelTensorPtr> &outputs,
                                    const std::map<uint32_t, tensor::TensorPtr> &inputsOnHost) {
  if (auto ret = KernelMod::Resize(base_operator, inputs, outputs, inputsOnHost); ret != KRET_OK) {
    return ret;
  }

  index_shape_ = Convert2SizeT(inputs[index_idx_]->GetShapeVector());
  grad_shape_ = Convert2SizeT(inputs[grad_idx_]->GetShapeVector());
  output_shape_ = Convert2SizeT(outputs[0]->GetShapeVector());
  if (grad_shape_.size() != index_shape_.size() || output_shape_.size() != index_shape_.size()) {
    MS_LOG(ERROR) << "For '" << kernel_name_ << "', the dimension of grad and output must be the equal to the "
                  << "dimension of index: " << index_shape_.size()
                  << ", but got the dimension of grad: " << grad_shape_.size()
                  << ", the dimension of output: " << output_shape_.size();
    return KRET_RESIZE_FAILED;
  }

  return KRET_OK;
}

template <typename I, typename T>
bool GatherDGradCpuKernelMod::LaunchKernel(const std::vector<kernel::AddressPtr> &inputs,
                                           const std::vector<AddressPtr> &,
                                           const std::vector<kernel::AddressPtr> &outputs) {
  size_t index_size = get_element_num(index_shape_) * sizeof(I);
  size_t grad_size = get_element_num(grad_shape_) * sizeof(T);
  size_t output_size = get_element_num(output_shape_) * sizeof(T);
  if (inputs[index_idx_]->size != index_size) {
    MS_LOG(EXCEPTION) << "For '" << kernel_name_ << "', the address size of 'index' must be " << index_size
                      << ", but got " << inputs[index_idx_]->size << ".";
  }
  if (inputs[grad_idx_]->size != grad_size) {
    MS_LOG(EXCEPTION) << "For '" << kernel_name_ << "', the address size of 'grad' must be " << grad_size
                      << ", but got " << inputs[grad_idx_]->size << ".";
  }
  if (outputs[0]->size != output_size) {
    MS_LOG(EXCEPTION) << "For '" << kernel_name_ << "', the address size of output must be " << output_size
                      << ", but got " << outputs[0]->size << ".";
  }

  auto *index = reinterpret_cast<I *>(inputs[index_idx_]->addr);
  auto *grad = reinterpret_cast<T *>(inputs[grad_idx_]->addr);
  auto out = reinterpret_cast<T *>(outputs[0]->addr);
  int output_rank = SizeToInt(output_shape_.size());
  if (axis_ >= output_rank || axis_ < -output_rank) {
    MS_LOG(EXCEPTION) << "For '" << kernel_name_ << "', the value of 'dim' must be in [" << -output_rank << ", "
                      << output_rank << "), but got: " << axis_;
  }
  if (axis_ < 0) {
    axis_ = axis_ + SizeToInt(output_shape_.size());
  }

  // check index
  index_size = get_element_num(index_shape_);
  int max_index = LongToInt(output_shape_[axis_]);
  for (size_t i = 0; i < index_size; ++i) {
    if (index[i] >= max_index || index[i] < -max_index) {
      MS_LOG(EXCEPTION) << "For '" << kernel_name_ << "', the value of 'index' must be in [" << -max_index << ", "
                        << max_index << "), but got: " << index[i];
    }
    if (index[i] < 0) {
      index[i] = max_index + index[i];
    }
  }
  auto out_size = get_element_num(output_shape_);
  auto ret = memset_s(out, out_size * sizeof(T), 0x00, out_size * sizeof(T));
  if (ret != EOK) {
    MS_LOG(EXCEPTION) << "For '" << kernel_name_ << "', memset failed. Error no: " << ret;
  }

  // out_cargo_size
  std::vector<size_t> out_cargo_size = std::vector<size_t>(output_shape_.size(), 1);
  for (int i = out_cargo_size.size() - 2; i >= 0; --i) {
    out_cargo_size[i] = LongToSize(output_shape_[i + 1]) * out_cargo_size[i + 1];
  }
  // grad_cargo_size
  std::vector<size_t> grad_cargo_size = std::vector<size_t>(grad_shape_.size(), 1);
  for (int i = grad_cargo_size.size() - 2; i >= 0; --i) {
    grad_cargo_size[i] = LongToSize(grad_shape_[i + 1]) * grad_cargo_size[i + 1];
  }

  // copy task
  std::vector<size_t> pos(index_shape_.size(), 0);
  GatherDGradCopyTask<I, T>(0, &pos, out, index, axis_, grad, index_shape_, grad_cargo_size, out_cargo_size);
  return true;
}

const std::vector<std::pair<KernelAttr, GatherDGradCpuKernelMod::KernelRunFunc>> &GatherDGradCpuKernelMod::GetFuncList()
  const {
  static const std::vector<std::pair<KernelAttr, GatherDGradCpuKernelMod::KernelRunFunc>> func_list = {
    // For static shape case:
    {KernelAttr().AddInputAttr(kNumberTypeInt32).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeInt32),
     &GatherDGradCpuKernelMod::LaunchKernel<int32_t, int32_t>},
    {KernelAttr().AddInputAttr(kNumberTypeInt32).AddInputAttr(kNumberTypeInt64).AddOutputAttr(kNumberTypeInt64),
     &GatherDGradCpuKernelMod::LaunchKernel<int32_t, int64_t>},
    {KernelAttr().AddInputAttr(kNumberTypeInt32).AddInputAttr(kNumberTypeFloat32).AddOutputAttr(kNumberTypeFloat32),
     &GatherDGradCpuKernelMod::LaunchKernel<int32_t, float>},
    {KernelAttr().AddInputAttr(kNumberTypeInt32).AddInputAttr(kNumberTypeFloat16).AddOutputAttr(kNumberTypeFloat16),
     &GatherDGradCpuKernelMod::LaunchKernel<int32_t, float16>},
    {KernelAttr().AddInputAttr(kNumberTypeInt32).AddInputAttr(kNumberTypeBool).AddOutputAttr(kNumberTypeBool),
     &GatherDGradCpuKernelMod::LaunchKernel<int32_t, bool>},
    {KernelAttr().AddInputAttr(kNumberTypeInt64).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeInt32),
     &GatherDGradCpuKernelMod::LaunchKernel<int64_t, int32_t>},
    {KernelAttr().AddInputAttr(kNumberTypeInt64).AddInputAttr(kNumberTypeInt64).AddOutputAttr(kNumberTypeInt64),
     &GatherDGradCpuKernelMod::LaunchKernel<int64_t, int64_t>},
    {KernelAttr().AddInputAttr(kNumberTypeInt64).AddInputAttr(kNumberTypeFloat32).AddOutputAttr(kNumberTypeFloat32),
     &GatherDGradCpuKernelMod::LaunchKernel<int64_t, float>},
    {KernelAttr().AddInputAttr(kNumberTypeInt64).AddInputAttr(kNumberTypeFloat16).AddOutputAttr(kNumberTypeFloat16),
     &GatherDGradCpuKernelMod::LaunchKernel<int64_t, float16>},
    {KernelAttr().AddInputAttr(kNumberTypeInt64).AddInputAttr(kNumberTypeBool).AddOutputAttr(kNumberTypeBool),
     &GatherDGradCpuKernelMod::LaunchKernel<int64_t, bool>},
    // For dynamic shape case:
    {KernelAttr()
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeInt32)
       .AddOutputAttr(kNumberTypeInt32),
     &GatherDGradCpuKernelMod::LaunchKernel<int32_t, int32_t>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeInt64)
       .AddOutputAttr(kNumberTypeInt64),
     &GatherDGradCpuKernelMod::LaunchKernel<int32_t, int64_t>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeFloat32)
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeFloat32)
       .AddOutputAttr(kNumberTypeFloat32),
     &GatherDGradCpuKernelMod::LaunchKernel<int32_t, float>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeFloat16)
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeFloat16)
       .AddOutputAttr(kNumberTypeFloat16),
     &GatherDGradCpuKernelMod::LaunchKernel<int32_t, float16>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeBool)
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeBool)
       .AddOutputAttr(kNumberTypeBool),
     &GatherDGradCpuKernelMod::LaunchKernel<int32_t, bool>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeInt32)
       .AddOutputAttr(kNumberTypeInt32),
     &GatherDGradCpuKernelMod::LaunchKernel<int64_t, int32_t>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeInt64)
       .AddOutputAttr(kNumberTypeInt64),
     &GatherDGradCpuKernelMod::LaunchKernel<int64_t, int64_t>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeFloat32)
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeFloat32)
       .AddOutputAttr(kNumberTypeFloat32),
     &GatherDGradCpuKernelMod::LaunchKernel<int64_t, float>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeFloat16)
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeFloat16)
       .AddOutputAttr(kNumberTypeFloat16),
     &GatherDGradCpuKernelMod::LaunchKernel<int64_t, float16>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeBool)
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeBool)
       .AddOutputAttr(kNumberTypeBool),
     &GatherDGradCpuKernelMod::LaunchKernel<int64_t, bool>}};

  return func_list;
}

MS_KERNEL_FACTORY_REG(NativeCpuKernelMod, GatherDGrad, GatherDGradCpuKernelMod);
MS_KERNEL_FACTORY_REG(NativeCpuKernelMod, GatherDGradV2, GatherDGradCpuKernelMod);
}  // namespace kernel
}  // namespace mindspore
