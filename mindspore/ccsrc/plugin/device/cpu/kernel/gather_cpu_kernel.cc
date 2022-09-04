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

#include "plugin/device/cpu/kernel/gather_cpu_kernel.h"
#include <algorithm>
#include <utility>
#include <complex>
#include "plugin/device/cpu/hal/device/cpu_device_address.h"
#include "nnacl/gather_parameter.h"
#include "nnacl/base/gather_base.h"
#include "include/common/thread_pool.h"

namespace mindspore {
namespace kernel {
namespace {
using complex64 = std::complex<float>;
using complex128 = std::complex<double>;
constexpr size_t kGatherInputsNum = 2;
constexpr size_t kGatherOutputsNum = 1;
constexpr size_t kGatherInputParamsMaxDim = 7;
}  // namespace
bool GatherCpuKernelMod::Init(const BaseOperatorPtr &base_operator, const std::vector<KernelTensorPtr> &inputs,
                              const std::vector<KernelTensorPtr> &outputs) {
  kernel_name_ = base_operator->name();
  size_t input_num = inputs.size();
  if (input_num == kGatherInputsNum + 1) {
    is_dynamic_shape_ = true;
    MS_LOG(DEBUG) << " GatherCPUKernel running in Dynamic Mode.";
  } else if (input_num == kGatherInputsNum) {
    axis_ = GetValue<int64_t>(base_operator->GetAttr("axis"));
    MS_LOG(DEBUG) << " GatherCPUKernel running in Normal Mode.";
  } else {
    MS_LOG(EXCEPTION) << "Argument number is " << input_num << ", but GatherCPUKernel needs 2.";
  }
  auto kernel_attr = GetKernelAttrFromTensors(inputs, outputs);
  auto [is_match, index] = MatchKernelAttr(kernel_attr, GetOpSupport());
  if (!is_match) {
    MS_LOG(ERROR) << "For '" << kernel_name_ << "', it does not support this kernel type: " << kernel_attr;
    return false;
  }
  kernel_func_ = func_list_[index].second;
  input_type_size_ = abstract::TypeIdSize(kernel_attr.GetInputAttr(kIndex0).first);
  indices_type_size_ = abstract::TypeIdSize(kernel_attr.GetInputAttr(kIndex1).first);
  if (is_dynamic_shape_) {
    axis_type_size_ = abstract::TypeIdSize(kernel_attr.GetInputAttr(kIndex2).first);
  }
  return true;
}

int GatherCpuKernelMod::Resize(const BaseOperatorPtr &base_operator, const std::vector<KernelTensorPtr> &inputs,
                               const std::vector<KernelTensorPtr> &outputs,
                               const std::map<uint32_t, tensor::TensorPtr> &inputsOnHost) {
  ResetResource();
  input_shape_ = inputs[kIndexZero]->GetShapeVector();
  indices_shape_ = inputs[kIndexOne]->GetShapeVector();
  output_shape_ = outputs[kIndexZero]->GetShapeVector();
  is_null_input_ = input_shape_.empty() || indices_shape_.empty() || output_shape_.empty();
  if (is_null_input_) {
    InitSizeLists();
    return KRET_OK;
  }
  if (input_shape_.size() > kGatherInputParamsMaxDim) {
    MS_LOG(EXCEPTION) << "For '" << kernel_name_ << "', the dimension of 'input_params' should be "
                      << kGatherInputParamsMaxDim << "D or lower, but got " << input_shape_.size() << ".";
  }
  auto it_x = std::find_if(input_shape_.begin(), input_shape_.end(), [](int64_t sh) { return sh <= 0; });
  auto it_y = std::find_if(indices_shape_.begin(), indices_shape_.end(), [](int64_t sh) { return sh <= 0; });
  if (it_x != input_shape_.end() || it_y != indices_shape_.end()) {
    return KRET_UNKNOWN_SHAPE;
  }
  InitSizeLists();
  return KRET_OK;
}

template <typename T>
bool GatherCpuKernelMod::LaunchKernel(const std::vector<kernel::AddressPtr> &inputs,
                                      const std::vector<kernel::AddressPtr> &outputs) {
  CHECK_KERNEL_OUTPUTS_NUM(outputs.size(), kGatherOutputsNum, kernel_name_);
  const auto *input_tensor = reinterpret_cast<int8_t *>(inputs[0]->addr);
  const auto *indices_data = reinterpret_cast<int32_t *>(inputs[1]->addr);
  auto *output_addr = reinterpret_cast<int8_t *>(outputs[0]->addr);

  if (inputs.size() == kGatherInputsNum + 1) {
    axis_ = reinterpret_cast<int64_t *>(inputs[kIndex2]->addr)[0];
  }

  int dims = SizeToInt(input_shape_.size());
  if (axis_ < -dims || axis_ >= dims) {
    MS_LOG(ERROR) << "For '" << kernel_name_ << "', the 'axis' should be in the range [-" << dims << ", " << dims
                  << "), but got " << axis_ << ".";
    return false;
  } else if (axis_ < 0) {
    axis_ = axis_ + dims;
  }

  size_t outer_size = 1, inner_size = 1;
  auto axis = static_cast<size_t>(axis_);
  for (size_t i = 0; i < axis; ++i) {
    outer_size *= LongToSize(input_shape_.at(i));
  }
  for (size_t i = axis + 1; i < input_shape_.size(); ++i) {
    inner_size *= LongToSize(input_shape_.at(i));
  }
  size_t indices_element_size = 1;
  for (size_t i = 0; i < indices_shape_.size(); i++) {
    indices_element_size *= LongToSize(indices_shape_.at(i));
  }
  auto limit = LongToSize(input_shape_.at(axis));
  size_t byte_inner_size = inner_size * sizeof(T);
  size_t byte_out_stride = indices_element_size * byte_inner_size;
  auto task = [&](size_t start, size_t end) {
    int count = SizeToInt(end - start);
    const int8_t *in = input_tensor + start * limit * byte_inner_size;
    int8_t *out = output_addr + start * byte_out_stride;
    int ret = Gather(in, count, byte_inner_size, limit, indices_data, indices_element_size, out, byte_out_stride);
    if (ret != 0) {
      MS_LOG(EXCEPTION) << "For '" << kernel_name_ << "', error_code[" << ret << "]";
    }
  };
  ParallelLaunchAutoSearch(task, outer_size, this, &parallel_search_info_);
  return true;
}

std::vector<std::pair<KernelAttr, GatherCpuKernelMod::GatherFunc>> GatherCpuKernelMod::func_list_ = {
  {KernelAttr().AddInputAttr(kNumberTypeUInt8).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeUInt8),
   &GatherCpuKernelMod::LaunchKernel<uint8_t>},
  {KernelAttr().AddInputAttr(kNumberTypeUInt16).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeUInt16),
   &GatherCpuKernelMod::LaunchKernel<uint16_t>},
  {KernelAttr().AddInputAttr(kNumberTypeUInt32).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeUInt32),
   &GatherCpuKernelMod::LaunchKernel<uint32_t>},
  {KernelAttr().AddInputAttr(kNumberTypeUInt64).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeUInt64),
   &GatherCpuKernelMod::LaunchKernel<uint64_t>},
  {KernelAttr().AddInputAttr(kNumberTypeInt8).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeInt8),
   &GatherCpuKernelMod::LaunchKernel<int8_t>},
  {KernelAttr().AddInputAttr(kNumberTypeInt16).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeInt16),
   &GatherCpuKernelMod::LaunchKernel<int16_t>},
  {KernelAttr().AddInputAttr(kNumberTypeInt32).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeInt32),
   &GatherCpuKernelMod::LaunchKernel<int32_t>},
  {KernelAttr().AddInputAttr(kNumberTypeInt64).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeInt64),
   &GatherCpuKernelMod::LaunchKernel<int64_t>},
  {KernelAttr().AddInputAttr(kNumberTypeFloat16).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeFloat16),
   &GatherCpuKernelMod::LaunchKernel<float16>},
  {KernelAttr().AddInputAttr(kNumberTypeFloat32).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeFloat32),
   &GatherCpuKernelMod::LaunchKernel<float>},
  {KernelAttr().AddInputAttr(kNumberTypeFloat64).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeFloat64),
   &GatherCpuKernelMod::LaunchKernel<double>},
  {KernelAttr().AddInputAttr(kNumberTypeBool).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeBool),
   &GatherCpuKernelMod::LaunchKernel<bool>},
  {KernelAttr().AddInputAttr(kNumberTypeComplex64).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeComplex64),
   &GatherCpuKernelMod::LaunchKernel<complex64>},
  {KernelAttr().AddInputAttr(kNumberTypeComplex128).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeComplex128),
   &GatherCpuKernelMod::LaunchKernel<complex128>},
  // dynamic shape ends
  {KernelAttr()
     .AddInputAttr(kNumberTypeUInt8)
     .AddInputAttr(kNumberTypeInt32)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeUInt8),
   &GatherCpuKernelMod::LaunchKernel<uint8_t>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeUInt16)
     .AddInputAttr(kNumberTypeInt32)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeUInt16),
   &GatherCpuKernelMod::LaunchKernel<uint16_t>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeUInt32)
     .AddInputAttr(kNumberTypeInt32)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeUInt32),
   &GatherCpuKernelMod::LaunchKernel<uint32_t>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeUInt64)
     .AddInputAttr(kNumberTypeInt32)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeUInt64),
   &GatherCpuKernelMod::LaunchKernel<uint64_t>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeInt8)
     .AddInputAttr(kNumberTypeInt32)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeInt8),
   &GatherCpuKernelMod::LaunchKernel<int8_t>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeInt16)
     .AddInputAttr(kNumberTypeInt32)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeInt16),
   &GatherCpuKernelMod::LaunchKernel<int16_t>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeInt32)
     .AddInputAttr(kNumberTypeInt32)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeInt32),
   &GatherCpuKernelMod::LaunchKernel<int32_t>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt32)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeInt64),
   &GatherCpuKernelMod::LaunchKernel<int64_t>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeFloat16)
     .AddInputAttr(kNumberTypeInt32)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeFloat16),
   &GatherCpuKernelMod::LaunchKernel<float16>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeFloat32)
     .AddInputAttr(kNumberTypeInt32)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeFloat32),
   &GatherCpuKernelMod::LaunchKernel<float>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeFloat64)
     .AddInputAttr(kNumberTypeInt32)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeFloat64),
   &GatherCpuKernelMod::LaunchKernel<double>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeBool)
     .AddInputAttr(kNumberTypeInt32)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeBool),
   &GatherCpuKernelMod::LaunchKernel<bool>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeComplex64)
     .AddInputAttr(kNumberTypeInt32)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeComplex64),
   &GatherCpuKernelMod::LaunchKernel<complex64>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeComplex128)
     .AddInputAttr(kNumberTypeInt32)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeComplex128),
   &GatherCpuKernelMod::LaunchKernel<complex128>}};

std::vector<KernelAttr> GatherCpuKernelMod::GetOpSupport() {
  std::vector<KernelAttr> support_list;
  (void)std::transform(func_list_.begin(), func_list_.end(), std::back_inserter(support_list),
                       [](const std::pair<KernelAttr, GatherFunc> &pair) { return pair.first; });

  return support_list;
}

MS_KERNEL_FACTORY_REG(NativeCpuKernelMod, Gather, GatherCpuKernelMod);
}  // namespace kernel
}  // namespace mindspore
