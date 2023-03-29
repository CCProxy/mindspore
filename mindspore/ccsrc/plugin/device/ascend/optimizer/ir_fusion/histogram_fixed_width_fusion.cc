/**
 * Copyright 2023 Huawei Technologies Co., Ltd
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

#include "plugin/device/ascend/optimizer/ir_fusion/histogram_fixed_width_fusion.h"

#include <memory>

#include "include/common/utils/anfalgo.h"

namespace mindspore {
namespace opt {
const BaseRef HistogramFixedWidthFusion::DefinePattern() const {
  VarPtr Xs = std::make_shared<SeqVar>();
  return VectorRef({prim::kPrimHistogramFixedWidth, Xs});
}

const AnfNodePtr HistogramFixedWidthFusion::Process(const FuncGraphPtr &graph, const AnfNodePtr &node,
                                                    const EquivPtr &) const {
  MS_EXCEPTION_IF_NULL(graph);
  MS_EXCEPTION_IF_NULL(node);

  auto cnode = node->cast<CNodePtr>();
  MS_EXCEPTION_IF_NULL(cnode);
  MS_LOG(DEBUG) << "Begin to convert attr to input for node: " << node->DebugString();

  const auto &origin_prim = common::AnfAlgo::GetCNodePrimitive(node);
  MS_EXCEPTION_IF_NULL(origin_prim);
  const auto &origin_attrs = origin_prim->attrs();

  constexpr auto kNbins = "nbins";
  if (origin_attrs.count(kNbins) == 0) {
    MS_LOG(DEBUG) << "Origin primitive: " << origin_prim->name() << "has no attr : " << kNbins;
    return node;
  }

  // Convert the specific attr to input and erase the specific attr.
  auto attr_value = origin_prim->GetAttr(kNbins);
  MS_EXCEPTION_IF_NULL(attr_value);
  auto new_value_node = std::make_shared<ValueNode>(attr_value);
  MS_EXCEPTION_IF_NULL(new_value_node);
  new_value_node->set_abstract(attr_value->ToAbstract());
  cnode->add_input(new_value_node);
  MS_LOG(DEBUG) << "End, new node: " << node->DebugString();
  return cnode;
}
}  // namespace opt
}  // namespace mindspore
