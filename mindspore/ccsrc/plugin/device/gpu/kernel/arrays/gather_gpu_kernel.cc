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

#include "plugin/device/gpu/kernel/arrays/gather_gpu_kernel.h"
#include <string>
#include <algorithm>

namespace mindspore {
namespace kernel {
namespace {
std::pair<bool, int64_t> GetDimValue(const ValuePtr &dim_value_ptr) {
  MS_EXCEPTION_IF_NULL(dim_value_ptr);
  int64_t dim_v = 0;
  bool value_type_error = false;
  if (dim_value_ptr->isa<tensor::Tensor>()) {
    auto dim_tensor = dim_value_ptr->cast<tensor::TensorPtr>();
    MS_EXCEPTION_IF_NULL(dim_tensor);
    size_t data_size = dim_tensor->DataSize();
    MS_EXCEPTION_IF_CHECK_FAIL(data_size == 1, "dim value is not equal to one!");
    auto dim_type_id = dim_tensor->data_type();
    if (dim_type_id == kNumberTypeInt32) {
      auto dim_data32 = reinterpret_cast<int *>(dim_tensor->data_c());
      MS_EXCEPTION_IF_NULL(dim_data32);
      dim_v = static_cast<int64_t>(*dim_data32);
    } else if (dim_type_id == kNumberTypeInt64) {
      auto dim_data64 = reinterpret_cast<int64_t *>(dim_tensor->data_c());
      MS_EXCEPTION_IF_NULL(dim_data64);
      dim_v = static_cast<int64_t>(*dim_data64);
    } else {
      value_type_error = true;
    }
  } else {
    if (dim_value_ptr->isa<Int32Imm>() || dim_value_ptr->isa<Int64Imm>()) {
      dim_v = GetValue<int64_t>(dim_value_ptr);
    } else {
      value_type_error = true;
    }
  }

  if (value_type_error) {
    MS_LOG(ERROR) << "For GatherD, 'dim' must be one of these types: [int32/int64].";
    return {false, dim_v};
  }
  return {true, dim_v};
}
}  // namespace

std::vector<std::pair<KernelAttr, GatherFwdGpuKernelMod::GatherFwdFunc>> GatherFwdGpuKernelMod::func_list_ = {
  // For static shape case:
  {KernelAttr().AddInputAttr(kNumberTypeFloat64).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeFloat64),
   &GatherFwdGpuKernelMod::LaunchKernel<double, int>},
  {KernelAttr().AddInputAttr(kNumberTypeFloat64).AddInputAttr(kNumberTypeInt64).AddOutputAttr(kNumberTypeFloat64),
   &GatherFwdGpuKernelMod::LaunchKernel<double, int64_t>},
  {KernelAttr().AddInputAttr(kNumberTypeFloat32).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeFloat32),
   &GatherFwdGpuKernelMod::LaunchKernel<float, int>},
  {KernelAttr().AddInputAttr(kNumberTypeFloat32).AddInputAttr(kNumberTypeInt64).AddOutputAttr(kNumberTypeFloat32),
   &GatherFwdGpuKernelMod::LaunchKernel<float, int64_t>},
  {KernelAttr().AddInputAttr(kNumberTypeFloat16).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeFloat16),
   &GatherFwdGpuKernelMod::LaunchKernel<half, int>},
  {KernelAttr().AddInputAttr(kNumberTypeFloat16).AddInputAttr(kNumberTypeInt64).AddOutputAttr(kNumberTypeFloat16),
   &GatherFwdGpuKernelMod::LaunchKernel<half, int64_t>},
  {KernelAttr().AddInputAttr(kNumberTypeInt32).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeInt32),
   &GatherFwdGpuKernelMod::LaunchKernel<int, int>},
  {KernelAttr().AddInputAttr(kNumberTypeInt32).AddInputAttr(kNumberTypeInt64).AddOutputAttr(kNumberTypeInt32),
   &GatherFwdGpuKernelMod::LaunchKernel<int, int64_t>},
  {KernelAttr().AddInputAttr(kNumberTypeInt8).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeInt8),
   &GatherFwdGpuKernelMod::LaunchKernel<int8_t, int>},
  {KernelAttr().AddInputAttr(kNumberTypeInt8).AddInputAttr(kNumberTypeInt64).AddOutputAttr(kNumberTypeInt8),
   &GatherFwdGpuKernelMod::LaunchKernel<int8_t, int64_t>},
  {KernelAttr().AddInputAttr(kNumberTypeInt16).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeInt16),
   &GatherFwdGpuKernelMod::LaunchKernel<int16_t, int>},
  {KernelAttr().AddInputAttr(kNumberTypeInt16).AddInputAttr(kNumberTypeInt64).AddOutputAttr(kNumberTypeInt16),
   &GatherFwdGpuKernelMod::LaunchKernel<int16_t, int64_t>},
  {KernelAttr().AddInputAttr(kNumberTypeInt64).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeInt64),
   &GatherFwdGpuKernelMod::LaunchKernel<int64_t, int>},
  {KernelAttr().AddInputAttr(kNumberTypeInt64).AddInputAttr(kNumberTypeInt64).AddOutputAttr(kNumberTypeInt64),
   &GatherFwdGpuKernelMod::LaunchKernel<int64_t, int64_t>},
  {KernelAttr().AddInputAttr(kNumberTypeUInt32).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeUInt32),
   &GatherFwdGpuKernelMod::LaunchKernel<uint, int>},
  {KernelAttr().AddInputAttr(kNumberTypeUInt32).AddInputAttr(kNumberTypeInt64).AddOutputAttr(kNumberTypeUInt32),
   &GatherFwdGpuKernelMod::LaunchKernel<uint, int64_t>},
  {KernelAttr().AddInputAttr(kNumberTypeUInt8).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeUInt8),
   &GatherFwdGpuKernelMod::LaunchKernel<uchar, int>},
  {KernelAttr().AddInputAttr(kNumberTypeUInt8).AddInputAttr(kNumberTypeInt64).AddOutputAttr(kNumberTypeUInt8),
   &GatherFwdGpuKernelMod::LaunchKernel<uchar, int64_t>},
  {KernelAttr().AddInputAttr(kNumberTypeBool).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeBool),
   &GatherFwdGpuKernelMod::LaunchKernel<bool, int>},
  {KernelAttr().AddInputAttr(kNumberTypeBool).AddInputAttr(kNumberTypeInt64).AddOutputAttr(kNumberTypeBool),
   &GatherFwdGpuKernelMod::LaunchKernel<bool, int64_t>},
  {KernelAttr().AddInputAttr(kNumberTypeUInt32).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeUInt32),
   &GatherFwdGpuKernelMod::LaunchKernel<uint32_t, int>},
  {KernelAttr().AddInputAttr(kNumberTypeUInt32).AddInputAttr(kNumberTypeInt64).AddOutputAttr(kNumberTypeUInt32),
   &GatherFwdGpuKernelMod::LaunchKernel<uint32_t, int64_t>},
  {KernelAttr().AddInputAttr(kNumberTypeUInt64).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeUInt64),
   &GatherFwdGpuKernelMod::LaunchKernel<uint64_t, int>},
  {KernelAttr().AddInputAttr(kNumberTypeUInt64).AddInputAttr(kNumberTypeInt64).AddOutputAttr(kNumberTypeUInt64),
   &GatherFwdGpuKernelMod::LaunchKernel<uint64_t, int64_t>},
  {KernelAttr().AddInputAttr(kNumberTypeUInt16).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeUInt16),
   &GatherFwdGpuKernelMod::LaunchKernel<uint16_t, int>},
  {KernelAttr().AddInputAttr(kNumberTypeUInt16).AddInputAttr(kNumberTypeInt64).AddOutputAttr(kNumberTypeUInt16),
   &GatherFwdGpuKernelMod::LaunchKernel<uint16_t, int64_t>},
  // For dynamic shape case:
  {KernelAttr()
     .AddInputAttr(kNumberTypeFloat64)
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt32)
     .AddOutputAttr(kNumberTypeFloat64),
   &GatherFwdGpuKernelMod::LaunchKernel<double, int>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeFloat64)
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeFloat64),
   &GatherFwdGpuKernelMod::LaunchKernel<double, int64_t>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeFloat32)
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt32)
     .AddOutputAttr(kNumberTypeFloat32),
   &GatherFwdGpuKernelMod::LaunchKernel<float, int>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeFloat32)
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeFloat32),
   &GatherFwdGpuKernelMod::LaunchKernel<float, int64_t>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeFloat16)
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt32)
     .AddOutputAttr(kNumberTypeFloat16),
   &GatherFwdGpuKernelMod::LaunchKernel<half, int>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeFloat16)
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeFloat16),
   &GatherFwdGpuKernelMod::LaunchKernel<half, int64_t>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeInt32)
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt32)
     .AddOutputAttr(kNumberTypeInt32),
   &GatherFwdGpuKernelMod::LaunchKernel<int, int>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeInt32)
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeInt32),
   &GatherFwdGpuKernelMod::LaunchKernel<int, int64_t>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeInt8)
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt32)
     .AddOutputAttr(kNumberTypeInt8),
   &GatherFwdGpuKernelMod::LaunchKernel<int8_t, int>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeInt8)
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeInt8),
   &GatherFwdGpuKernelMod::LaunchKernel<int8_t, int64_t>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeInt16)
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt32)
     .AddOutputAttr(kNumberTypeInt16),
   &GatherFwdGpuKernelMod::LaunchKernel<int16_t, int>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeInt16)
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeInt16),
   &GatherFwdGpuKernelMod::LaunchKernel<int16_t, int64_t>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt32)
     .AddOutputAttr(kNumberTypeInt64),
   &GatherFwdGpuKernelMod::LaunchKernel<int64_t, int>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeInt64),
   &GatherFwdGpuKernelMod::LaunchKernel<int64_t, int64_t>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeUInt32)
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt32)
     .AddOutputAttr(kNumberTypeUInt32),
   &GatherFwdGpuKernelMod::LaunchKernel<uint, int>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeUInt32)
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeUInt32),
   &GatherFwdGpuKernelMod::LaunchKernel<uint, int64_t>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeUInt8)
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt32)
     .AddOutputAttr(kNumberTypeUInt8),
   &GatherFwdGpuKernelMod::LaunchKernel<uchar, int>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeUInt8)
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeUInt8),
   &GatherFwdGpuKernelMod::LaunchKernel<uchar, int64_t>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeBool)
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt32)
     .AddOutputAttr(kNumberTypeBool),
   &GatherFwdGpuKernelMod::LaunchKernel<bool, int>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeBool)
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeBool),
   &GatherFwdGpuKernelMod::LaunchKernel<bool, int64_t>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeUInt32)
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt32)
     .AddOutputAttr(kNumberTypeUInt32),
   &GatherFwdGpuKernelMod::LaunchKernel<uint32_t, int>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeUInt32)
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeUInt32),
   &GatherFwdGpuKernelMod::LaunchKernel<uint32_t, int64_t>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeUInt64)
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt32)
     .AddOutputAttr(kNumberTypeUInt64),
   &GatherFwdGpuKernelMod::LaunchKernel<uint64_t, int>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeUInt64)
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeUInt64),
   &GatherFwdGpuKernelMod::LaunchKernel<uint64_t, int64_t>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeUInt16)
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt32)
     .AddOutputAttr(kNumberTypeUInt16),
   &GatherFwdGpuKernelMod::LaunchKernel<uint16_t, int>},
  {KernelAttr()
     .AddInputAttr(kNumberTypeUInt16)
     .AddInputAttr(kNumberTypeInt64)
     .AddInputAttr(kNumberTypeInt64)
     .AddOutputAttr(kNumberTypeUInt16),
   &GatherFwdGpuKernelMod::LaunchKernel<uint16_t, int64_t>}};

bool GatherFwdGpuKernelMod::SetDimParam(int64_t dim_value) {
  int64_t x_rank = SizeToLong(input_shapes_.size());
  if (dim_value < -x_rank || dim_value >= x_rank) {
    MS_LOG(ERROR) << "For '" << kernel_name_ << "', the 'dim' must be in the range [-" << x_rank << "," << x_rank
                  << "), but got " << dim_value;
    return false;
  }
  if (dim_value < 0) {
    dim_value += x_rank;
  }

  if (input_shapes_.size() <= LongToSize(dim_value) || output_shapes_.size() <= LongToSize(dim_value)) {
    MS_LOG(EXCEPTION) << "Rank should be great than " << dim_value << ", but got inputs size " << input_shapes_.size()
                      << ", outputs size: " << output_shapes_.size();
  }

  size_t dim_before_axis = 1;
  for (size_t i = 0; i < LongToSize(dim_value); i++) {
    dim_before_axis *= output_shapes_[i];
  }
  size_t dim_at_axis_input = input_shapes_[LongToSize(dim_value)];
  size_t dim_at_axis_output = output_shapes_[LongToSize(dim_value)];
  size_t dim_after_axis = 1;
  for (size_t i = LongToSize(dim_value) + 1; i < output_shapes_.size(); i++) {
    dim_after_axis *= output_shapes_[i];
  }

  const size_t k2Idx = 2;
  const size_t k3Idx = 3;
  dims_[0] = dim_before_axis;
  dims_[1] = dim_at_axis_input;
  dims_[k2Idx] = dim_at_axis_output;
  dims_[k3Idx] = dim_after_axis;
  return true;
}

bool GatherFwdGpuKernelMod::Init(const BaseOperatorPtr &base_operator, const std::vector<KernelTensorPtr> &inputs,
                                 const std::vector<KernelTensorPtr> &outputs) {
  kernel_name_ = base_operator->name();
  size_t input_num = inputs.size();
  const size_t kStaticInputNum = 2;
  const size_t kDynInputNum = 3;
  if (input_num == kStaticInputNum) {
    is_dynamic_case_ = false;
  } else if (input_num == kDynInputNum) {
    is_dynamic_case_ = true;
    const size_t kDynIndexIdx = 2;
    index_idx_ = kDynIndexIdx;
  } else {
    MS_LOG(ERROR) << "For '" << kernel_name_ << "', the number of inputs must be 2 or 3, but got " << input_num;
    return false;
  }

  auto kernel_attr = GetKernelAttrFromTensors(inputs, outputs);
  auto [is_match, index] = MatchKernelAttr(kernel_attr, GetOpSupport());
  if (!is_match) {
    MS_LOG(ERROR) << "For '" << kernel_name_ << "', it does not support this kernel type: " << kernel_attr;
    return false;
  }
  kernel_func_ = func_list_[index].second;

  return true;
}

int GatherFwdGpuKernelMod::Resize(const BaseOperatorPtr &base_operator, const std::vector<KernelTensorPtr> &inputs,
                                  const std::vector<KernelTensorPtr> &outputs,
                                  const std::map<uint32_t, tensor::TensorPtr> &inputsOnHost) {
  int ret = KernelMod::Resize(base_operator, inputs, outputs, inputsOnHost);
  if (ret != KRET_OK) {
    return ret;
  }

  auto input_shapes = inputs[0]->GetShapeVector();
  auto index_shapes = inputs[index_idx_]->GetShapeVector();
  auto output_shapes = outputs[0]->GetShapeVector();

  input_shapes_.clear();
  index_shapes_.clear();
  output_shapes_.clear();
  std::transform(input_shapes.cbegin(), input_shapes.cend(), std::back_inserter(input_shapes_), LongToSize);
  std::transform(index_shapes.cbegin(), index_shapes.cend(), std::back_inserter(index_shapes_), LongToSize);
  std::transform(output_shapes.cbegin(), output_shapes.cend(), std::back_inserter(output_shapes_), LongToSize);

  is_null_input_ = CHECK_SHAPE_NULL(input_shapes_, kernel_name_, "input") ||
                   CHECK_SHAPE_NULL(index_shapes_, kernel_name_, "input_indices") ||
                   CHECK_SHAPE_NULL(output_shapes_, kernel_name_, "output");
  if (is_null_input_) {
    return KRET_OK;
  }

  if (input_shapes_.size() != index_shapes_.size() || input_shapes_.size() != output_shapes_.size()) {
    MS_LOG(ERROR) << "For '" << kernel_name_ << "', the dimension of input and output must be the equal to the "
                  << "dimension of index: " << index_shapes_.size()
                  << ", but got the dimension of input: " << input_shapes_.size()
                  << ", the dimension of output: " << output_shapes_.size();
    return KRET_RESIZE_FAILED;
  }

  int64_t dim_value = 0;
  if (!is_dynamic_case_) {
    const std::string kAttrDim = "dim";
    auto prim = base_operator->GetPrim();
    MS_EXCEPTION_IF_NULL(prim);
    auto dim_attr = prim->GetAttr(kAttrDim);
    if (dim_attr == nullptr) {
      return KRET_RESIZE_FAILED;
    }

    auto value_res = GetDimValue(dim_attr);
    if (!value_res.first) {
      return KRET_RESIZE_FAILED;
    }
    dim_value = value_res.second;
  } else {
    if (!GetDynamicAttrIntValue(inputs, 1, inputsOnHost, kernel_name_, &dim_value)) {
      MS_LOG(ERROR) << "For '" << kernel_name_ << "', dim value must be valid.";
      return KRET_RESIZE_FAILED;
    }
  }
  if (!SetDimParam(dim_value)) {
    return KRET_RESIZE_FAILED;
  }

  return KRET_OK;
}

std::vector<KernelAttr> GatherFwdGpuKernelMod::GetOpSupport() {
  std::vector<KernelAttr> support_list;
  (void)std::transform(func_list_.begin(), func_list_.end(), std::back_inserter(support_list),
                       [](const std::pair<KernelAttr, GatherFwdFunc> &pair) { return pair.first; });
  return support_list;
}

MS_KERNEL_FACTORY_REG(NativeGpuKernelMod, GatherD, GatherFwdGpuKernelMod);
}  // namespace kernel
}  // namespace mindspore
