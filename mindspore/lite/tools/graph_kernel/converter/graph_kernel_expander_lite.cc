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

#include "tools/graph_kernel/converter/graph_kernel_expander_lite.h"

#include <utility>
#include <algorithm>
#include <vector>
#include <map>
#include <string>
#include <unordered_set>

#include "common/graph_kernel/model/node.h"
#include "common/graph_kernel/model/op_node.h"
#include "common/graph_kernel/core/graph_kernel_callback.h"
#include "common/graph_kernel/core/graph_kernel_utils.h"
#include "common/graph_kernel/core/graph_builder.h"
#include "common/graph_kernel/graph_kernel_flags.h"
#include "utils/anf_utils.h"
#include "tools/graph_kernel/converter/basic_op_infer_shape.h"
#include "utils/ms_context.h"
#include "tools/graph_kernel/converter/preprocess_weight.h"

namespace mindspore::graphkernel {
AnfNodePtr TensorToValueDeco::Run(const AnfNodePtr &node) {
  auto cnode = QuickCloneCNode(node);
  for (const auto &idx : input_idx_) {
    if (cnode->input(idx + 1)->isa<ValueNode>()) {
      auto value = cnode->input(idx + 1)->cast<ValueNodePtr>()->value();
      if (value->isa<tensor::Tensor>()) {
        auto param_value = value->cast<tensor::TensorPtr>();
        auto int_value = static_cast<int *>(param_value->data_ptr()->data());
        ShapeVector out_list;
        std::transform(int_value, int_value + param_value->data_ptr()->size(), std::back_inserter(out_list), IntToLong);
        auto new_value = std::make_shared<ValueNode>(MakeValue(out_list));
        cnode->set_input(idx + 1, new_value);
      }
    }
  }
  return decorated_->Run(cnode);
}

AnfNodePtr FixFormatDeco::Run(const AnfNodePtr &node) {
  auto cnode = QuickCloneCNode(node);
  std::vector<std::string> format = {kOpFormat_DEFAULT};
  auto ori_format = node->cast<CNodePtr>()->GetAttr(kOutputsFormat);
  cnode->AddAttr(kOutputsFormat, MakeValue(format));
  auto ret = decorated_->Run(cnode);
  if (ret == nullptr) {
    return nullptr;
  }
  auto fg = GetCNodeFuncGraph(ret);
  for (auto sub_cnode : fg->GetOrderedCnodes()) {
    sub_cnode->AddAttr(kOutputsFormat, ori_format);
  }
  auto ret_cnode = ret->cast<CNodePtr>();
  ret_cnode->AddAttr(kOutputsFormat, ori_format);
  return ret_cnode;
}

AnfNodePtr InferValueDeco::Run(const AnfNodePtr &node) {
  std::unordered_set<std::string> akg_exclude_nodes = {prim::kPrimGather->name(), prim::kPrimShape->name(),
                                                       prim::kPrimConcat->name(), prim::kPrimConstantOfShape->name()};
  auto cnode = QuickCloneCNode(node);
  auto ret = decorated_->Run(cnode);
  if (ret == nullptr) {
    return nullptr;
  }
  auto fg = GetCNodeFuncGraph(ret);

  AnfNodePtrList inputs = ret->cast<CNodePtr>()->inputs();
  (void)RemoveNonScalarConstTensorFromParameter(fg, &inputs);

  inner::LiteGraphPtr litegraph = GkUtils::AnfGraph2LiteGraph(fg);
  auto ops_list = litegraph->GetOrderedNodes();
  auto iter = ops_list.begin();
  while (iter != ops_list.end()) {
    auto this_op = std::static_pointer_cast<inner::PrimOp>(*iter);
    auto value = this_op->InferValue(this_op->inputs(), this_op->attrs());
    if (value != nullptr) {
      (*iter)->ReplaceWith(value);
      ops_list = litegraph->GetOrderedNodes();
      iter = ops_list.begin();
    } else {
      ++iter;
    }
  }
  auto &outputs = litegraph->GetOutputs();

  if (outputs.size() != 1) {
    return nullptr;
  }

  if (outputs[0]->NodeType() == inner::NType::Value) {
    auto value = std::static_pointer_cast<inner::ConstTensorNode>(outputs[0])->data();
    auto valuenode = NewValueNode(value);
    valuenode->set_abstract(value->ToAbstract());
    return valuenode;
  } else {
    bool cannot_expand =
      std::any_of(ops_list.begin(), ops_list.end(), [&akg_exclude_nodes](const inner::NodePtr &node) {
        return akg_exclude_nodes.count(std::static_pointer_cast<inner::PrimOp>(node)->op()) > 0;
      });
    if (cannot_expand) {
      return nullptr;
    } else {
      auto new_fg = GkUtils::LiteGraph2AnfGraph(litegraph, Callback::Instance());
      (void)ConvertNonscalarTensorToParameter(new_fg, &inputs);
      AnfNodePtrList new_inputs = {NewValueNode(new_fg)};
      (void)new_inputs.insert(new_inputs.end(), inputs.cbegin() + 1, inputs.cend());
      return node->func_graph()->NewCNode(new_inputs);
    }
  }
}

AnfNodePtr PoolLayoutDeco::Run(const AnfNodePtr &node) {
  auto cnode = QuickCloneCNode(node);
  auto prev_node = AnfUtils::VisitKernel(node->cast<CNodePtr>()->input(1), 0).first;
  if (prev_node != nullptr) {
    auto sub_graph = GetCNodeFuncGraph(prev_node);
    if (sub_graph != nullptr) {
      auto sub_nodes = TopoSort(sub_graph->get_return());
      for (auto sub_node : sub_nodes) {
        if (IsPrimitiveCNode(sub_node, prim::kPrimConv2D)) {
          AnfUtils::SetNodeAttr("layout_axis", GetCNodePrimitive(sub_node)->GetAttr("weight_coi"), cnode);
          break;
        }
      }
    }
  }
  return decorated_->Run(cnode);
}

std::vector<PrimitivePtr> GraphKernelExpanderLite::InitOpList() {
  std::vector<OpWithLevel> expand_ops_with_level = {
    {kCPUDevice, OpLevel_0, prim::kPrimAddFusion},       {kCPUDevice, OpLevel_0, prim::kPrimMulFusion},
    {kCPUDevice, OpLevel_0, prim::kPrimSubFusion},       {kCPUDevice, OpLevel_0, prim::kPrimSquare},
    {kCPUDevice, OpLevel_1, prim::kPrimReduceFusion},    {kCPUDevice, OpLevel_0, prim::kPrimActivation},
    {kCPUDevice, OpLevel_0, prim::kPrimDivFusion},       {kCPUDevice, OpLevel_1, prim::kPrimExpandDims},
    {kCPUDevice, OpLevel_0, prim::kPrimExpFusion},       {kCPUDevice, OpLevel_1, prim::kPrimSqueeze},
    {kCPUDevice, OpLevel_1, prim::kPrimTranspose},       {kCPUDevice, OpLevel_1, prim::kPrimReshape},
    {kCPUDevice, OpLevel_1, prim::kPrimUnsqueeze},       {kCPUDevice, OpLevel_1, prim::kPrimGather},
    {kCPUDevice, OpLevel_1, prim::kPrimShape},           {kCPUDevice, OpLevel_1, prim::kPrimConcat},
    {kCPUDevice, OpLevel_1, prim::kPrimConstantOfShape}, {kCPUDevice, OpLevel_1, prim::kPrimConv2DFusion},
    {kCPUDevice, OpLevel_1, prim::kPrimAvgPoolFusion},   {kCPUDevice, OpLevel_1, prim::kPrimMaxPoolFusion},
    {kCPUDevice, OpLevel_1, prim::kPrimStridedSlice}};
  const auto &flags = GraphKernelFlags::GetInstance();
  return GkUtils::GetValidOps(expand_ops_with_level, flags.fusion_ops_level, flags.enable_expand_ops_only,
                              flags.enable_expand_ops, flags.disable_expand_ops);
}

bool GraphKernelExpanderLite::CanExpand(const CNodePtr &node) const {
  if (!GraphKernelExpander::CanExpand(node)) {
    return false;
  }
  // check if the node has dynamic shape
  auto cb = Callback::Instance();
  for (size_t i = 0; i < node->size() - 1; i++) {
    if (!node->input(i + 1)->isa<Parameter>() && !node->input(i + 1)->isa<ValueNode>() &&
        cb->GetInputShape(node, i).size() == 0) {
      MS_LOG(INFO) << "cnode with no input info can not expand now, node is " << node->fullname_with_scope();
      return false;
    }
  }
  return true;
}

ExpanderPtr GraphKernelExpanderLite::InitExpander(const AnfNodePtr &node) {
  auto expander = std::make_shared<DefaultExpander>(Callback::Instance());
  ExpanderCreatorFuncList decos = {InferValueDeco::Creator};
  std::map<std::string, ExpanderCreatorFuncList> creators = {
    {prim::kPrimReduceFusion->name(), {InputToAttrDeco::GetCreator({1}), FixFormatDeco::Creator}},
    {prim::kPrimExpandDims->name(), {InputToAttrDeco::GetCreator({1}), FixFormatDeco::Creator}},
    {prim::kPrimUnsqueeze->name(), {FixFormatDeco::Creator}},
    {prim::kPrimSqueeze->name(), {FixFormatDeco::Creator}},
    {prim::kPrimShape->name(), {FixFormatDeco::Creator}},
    {prim::kPrimReshape->name(), {InputToAttrDeco::GetCreator({1}), FixFormatDeco::Creator}},
    {prim::kPrimConstantOfShape->name(), {InputToAttrDeco::GetCreator({0}), FixFormatDeco::Creator}},
    {prim::kPrimTranspose->name(), {TensorToValueDeco::GetCreator({1}), InputToAttrDeco::GetCreator({1})}},
    {prim::kPrimGather->name(),
     {TensorToValueDeco::GetCreator({2}), InputToAttrDeco::GetCreator({2}), FixFormatDeco::Creator}},
    {prim::kPrimConcat->name(), {FixFormatDeco::Creator}},
    {prim::kPrimStridedSlice->name(), {FixFormatDeco::Creator}},
    {prim::kPrimConv2DFusion->name(), {SubstituteConv2D::Creator}},
    {prim::kPrimMatMulFusion->name(), {MatmulPackB::Creator}},
    {prim::kPrimAvgPoolFusion->name(), {PoolLayoutDeco::Creator}},
    {prim::kPrimMaxPoolFusion->name(), {PoolLayoutDeco::Creator}},
  };
  auto iter = creators.find(GetCNodePrimitive(node)->name());
  if (iter != creators.end()) {
    (void)decos.insert(decos.end(), iter->second.begin(), iter->second.end());
  }
  return WrapExpander(expander, decos);
}

void GraphKernelExpanderLite::PreProcessAllNode(const CNodePtr &node) {
  if (!AnfUtils::IsGraphKernel(node)) {
    BasicOpInferShape().InferShape(node);
  }
}
}  // namespace mindspore::graphkernel
