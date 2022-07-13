/**
 * Copyright 2020-2021 Huawei Technologies Co., Ltd
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
#include "ops/unsorted_segment_sum.h"
#include <string>
#include <algorithm>
#include <memory>
#include <set>
#include <map>
#include <vector>
#include "ops/op_utils.h"
#include "utils/check_convert_utils.h"
#include "abstract/ops/primitive_infer_map.h"
#include "mindapi/src/helper.h"

namespace mindspore {
namespace ops {
namespace {
abstract::ShapePtr UnsortedSegmentSumInferShape(const PrimitivePtr &primitive,
                                                const std::vector<AbstractBasePtr> &input_args) {
  MS_EXCEPTION_IF_NULL(primitive);
  const std::string &op_name = primitive->name();
  auto x_shape = CheckAndConvertUtils::ConvertShapePtrToShapeMap(input_args[0]->BuildShape())[kShape];
  auto x_min_shape = CheckAndConvertUtils::ConvertShapePtrToShapeMap(input_args[0]->BuildShape())[kMinShape];
  auto x_max_shape = CheckAndConvertUtils::ConvertShapePtrToShapeMap(input_args[0]->BuildShape())[kMaxShape];
  CheckAndConvertUtils::CheckMinMaxShape(x_shape, &x_min_shape, &x_max_shape);
  auto x_shape_rank = SizeToLong(x_shape.size());
  (void)CheckAndConvertUtils::CheckInteger("input_x size", x_shape_rank, kGreaterThan, 0, op_name);
  auto segment_ids_shape = CheckAndConvertUtils::ConvertShapePtrToShapeMap(input_args[1]->BuildShape())[kShape];
  auto segment_ids_shape_rank = SizeToLong(segment_ids_shape.size());
  (void)CheckAndConvertUtils::CheckInteger("segment_ids size", segment_ids_shape_rank, kGreaterThan, 0, op_name);
  ShapeVector output_shape;
  ShapeVector out_min_shape;
  ShapeVector out_max_shape;
  constexpr int dynamic_rank_len = 1;
  constexpr int dynamic_rank_value = -2;
  if ((x_shape_rank == dynamic_rank_len && x_shape[0] == dynamic_rank_value) ||
      (segment_ids_shape_rank == dynamic_rank_len && segment_ids_shape[0] == dynamic_rank_value)) {
    output_shape = {dynamic_rank_value};  // unknown dimension
    out_min_shape = {0};
    out_max_shape = {abstract::Shape::SHP_ANY};
    return std::make_shared<abstract::Shape>(output_shape, out_min_shape, out_max_shape);
  }
  (void)CheckAndConvertUtils::CheckValue<size_t>("x rank", x_shape.size(), kGreaterEqual, "segment_ids_shape rank",
                                                 segment_ids_shape.size(), op_name);
  for (uint64_t i = 0; i < segment_ids_shape.size(); i++) {
    if (segment_ids_shape[i] == abstract::Shape::SHP_ANY || x_shape[i] == abstract::Shape::SHP_ANY) continue;
    if (segment_ids_shape[i] != x_shape[i]) {
      MS_EXCEPTION(ValueError) << "For '" << op_name
                               << "', the whose shape of 'segment_ids' must be a prefix of the shape of 'input_x', "
                                  "but got 'segment_ids_shape["
                               << i << "]': " << segment_ids_shape[i] << " and 'input_x_shape[" << i
                               << "]': " << x_shape[i];
    }
  }
  abstract::CheckShapeAnyAndPositive(op_name + " x_shape", x_shape);
  abstract::CheckShapeAnyAndPositive(op_name + " segment_ids_shape", segment_ids_shape);

  ShapeVector num_vec;
  ShapeVector num_min_vec;
  ShapeVector num_max_vec;
  int64_t num_segments_v = 0;
  // num_segments is a Tensor when UnsortedSegmentSum is a dynamic shape operator
  if (input_args[kInputIndex2]->isa<abstract::AbstractTensor>()) {
    auto n_value_ptr = input_args[kInputIndex2]->BuildValue();
    MS_EXCEPTION_IF_NULL(n_value_ptr);
    if (n_value_ptr->isa<tensor::Tensor>()) {
      auto n_tensor_ptr = n_value_ptr->cast<tensor::TensorPtr>();
      MS_EXCEPTION_IF_NULL(n_tensor_ptr);
      num_segments_v = *static_cast<int64_t *>(n_tensor_ptr->data_c());
      (void)CheckAndConvertUtils::CheckInteger("num_segments's value", num_segments_v, kGreaterThan, 0, op_name);
      num_vec.push_back(num_segments_v);
      num_min_vec.push_back(num_segments_v);
      num_max_vec.push_back(num_segments_v);
    } else {
      auto n_abstract_tensor = input_args[kInputIndex2]->cast<abstract::AbstractTensorPtr>();
      MS_EXCEPTION_IF_NULL(n_abstract_tensor);
      num_vec.push_back(-1);
      auto num_min_value = n_abstract_tensor->get_min_value();
      auto num_max_value = n_abstract_tensor->get_max_value();
      if (num_min_value != nullptr && num_max_value != nullptr) {
        num_min_vec = GetValue<ShapeVector>(num_min_value);
        num_max_vec = GetValue<ShapeVector>(num_max_value);
      } else {
        num_min_vec.push_back(-1);
        num_max_vec.push_back(-1);
      }
    }
  } else if (input_args[kInputIndex2]->isa<abstract::AbstractScalar>()) {
    num_segments_v = GetValue<int64_t>(input_args[kInputIndex2]->BuildValue());
    (void)CheckAndConvertUtils::CheckInteger("num_segments's value", num_segments_v, kGreaterThan, 0, op_name);
    num_vec.push_back(num_segments_v);
    num_min_vec.push_back(num_segments_v);
    num_max_vec.push_back(num_segments_v);
  } else {
    MS_LOG(EXCEPTION) << "For '" << op_name
                      << "', the third input type should be tensor or scalar, but got invalid abstract type:"
                      << input_args[kInputIndex2]->type_name() << ".";
  }
  auto calc_shape = [segment_ids_shape_rank](const ShapeVector &num_vec, const ShapeVector &x_shape) -> ShapeVector {
    ShapeVector out_vec;
    (void)copy(num_vec.begin(), num_vec.end(), std::back_inserter(out_vec));
    (void)copy(x_shape.begin() + segment_ids_shape_rank, x_shape.end(), std::back_inserter(out_vec));
    return out_vec;
  };
  output_shape = calc_shape(num_vec, x_shape);
  out_min_shape = calc_shape(num_min_vec, x_min_shape);
  out_max_shape = calc_shape(num_max_vec, x_max_shape);
  return std::make_shared<abstract::Shape>(output_shape, out_min_shape, out_max_shape);
}

TypePtr UnsortedSegmentSumInferType(const PrimitivePtr &primitive, const std::vector<AbstractBasePtr> &input_args) {
  MS_EXCEPTION_IF_NULL(primitive);
  auto prim_name = primitive->name();
  /* check segment_ids */
  auto ids_ptr = input_args[kInputIndex1]->BuildType();
  std::set<TypePtr> ids_type_set = {kInt32, kInt64};
  (void)CheckAndConvertUtils::CheckTensorTypeValid("segment_ids type", ids_ptr, ids_type_set, prim_name);
  /* check num_segments */
  auto num_ptr = input_args[kInputIndex2]->BuildType();
  std::map<std::string, TypePtr> args_num_segments;
  (void)args_num_segments.insert({"num_segments", num_ptr});
  const std::set<TypePtr> num_type_set = {kInt32, kInt64};
  (void)CheckAndConvertUtils::CheckScalarOrTensorTypesSame(args_num_segments, num_type_set, prim_name);
  /* check input_x */
  auto x_type_ptr = input_args[kInputIndex0]->BuildType();
  std::set<TypePtr> x_type_set = {kFloat16, kFloat32, kInt32};
  return CheckAndConvertUtils::CheckTensorTypeValid("input_x", x_type_ptr, x_type_set, prim_name);
}
}  // namespace
MIND_API_OPERATOR_IMPL(UnsortedSegmentSum, BaseOperator);

AbstractBasePtr UnsortedSegmentSumInfer(const abstract::AnalysisEnginePtr &, const PrimitivePtr &primitive,
                                        const std::vector<AbstractBasePtr> &input_args) {
  MS_EXCEPTION_IF_NULL(primitive);
  const int64_t kMinInputNum = 2;
  const int64_t kMaxInputNum = 3;
  (void)CheckAndConvertUtils::CheckInputArgs(input_args, kGreaterEqual, kMinInputNum, primitive->name());
  (void)CheckAndConvertUtils::CheckInputArgs(input_args, kLessEqual, kMaxInputNum, primitive->name());
  auto infer_type = UnsortedSegmentSumInferType(primitive, input_args);
  auto infer_shape = UnsortedSegmentSumInferShape(primitive, input_args);
  return abstract::MakeAbstract(infer_shape, infer_type);
}

REGISTER_HOST_DEPENDS(kNameUnsortedSegmentSum, {2});
REGISTER_PRIMITIVE_EVAL_IMPL(UnsortedSegmentSum, prim::kPrimUnsortedSegmentSum, UnsortedSegmentSumInfer, nullptr, true);
}  // namespace ops
}  // namespace mindspore
