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

#include "kernel/kernel.h"

#include <functional>
#include <algorithm>
#include <stack>
#include "utils/ms_context.h"
#include "utils/anf_utils.h"
#include "runtime/device/ms_device_shape_transfer.h"
#include "backend/common/session/anf_runtime_algorithm.h"
#include "include/common/utils/anfalgo.h"
#include "backend/common/optimizer/helper.h"

namespace mindspore {
namespace kernel {
constexpr int64_t kInvalidShape = -2;

string KernelTensor::GetAbstractName() const {
  if (tensor_info_.abstract_base == nullptr) {
    return "null(no abstract base)";
  }
  return tensor_info_.abstract_base->ToString();
}

bool KernelTensor::IsDynamicShape() const {
  auto shape = this->GetShapeVector();
  return std::any_of(shape.cbegin(), shape.cend(), [](auto i) { return i < 0; });
}

size_t KernelTensor::GetSizeInBytes() const {
  auto unit_size = GetTypeByte(TypeIdToType(GetDtype()));
  auto shapes = this->GetShapeVector();
  if (shapes.size() == 0) {
    return unit_size;
  }

  auto cur_size = unit_size;
  for (const auto val : shapes) {
    if (val < 0) {
      MS_LOG_EXCEPTION << "Invalid shape value " << val << " for calculating size. Abstract name: " << GetAbstractName()
                       << ". Please contact MindSpore support.";
    }
    if (val == 0) {
      MS_LOG_WARNING << "One dim of the shape is 0. Abstract name: " << GetAbstractName() << ".";
    }
    cur_size *= val;
  }

  return cur_size;
}

TypeId KernelTensor::GetDtype() const {
  if (tensor_info_.abstract_base == nullptr) {
    return TypeId::kTypeUnknown;
  }

  auto type_ptr = tensor_info_.abstract_base->BuildType();
  if (type_ptr == nullptr || !type_ptr->isa<TensorType>()) {
    return TypeId::kTypeUnknown;
  }

  auto tensor_ptr = type_ptr->cast<TensorTypePtr>();
  auto elem = tensor_ptr->element();
  if (elem == nullptr) {
    return TypeId::kTypeUnknown;
  }
  return elem->type_id();
}

ShapeVector KernelTensor::GetShapeVector() const {
  auto base_shape_ptr = GetBaseShape();
  if (base_shape_ptr == nullptr || !base_shape_ptr->isa<abstract::Shape>()) {
    return {};
  }
  auto shape = base_shape_ptr->cast<abstract::ShapePtr>()->shape();
  return shape;
}

std::vector<TypeId> KernelTensor::GetListOrTupleDtype() const {
  if (tensor_info_.abstract_base == nullptr) {
    return {TypeId::kTypeUnknown};
  }

  auto type_ptr = tensor_info_.abstract_base->BuildType();
  if (type_ptr == nullptr || !type_ptr->isa<List>() || !type_ptr->isa<Tuple>()) {
    return {TypeId::kTypeUnknown};
  }

  std::vector<TypeId> types;
  if (type_ptr->isa<List>()) {
    auto tuple_ptr = type_ptr->cast<TuplePtr>();
    auto elements = tuple_ptr->elements();
    (void)std::transform(elements.begin(), elements.end(), std::back_inserter(types),
                         [](const TypePtr &t) { return t->type_id(); });
  } else if (type_ptr->isa<Tuple>()) {
    auto tuple_ptr = type_ptr->cast<TuplePtr>();
    auto elements = tuple_ptr->elements();
    (void)std::transform(elements.begin(), elements.end(), std::back_inserter(types),
                         [](const TypePtr &t) { return t->type_id(); });
  } else {
    types.push_back(TypeId::kTypeUnknown);
  }

  return types;
}

ShapeArray KernelTensor::GetListOrTupleShapeVector() const {
  auto base_shape_ptr = GetBaseShape();
  // ListShape or TupleShape is inherited from SequenceShape.
  if (base_shape_ptr == nullptr || !base_shape_ptr->isa<abstract::SequenceShape>()) {
    return {};
  }
  auto sequence_shape_ptr = base_shape_ptr->cast<abstract::SequenceShapePtr>();
  auto base_shape_list = sequence_shape_ptr->shape();
  std::vector<std::vector<int64_t>> shape_vector_list;
  for (auto base_shape : base_shape_list) {
    if (base_shape == nullptr || !base_shape->isa<abstract::Shape>()) {
      return {};
    }
    auto tmp_shape = base_shape->cast<abstract::ShapePtr>()->shape();
    shape_vector_list.push_back(tmp_shape);
  }

  return shape_vector_list;
}

void KernelTensor::SetDtype(const TypePtr &dtype) {
  if (tensor_info_.abstract_base == nullptr) {
    return;
  }
  tensor_info_.abstract_base->set_type(dtype);
}

void KernelTensor::SetShapeVector(const std::vector<int64_t> &shape) {
  if (tensor_info_.abstract_base == nullptr) {
    return;
  }
  tensor_info_.abstract_base->set_shape(std::make_shared<abstract::Shape>(shape));
}

abstract::BaseShapePtr KernelTensor::GetBaseShape() const {
  if (tensor_info_.abstract_base == nullptr) {
    return nullptr;
  }
  return tensor_info_.abstract_base->BuildShape();
}

void KernelTensor::SetBaseShape(const abstract::BaseShapePtr &base_shape) {
  if (tensor_info_.abstract_base == nullptr) {
    return;
  }
  tensor_info_.abstract_base->set_shape(base_shape);
}

const std::vector<int64_t> &KernelTensor::GetDeviceShapeAdaptively() const {
  return tensor_info_.device_shape_adaptively;
}

void KernelTensor::SetDeviceShapeAdaptively(const std::vector<int64_t> &device_shape_adaptively) {
  tensor_info_.device_shape_adaptively = device_shape_adaptively;
}

int KernelMod::Resize(const BaseOperatorPtr & /* base_operator */, const std::vector<KernelTensorPtr> &inputs,
                      const std::vector<KernelTensorPtr> &outputs,
                      const std::map<uint32_t, tensor::TensorPtr> & /* inputsOnHost */) {
  auto ret = KRET_OK;
  workspace_size_list_.clear();
  input_size_list_.clear();
  for (auto &input : inputs) {
    size_t tensor_size = 0;
    size_t type_size = GetTypeByte(TypeIdToType(input->GetDtype()));
    auto shape = input->GetShapeVector();
    // If any input shape contains -1, means input shape is dynamic.
    if (!IsValidShape(shape)) {
      // Note: When any input shape contains -1, it cannot be returned directly here.
      // This is because in the phase of dynamic memory reuse, it depends on the length of output_size_list_.
      ret = KRET_UNKNOWN_SHAPE;
      // As a placeholder, put type_size to input_size_list_.
      tensor_size = type_size;
    } else {
      tensor_size =
        shape.empty() ? type_size : std::accumulate(shape.begin(), shape.end(), type_size, std::multiplies<size_t>());
      tensor_size = std::max(tensor_size, type_size);
    }
    (void)input_size_list_.emplace_back(tensor_size);
  }

  output_size_list_.clear();
  for (auto &output : outputs) {
    size_t tensor_size = 0;
    size_t type_size = GetTypeByte(TypeIdToType(output->GetDtype()));
    auto shape = output->GetShapeVector();
    // If any output shape contains -1, means output shape is dynamic.
    if (!IsValidShape(shape)) {
      // Note:
      // 1. When any output shape contains -1, it cannot be returned directly here.
      //    This is because in the phase of dynamic memory reuse, it depends on the length of output_size_list_.
      // 2. If ret != KRET_OK, this means that ret is KRET_UNKNOWN_SHAPE, at this time, the KRET_UNKNOWN_SHAPE should be
      //    returned preferentially at the end of the function.
      ret = (ret == KRET_OK ? KRET_UNKNOWN_OUT_SHAPE : ret);
      // As a placeholder, put type_size to output_size_list_.
      tensor_size = type_size;
    } else {
      tensor_size =
        shape.empty() ? type_size : std::accumulate(shape.begin(), shape.end(), type_size, std::multiplies<size_t>());
      tensor_size = std::max(tensor_size, type_size);
    }
    (void)output_size_list_.emplace_back(tensor_size);
  }
  return static_cast<int>(ret);
}
}  // namespace kernel
}  // namespace mindspore
