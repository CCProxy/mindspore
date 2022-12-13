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

#include "ops/sequence_slice.h"

#include <vector>

#include "ops/op_utils.h"
#include "utils/check_convert_utils.h"
#include "include/common/utils/utils.h"
#include "mindapi/src/helper.h"

namespace mindspore {
namespace ops {
AbstractBasePtr AbstractInferInner(const PrimitivePtr &primitive, const std::vector<AbstractBasePtr> &input_args) {
  MS_EXCEPTION_IF_NULL(primitive);
  auto prim_name = primitive->name();
  constexpr size_t input_num = 4;
  constexpr size_t seq_index = 0;
  constexpr size_t start_index = 1;
  constexpr size_t end_index = 2;
  constexpr size_t step_index = 3;
  CheckAndConvertUtils::CheckInputArgs(input_args, kEqual, input_num, prim_name);
  auto first_abs = input_args[seq_index];
  MS_EXCEPTION_IF_NULL(first_abs);
  if (!first_abs->isa<abstract::AbstractSequence>()) {
    MS_EXCEPTION(TypeError) << "For '" << prim_name
                            << "', the first input should be tuple or list but got: " << first_abs->ToString();
  }
  auto seq_abs = first_abs->cast<abstract::AbstractSequencePtr>();
  MS_EXCEPTION_IF_NULL(seq_abs);
  if (seq_abs->dynamic_len()) {
    // If the length of input sequence is dynamic length, the length of sliced sequence should also be dynamic length.
    return seq_abs->Clone();
  }
  auto start_abs = input_args[start_index];
  MS_EXCEPTION_IF_NULL(start_abs);
  auto end_abs = input_args[end_index];
  MS_EXCEPTION_IF_NULL(end_abs);
  auto step_abs = input_args[step_index];
  MS_EXCEPTION_IF_NULL(step_abs);
  if (start_abs->BuildValue() != kAnyValue && end_abs->BuildValue() != kAnyValue &&
      step_abs->BuildValue() != kAnyValue) {
    MS_EXCEPTION(ValueError) << "For '" << prim_name << "', the input sequence should be dynamic length sequence or "
                             << "at least one of the start/end/step should be variable, but got all constant.";
  }
  auto ret = seq_abs->Clone()->cast<abstract::AbstractSequencePtr>();
  ret->CheckAndConvertToDynamicLenSequence();
  return ret;
}

MIND_API_OPERATOR_IMPL(SequenceSlice, BaseOperator);
class SequenceSliceInfer : public abstract::OpInferBase {
 public:
  BaseShapePtr InferShape(const PrimitivePtr &primitive,
                          const std::vector<AbstractBasePtr> &input_args) const override {
    return AbstractInferInner(primitive, input_args)->BuildShape();
  }

  TypePtr InferType(const PrimitivePtr &prim, const std::vector<AbstractBasePtr> &input_args) const override {
    return AbstractInferInner(prim, input_args)->BuildType();
  }

  AbstractBasePtr InferShapeAndType(const abstract::AnalysisEnginePtr &, const PrimitivePtr &primitive,
                                    const std::vector<AbstractBasePtr> &input_args) const override {
    return AbstractInferInner(primitive, input_args);
  }
};
REGISTER_PRIMITIVE_OP_INFER_IMPL(SequenceSlice, prim::kPrimSequenceSlice, SequenceSliceInfer, false);
}  // namespace ops
}  // namespace mindspore
