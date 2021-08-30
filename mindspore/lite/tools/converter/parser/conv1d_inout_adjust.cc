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
#include "tools/converter/parser/conv1d_inout_adjust.h"
#include <string>
#include <vector>
#include <algorithm>
#include <memory>
#include "mindspore/lite/include/errorcode.h"
#include "ops/conv2d.h"
#include "ops/squeeze.h"
#include "ops/unsqueeze.h"
#include "ops/primitive_c.h"
#include "tools/optimizer/common/gllo_utils.h"
#include "nnacl/op_base.h"

namespace mindspore::lite {
namespace {
constexpr size_t kTripleNum = 3;
constexpr size_t kConvWeightIndex = 2;
constexpr int64_t kNumDim2 = 2;
}  // namespace
CNodePtr Conv1DInOutAdjust::NewUnsqueezeOpNode(const FuncGraphPtr &func_graph, const AnfNodePtr input_node,
                                               const std::vector<int64_t> &axis) {
  auto unsqueeze_prim = std::make_shared<ops::Unsqueeze>();
  MS_CHECK_TRUE_MSG(unsqueeze_prim != nullptr, nullptr, "create unsqueeze failed.");
  unsqueeze_prim->set_attr("axis", MakeValue(axis));
  ValueNodePtr value_node = NewValueNode(unsqueeze_prim);
  MS_CHECK_TRUE_MSG(value_node != nullptr, nullptr, "new valueNode failed.");
  std::vector<AnfNodePtr> op_inputs = {value_node, input_node};
  auto unsqueeze = func_graph->NewCNode(op_inputs);
  MS_CHECK_TRUE_MSG(unsqueeze != nullptr, nullptr, "new CNode failed.");
  unsqueeze->set_fullname_with_scope(input_node->fullname_with_scope() + "_unsqueeze");
  return unsqueeze;
}

CNodePtr Conv1DInOutAdjust::NewSqueezeOpNode(const FuncGraphPtr &func_graph, const AnfNodePtr input_node,
                                             const std::vector<int64_t> &axis) {
  auto squeeze_prim = std::make_shared<ops::Squeeze>();
  MS_CHECK_TRUE_MSG(squeeze_prim != nullptr, nullptr, "create squeeze failed.");
  squeeze_prim->set_attr("axis", MakeValue(axis));
  ValueNodePtr value_node = NewValueNode(squeeze_prim);
  MS_CHECK_TRUE_MSG(value_node != nullptr, nullptr, "new valueNode failed.");
  std::vector<AnfNodePtr> op_inputs = {value_node, input_node};
  auto squeeze = func_graph->NewCNode(op_inputs);
  MS_CHECK_TRUE_MSG(squeeze != nullptr, nullptr, "new CNode failed.");
  squeeze->set_fullname_with_scope(input_node->fullname_with_scope() + "_squeeze");
  return squeeze;
}

lite::STATUS Conv1DInOutAdjust::ExpandFilterShape(const AnfNodePtr &weight_node, const schema::Format &format) {
  MS_ASSERT(weight_node != nullptr);
  auto weight_tensor = opt::GetTensorInfo(weight_node);
  MS_CHECK_TRUE_MSG(weight_tensor != nullptr, lite::RET_ERROR, "weight node must be param value.");
  auto shape = weight_tensor->shape();
  MS_CHECK_TRUE_RET(shape.size() == kTripleNum, lite::RET_OK);
  std::vector<int64_t> new_shape(shape);
  switch (format) {
    case schema::Format_NCHW:
    case schema::Format_KCHW:
      new_shape.insert(new_shape.begin() + kNumDim2, 1);
      break;
    case schema::Format_NHWC:
    case schema::Format_KHWC:
      new_shape.insert(new_shape.begin() + 1, 1);
      break;
    default:
      MS_LOG(ERROR) << "Unsupported format.";
      return RET_ERROR;
  }
  weight_tensor->set_shape(new_shape);
  if (!utils::isa<ParameterPtr>(weight_node)) {
    return lite::RET_OK;
  }
  auto weight_param = weight_node->cast<ParameterPtr>();
  MS_CHECK_TRUE_MSG(weight_param != nullptr, RET_ERROR, "weight_param is nullptr.");
  auto type = weight_tensor->data_type();
  weight_param->set_abstract(std::make_shared<abstract::AbstractTensor>(TypeIdToType(type), new_shape));
  return RET_OK;
}

bool Conv1DInOutAdjust::Run(const FuncGraphPtr &func_graph) {
  MS_ASSERT(func_graph != nullptr);
  auto manager = func_graph->manager();
  MS_ASSERT(manager != nullptr);
  auto nodes = func_graph->nodes();
  for (auto &node : nodes) {
    if (!utils::isa<CNodePtr>(node)) {
      continue;
    }
    auto cnode = node->cast<CNodePtr>();
    if (!opt::CheckPrimitiveType(cnode, prim::kPrimConv2D) &&
        !opt::CheckPrimitiveType(cnode, prim::kPrimConv2DFusion)) {
      continue;
    }
    auto conv2d_node = GetValueNode<std::shared_ptr<mindspore::ops::Conv2D>>(cnode->input(0));
    MS_CHECK_TRUE_MSG(conv2d_node != nullptr, false, "conv2d is nullptr.");
    MS_CHECK_TRUE_MSG(conv2d_node->GetAttr(ops::kOriginalFormat) != nullptr, false, "The format of conv2d is nullptr.");
    std::vector<int64_t> axis;
    switch (Format(GetValue<int64_t>(conv2d_node->GetAttr(ops::kOriginalFormat)))) {
      case mindspore::Format::NWC:
        conv2d_node->AddAttr(mindspore::ops::kOriginalFormat, MakeValue<int64_t>(mindspore::NHWC));
        axis = {1};
        break;
      case mindspore::Format::NCW:
        conv2d_node->AddAttr(mindspore::ops::kOriginalFormat, MakeValue<int64_t>(mindspore::NCHW));
        axis = {2};
        break;
      default:
        continue;
    }

    auto input_node = cnode->input(1);
    auto unsqueeze = NewUnsqueezeOpNode(func_graph, input_node, axis);
    MS_CHECK_TRUE_MSG(unsqueeze != nullptr, false, "New unsqueeze node failed.");
    manager->Replace(input_node, unsqueeze);
    auto squeeze = NewSqueezeOpNode(func_graph, cnode, axis);
    MS_CHECK_TRUE_MSG(squeeze != nullptr, false, "New squeeze node failed.");
    manager->Replace(cnode, squeeze);
    auto conv_cnode = cnode->cast<CNodePtr>();
    MS_ASSERT(conv_cnode->inputs().size() > kConvWeightIndex);
    auto weight_node = conv_cnode->input(kConvWeightIndex);
    MS_ASSERT(weight_node != nullptr);
    auto prim = GetValueNode<PrimitivePtr>(conv_cnode->input(0));
    MS_ASSERT(prim != nullptr);
    schema::Format schema_format = schema::Format::Format_KCHW;
    // expand weight tensor to 4 dimensions.
    auto weight_tensor = opt::GetTensorInfo(weight_node);
    if (weight_tensor == nullptr) {
      MS_LOG(DEBUG) << "weight node is not param value, insert not for weight.";
      auto unsqueeze_weight = NewUnsqueezeOpNode(func_graph, weight_node, axis);
      MS_CHECK_TRUE_MSG(unsqueeze_weight != nullptr, false, "New unsqueeze node failed.");
      manager->Replace(input_node, unsqueeze_weight);
      return RET_OK;
    } else {
      auto status = ExpandFilterShape(weight_node, schema_format);
      MS_CHECK_TRUE_MSG(status == RET_OK, false, "Expand filter shape failed.");
    }
  }
  return true;
}
}  // namespace mindspore::lite
