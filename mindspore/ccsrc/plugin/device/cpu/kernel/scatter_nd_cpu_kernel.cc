/**
 * Copyright 2021-2022 Huawei Technologies Co., Ltd
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

#include "plugin/device/cpu/kernel/scatter_nd_cpu_kernel.h"
#include <algorithm>
#include <string>
#include "include/common/thread_pool.h"
#include "plugin/device/cpu/hal/device/cpu_device_address.h"

namespace mindspore {
namespace kernel {
namespace {
constexpr size_t kScatterNdInputSize = 2;
constexpr size_t kScatterNdOutputSize = 1;
constexpr size_t kMinIndiceRank = 2;
constexpr char kKernelName[] = "ScatterNd";

template <typename S, typename T>
struct ComputeParams {
  T *target_{nullptr};
  S *indices_{nullptr};
  T *updates_{nullptr};
  int unit_size_{0};
  int indices_unit_rank_{0};
  std::vector<int> *out_strides_{nullptr};
  size_t target_mem_size_{0};
};

template <typename S, typename T>
void Compute(ScatterNdCpuKernelMod *content, const ComputeParams<S, T> *params, const size_t start, const size_t end) {
  T *target = params->target_;
  S *indices = params->indices_;
  T *updates = params->updates_;
  std::vector<int> *out_strides = params->out_strides_;

  for (size_t i = start; i < end; ++i) {
    int offset = 0;
    for (size_t j = 0; j < IntToSize(params->indices_unit_rank_); ++j) {
      int index = static_cast<int>(indices[i * IntToSize(params->indices_unit_rank_) + j]);
      if (index < 0) {
        MS_LOG(EXCEPTION) << "For '" << kKernelName
                          << "', each element in 'indices' must be greater "
                             "than or equal to 0, but got "
                          << index;
      }
      if (index > SizeToInt(content->shape[j])) {
        MS_LOG(EXCEPTION) << "For '" << kKernelName
                          << "', each element in 'indices' should be smaller than the value of shape, but got " << index
                          << " and got the value of shape " << content->shape[j];
      }
      offset += index * out_strides->at(j) * params->unit_size_;
    }
    auto task = [&target, &updates, &params, offset, i](size_t update_start, size_t update_end) {
      for (size_t idx = update_start; idx < update_end; idx++) {
        target[IntToSize(offset) + idx] += updates[IntToSize(params->unit_size_) * i + idx];
      }
    };
    ParallelLaunchAutoSearch(task, IntToSize(params->unit_size_), content, &content->parallel_search_info_);
  }
}
}  // namespace

void ScatterNdCpuKernelMod::InitKernel(const CNodePtr &kernel_node) {
  MS_EXCEPTION_IF_NULL(kernel_node);
  kernel_name_ = common::AnfAlgo::GetCNodeName(kernel_node);
  Check(kernel_node);
  shape = common::AnfAlgo::GetOutputInferShape(kernel_node, 0);
  auto indices_shape = common::AnfAlgo::GetPrevNodeOutputInferShape(kernel_node, 0);
  auto updates_shape = common::AnfAlgo::GetPrevNodeOutputInferShape(kernel_node, 1);
  if (AnfAlgo::IsShapesDynamic({shape, indices_shape, updates_shape})) {
    return;
  }
  auto indices_unit_rank = indices_shape.back();
  if (indices_unit_rank > static_cast<int64_t>(shape.size())) {
    MS_LOG(EXCEPTION) << "For '" << kernel_name_
                      << "', the value of last dimension of 'indices' must be less than "
                         "or equal to the dimension of 'shape', but got  the value of last "
                         "dimension of 'indices': "
                      << indices_unit_rank << " and the dimension of 'shape': " << shape.size();
  }
  if (indices_shape.size() < kMinIndiceRank) {
    MS_LOG(EXCEPTION) << "For '" << kernel_name_ << "', the dimension of 'indices' must be at least 2, but got "
                      << indices_shape.size();
  }
  if (updates_shape.size() != indices_shape.size() - 1 + shape.size() - indices_unit_rank) {
    MS_LOG(EXCEPTION) << "For '" << kernel_name_
                      << "', the dimension of 'update' must be equal to the "
                         "dimension of 'indices' minus 1 plus the "
                         "dimension of 'shape' minus the value of last "
                         "dimension of 'indices', but got "
                      << "the dimension of 'update': " << updates_shape.size() << ", the dimension of 'indices' "
                      << indices_shape.size() << ", the dimension of 'shape' " << shape.size()
                      << ", the value of last dimension of 'indices' " << indices_unit_rank;
  }
  for (size_t i = 0; i < indices_shape.size() - 1; ++i) {
    if (updates_shape[i] != indices_shape[i]) {
      MS_LOG(EXCEPTION) << "For '" << kernel_name_
                        << "', the shape of 'updates' and 'indices' are "
                           "different in dimension i="
                        << i << ". The 'updates_shape[i]' is " << updates_shape[i] << " and the 'indices_shape[i]' is "
                        << indices_shape[i];
    }
  }
  indices_unit_rank_ = SizeToInt(indices_unit_rank);
  for (size_t i = indices_shape.size() - 1; i < updates_shape.size(); ++i) {
    unit_size_ *= SizeToInt(updates_shape[i]);
  }

  num_units_ *= updates_shape[indices_shape.size() - 2];
  for (int i = SizeToInt(indices_shape.size()) - 3; i >= 0; i--) {
    num_units_ *= updates_shape[IntToSize(i)];
  }
  int out_stride = 1;
  out_strides_.push_back(out_stride);
  for (int i = indices_unit_rank_ - 2; i >= 0; i--) {
    out_stride *= SizeToInt(shape[IntToSize(i + 1)]);
    out_strides_.push_back(out_stride);
  }
  reverse(out_strides_.begin(), out_strides_.end());

  auto kernel_attr = GetKernelAttrFromNode(kernel_node);
  std::vector<KernelAttr> support_list;
  (void)std::transform(func_list_.begin(), func_list_.end(), std::back_inserter(support_list),
                       [](const std::pair<KernelAttr, ScatterNdFunc> &pair) { return pair.first; });
  auto [is_match, index] = MatchKernelAttr(kernel_attr, support_list);
  if (!is_match) {
    MS_LOG(EXCEPTION) << "ScatterNd does not support this kernel data type: " << kernel_attr;
  }
  kernel_func_ = func_list_[index].second;
}

template <typename S, typename T>
bool ScatterNdCpuKernelMod::LaunchKernel(const std::vector<kernel::AddressPtr> &inputs,
                                         const std::vector<kernel::AddressPtr> &outputs) {
  auto target = reinterpret_cast<T *>(outputs[0]->addr);
  auto target_init = memset_s(target, outputs[0]->size, 0, outputs[0]->size);
  if (target_init != EOK) {
    MS_LOG(EXCEPTION) << "For '" << kernel_name_ << "', memset failed. Error no: " << target_init;
  }
  ComputeParams<S, T> params;
  params.target_ = target;
  params.indices_ = reinterpret_cast<S *>(inputs[0]->addr);
  params.updates_ = reinterpret_cast<T *>(inputs[1]->addr);
  params.target_mem_size_ = outputs[0]->size;
  params.unit_size_ = unit_size_;
  params.indices_unit_rank_ = indices_unit_rank_;
  params.out_strides_ = &out_strides_;
  for (size_t idx = 0; idx < num_units_; idx++) {
    Compute<S, T>(this, &params, idx, idx + 1);
  }
  return true;
}

void ScatterNdCpuKernelMod::Check(const CNodePtr &kernel_node) const {
  size_t input_num = common::AnfAlgo::GetInputTensorNum(kernel_node);
  constexpr size_t kDynamicInputNum = 3;
  if (input_num != kScatterNdInputSize && input_num != kDynamicInputNum) {
    MS_LOG(EXCEPTION) << "For '" << kernel_name_ << "', the number of inputs must be 2, but got " << input_num
                      << " input(s).";
  }
  size_t output_num = common::AnfAlgo::GetOutputTensorNum(kernel_node);
  if (output_num != kScatterNdOutputSize) {
    MS_LOG(EXCEPTION) << "For '" << kernel_name_ << "', the number of outputs must be 1, but got " << output_num
                      << " output(s).";
  }
}

#define DTYPE_REGISTER_(DT1, UPDATES, SHAPE, OUTPUT, DT2, T)                                      \
  KernelAttr().AddInputAttr(DT1).AddInputAttr(UPDATES).AddInputAttr(SHAPE).AddOutputAttr(OUTPUT), \
    &ScatterNdCpuKernelMod::LaunchKernel<DT2, T>

#define DTYPE_REGISTER(UPDATES, SHAPE, OUTPUT, T)                              \
  {DTYPE_REGISTER_(kNumberTypeInt16, UPDATES, SHAPE, OUTPUT, int16_t, T)},     \
    {DTYPE_REGISTER_(kNumberTypeInt32, UPDATES, SHAPE, OUTPUT, int32_t, T)}, { \
    DTYPE_REGISTER_(kNumberTypeInt64, UPDATES, SHAPE, OUTPUT, int64_t, T)      \
  }

#define DTYPE_REGISTER_ATTR_(DT1, UPDATES, OUTPUT, DT2, T)                    \
  KernelAttr().AddInputAttr(DT1).AddInputAttr(UPDATES).AddOutputAttr(OUTPUT), \
    &ScatterNdCpuKernelMod::LaunchKernel<DT2, T>

#define DTYPE_REGISTER_ATTR(UPDATES, OUTPUT, T)                              \
  {DTYPE_REGISTER_ATTR_(kNumberTypeInt16, UPDATES, OUTPUT, int16_t, T)},     \
    {DTYPE_REGISTER_ATTR_(kNumberTypeInt32, UPDATES, OUTPUT, int32_t, T)}, { \
    DTYPE_REGISTER_ATTR_(kNumberTypeInt64, UPDATES, OUTPUT, int64_t, T)      \
  }

std::vector<std::pair<KernelAttr, ScatterNdCpuKernelMod::ScatterNdFunc>> ScatterNdCpuKernelMod::func_list_ = {
  DTYPE_REGISTER_ATTR(kNumberTypeFloat64, kNumberTypeFloat64, double),
  DTYPE_REGISTER_ATTR(kNumberTypeFloat32, kNumberTypeFloat32, float),
  DTYPE_REGISTER_ATTR(kNumberTypeInt64, kNumberTypeInt64, int64_t),
  DTYPE_REGISTER_ATTR(kNumberTypeInt32, kNumberTypeInt32, int32_t),
  DTYPE_REGISTER_ATTR(kNumberTypeInt16, kNumberTypeInt16, int16_t),
  DTYPE_REGISTER_ATTR(kNumberTypeInt8, kNumberTypeInt8, int8_t),
  DTYPE_REGISTER_ATTR(kNumberTypeUInt64, kNumberTypeUInt64, uint64_t),
  DTYPE_REGISTER_ATTR(kNumberTypeUInt32, kNumberTypeUInt32, uint32_t),
  DTYPE_REGISTER_ATTR(kNumberTypeUInt16, kNumberTypeUInt16, uint16_t),
  DTYPE_REGISTER_ATTR(kNumberTypeUInt8, kNumberTypeUInt8, uint8_t),
  DTYPE_REGISTER(kNumberTypeFloat64, kNumberTypeInt64, kNumberTypeFloat64, double),
  DTYPE_REGISTER(kNumberTypeFloat32, kNumberTypeInt64, kNumberTypeFloat32, float),
  DTYPE_REGISTER(kNumberTypeInt64, kNumberTypeInt64, kNumberTypeInt64, int64_t),
  DTYPE_REGISTER(kNumberTypeInt32, kNumberTypeInt64, kNumberTypeInt32, int32_t),
  DTYPE_REGISTER(kNumberTypeInt16, kNumberTypeInt64, kNumberTypeInt16, int16_t),
  DTYPE_REGISTER(kNumberTypeInt8, kNumberTypeInt64, kNumberTypeInt8, int8_t),
  DTYPE_REGISTER(kNumberTypeUInt64, kNumberTypeInt64, kNumberTypeUInt64, uint64_t),
  DTYPE_REGISTER(kNumberTypeUInt32, kNumberTypeInt64, kNumberTypeUInt32, uint32_t),
  DTYPE_REGISTER(kNumberTypeUInt16, kNumberTypeInt64, kNumberTypeUInt16, uint16_t),
  DTYPE_REGISTER(kNumberTypeUInt8, kNumberTypeInt64, kNumberTypeUInt8, uint8_t),
};

std::vector<KernelAttr> ScatterNdCpuKernelMod::GetOpSupport() {
  std::vector<KernelAttr> support_list;
  (void)std::transform(func_list_.begin(), func_list_.end(), std::back_inserter(support_list),
                       [](const std::pair<KernelAttr, ScatterNdFunc> &pair) { return pair.first; });
  return support_list;
}

MS_KERNEL_FACTORY_REG(NativeCpuKernelMod, ScatterNd, ScatterNdCpuKernelMod);
}  // namespace kernel
}  // namespace mindspore
