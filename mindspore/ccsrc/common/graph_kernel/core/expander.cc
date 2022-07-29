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

#include "common/graph_kernel/core/expander.h"

#include "utils/anf_utils.h"
#include "common/graph_kernel/core/graph_kernel_callback.h"
#include "common/graph_kernel/core/graph_kernel_utils.h"
#include "common/graph_kernel/expanders/op_desc_registry.h"
#include "backend/common/optimizer/const_input_to_attr.h"

namespace mindspore::graphkernel {
ExpanderPtr WrapExpander(const ExpanderPtr &base, const ExpanderCreatorFuncList &deco_creators) {
  ExpanderPtr result = base;
  for (auto it = deco_creators.rbegin(); it != deco_creators.rend(); ++it) {
    result = (*it)(result);
  }
  return result;
}

AnfNodePtr ExpanderDecorator::Run(const AnfNodePtr &node) { return node ? decorated_->Run(node) : nullptr; }

CNodePtr ExpanderDecorator::QuickCloneCNode(const AnfNodePtr &node) const {
  auto cnode = node->cast<CNodePtr>();
  MS_EXCEPTION_IF_NULL(cnode);
  auto func_graph = node->func_graph();
  MS_EXCEPTION_IF_NULL(func_graph);
  CNodePtr new_node = func_graph->NewCNode(cnode->inputs());
  new_node->CloneCNodeInfo(cnode);
  return new_node;
}

AnfNodePtr InputToAttrDeco::Run(const AnfNodePtr &node) {
  auto cnode = QuickCloneCNode(node);
  auto new_node = opt::ConstInputToAttr(cnode, input_idx_);
  auto graph = cnode->func_graph();
  MS_EXCEPTION_IF_NULL(graph);
  MS_EXCEPTION_IF_NULL(graph->manager());
  (void)graph->manager()->Replace(cnode, new_node);
  return decorated_->Run(new_node);
}

AnfNodePtr DefaultExpander::Run(const AnfNodePtr &node) {
  MS_LOG(DEBUG) << "Expanding node: " << node->fullname_with_scope();
  auto cnode = node->cast<CNodePtr>();
  MS_EXCEPTION_IF_NULL(cnode);
  auto new_fg = ExpandToGraph(cnode);
  auto res = new_fg ? "success" : "failed";
  MS_LOG(DEBUG) << "Expanding node " << res << " : " << node->fullname_with_scope();
  if (new_fg == nullptr) {
    return nullptr;
  }
  AnfNodePtrList inputs = {NewValueNode(new_fg)};
  (void)inputs.insert(inputs.end(), cnode->inputs().cbegin() + 1, cnode->inputs().cend());
  return node->func_graph()->NewCNode(inputs);
}

FuncGraphPtr DefaultExpander::ExpandToGraph(const CNodePtr &node) {
  auto op_desc = expanders::OpDescFactory::Instance().GetOp(AnfUtils::GetCNodeName(node));
  if (op_desc == nullptr) {
    MS_LOG(INFO) << "expander not found " << node->fullname_with_scope();
    return nullptr;
  }
  expanders::BaseInfoList inputs(node->size() - 1);
  expanders::BaseInfoList outputs(AnfUtils::GetOutputTensorNum(node));
  MS_EXCEPTION_IF_NULL(cb_);
  for (size_t i = 0; i < inputs.size(); i++) {
    inputs[i].shape = cb_->GetInputShape(node, i);
    inputs[i].type = cb_->GetInputType(node, i);
    inputs[i].format = cb_->GetInputFormat(node, i);
  }
  for (size_t i = 0; i < outputs.size(); i++) {
    outputs[i].shape = cb_->GetOutputShape(node, i);
    outputs[i].type = cb_->GetOutputType(node, i);
    outputs[i].format = cb_->GetOutputFormat(node, i);
  }
  auto &attrs = GetCNodePrimitive(node)->attrs();
  auto litegraph = op_desc->Run(inputs, outputs, attrs, cb_->GetProcessor(node));
  if (litegraph == nullptr) {
    MS_LOG(INFO) << "undo expanding " << node->fullname_with_scope();
    return nullptr;
  }
  return GkUtils::LiteGraph2AnfGraph(litegraph, cb_);
}
}  // namespace mindspore::graphkernel
