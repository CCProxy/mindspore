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

#include "backend/common/pass/insert_type_transform_op.h"

#include <memory>
#include <vector>
#include "backend/common/session/anf_runtime_algorithm.h"
#include "include/common/utils/utils.h"
#include "include/common/utils/anfalgo.h"
#include "kernel/common_utils.h"

namespace mindspore {
namespace opt {
int64_t SplitTupleInputs(const FuncGraphPtr &graph, const AnfNodePtr &tuple_input,
                         std::vector<AnfNodePtr> *plant_inputs) {
  MS_EXCEPTION_IF_NULL(graph);
  MS_EXCEPTION_IF_NULL(tuple_input);
  MS_EXCEPTION_IF_NULL(plant_inputs);

  if (!common::AnfAlgo::IsTupleOutput(tuple_input)) {
    auto abs = tuple_input->abstract();
    MS_EXCEPTION_IF_NULL(abs);
    MS_LOG(WARNING) << "The Function only split the output type is tuple type but got" << abs->ToString();
    return -1;
  }
  MS_EXCEPTION_IF_NULL(plant_inputs);
  auto input_size = AnfAlgo::GetOutputTensorNum(tuple_input);
  if (tuple_input->isa<CNode>() && common::AnfAlgo::CheckPrimitiveType(tuple_input, prim::kPrimMakeTuple)) {
    auto make_tuple = tuple_input->cast<CNodePtr>();
    MS_EXCEPTION_IF_NULL(make_tuple);
    size_t tuple_input_num = common::AnfAlgo::GetInputTensorNum(make_tuple);
    for (size_t j = 0; j < tuple_input_num; ++j) {
      // using for graph kernel
      auto dyn_input_node = common::AnfAlgo::GetInputNode(make_tuple, j);
      MS_EXCEPTION_IF_NULL(dyn_input_node);
      // Handle tuple nested scenes.
      if (dyn_input_node->isa<CNode>() && common::AnfAlgo::CheckPrimitiveType(dyn_input_node, prim::kPrimMakeTuple)) {
        input_size += SplitTupleInputs(graph, dyn_input_node, plant_inputs);
        continue;
      }
      (void)plant_inputs->emplace_back(dyn_input_node);
    }
    return input_size;
  }
  for (size_t index = 0; index < input_size; ++index) {
    auto dynamic_input_node = CreatTupleGetItemNode(graph, tuple_input, index);
    MS_LOG(DEBUG) << "Create TupleGetItem node " << dynamic_input_node->fullname_with_scope() << " for tuple node "
                  << tuple_input->fullname_with_scope();
    // The virtual node's object types should be set.
    SetKernelInfoForNewCNode(dynamic_input_node, false);
    (void)plant_inputs->emplace_back(dynamic_input_node);
  }
  return input_size;
}

AnfNodePtr CreateNewNode(const FuncGraphPtr &func_graph, const AnfNodePtrList &input_list,
                         const CNodePtr &origin_node) {
  MS_EXCEPTION_IF_NULL(func_graph);
  MS_EXCEPTION_IF_NULL(origin_node);

  auto new_cnode = NewCNode(input_list, func_graph, {origin_node});
  MS_EXCEPTION_IF_NULL(new_cnode);
  // This pass should not have new node whose abstract differs from the original node. So set the original node's
  // abstract.
  new_cnode->set_abstract(origin_node->abstract());
  new_cnode->set_scope(origin_node->scope());
  new_cnode->set_primal_attrs(origin_node->primal_attrs());
  new_cnode->set_attrs(origin_node->attrs());
  auto kernel_graph = func_graph->cast<KernelGraphPtr>();
  if (kernel_graph != nullptr) {
    kernel_graph->FrontBackendlMapUpdate(origin_node, new_cnode);
  }

  // Need to reset new cnode's kernel build info because the inputs type and number could be changed after processing
  // methods. Only reset input types.
  SetKernelInfoForNewCNodeByOrigNode(new_cnode, origin_node);
  return new_cnode;
}

AnfNodePtr CreateRealMakeTupleByMakeTuple(const FuncGraphPtr &func_graph, const CNodePtr &make_tuple_node) {
  MS_EXCEPTION_IF_NULL(func_graph);
  MS_EXCEPTION_IF_NULL(make_tuple_node);

  // Create RealMakeTuple node and inherit inputs and abstract from MakeTuple node.
  AnfNodePtrList inputs = make_tuple_node->inputs();
  auto prim = NewValueNode(prim::kPrimRealMakeTuple);
  MS_EXCEPTION_IF_NULL(prim);
  inputs[kIndex0] = prim;
  CNodePtr real_make_tuple = func_graph->NewCNode(inputs);
  MS_EXCEPTION_IF_NULL(real_make_tuple);
  real_make_tuple->set_abstract(make_tuple_node->abstract());

  SetKernelInfoForNewCNode(real_make_tuple);
  return real_make_tuple;
}

AnfNodePtr CreateRealMakeTupleByTupleUnfoldInput(const FuncGraphPtr &func_graph,
                                                 const AnfNodePtr &node_with_tuple_unfold_output) {
  MS_EXCEPTION_IF_NULL(func_graph);
  MS_EXCEPTION_IF_NULL(node_with_tuple_unfold_output);

  auto prim = NewValueNode(prim::kPrimRealMakeTuple);
  MS_EXCEPTION_IF_NULL(prim);
  AnfNodePtrList inputs = {prim, node_with_tuple_unfold_output};
  CNodePtr real_make_tuple = func_graph->NewCNode(inputs);
  MS_EXCEPTION_IF_NULL(real_make_tuple);
  // Inherit abstract from TupleUnfold output node.
  real_make_tuple->set_abstract(node_with_tuple_unfold_output->abstract());

  SetKernelInfoForNewCNode(real_make_tuple);

  // Set object type to TupleUnfold so TupleUnfoldToTupleUnfold pattern will be matched.
  KernelBuildInfoPtr real_make_tuple_build_info = AnfAlgo::GetSelectKernelBuildInfo(real_make_tuple);
  MS_EXCEPTION_IF_NULL(real_make_tuple_build_info);
  real_make_tuple_build_info->SetInputsKernelObjectType({KernelObjectType::TUPLE_UNFOLD});
  return real_make_tuple;
}

void SetKernelInfoForNewCNodeByOrigNode(const CNodePtr &new_cnode, const CNodePtr &origin_node) {
  MS_EXCEPTION_IF_NULL(new_cnode);
  MS_EXCEPTION_IF_NULL(origin_node);

  // Inherit from origin kernel build info.
  KernelBuildInfoPtr origin_kernel_build_info = AnfAlgo::GetSelectKernelBuildInfo(origin_node);
  MS_EXCEPTION_IF_NULL(origin_kernel_build_info);
  auto new_kernel_builder = std::make_shared<KernelBuildInfoBuilder>(origin_kernel_build_info);
  MS_EXCEPTION_IF_NULL(new_kernel_builder);

  auto kernel_info = std::make_shared<device::KernelInfo>();
  MS_EXCEPTION_IF_NULL(kernel_info);
  new_cnode->set_kernel_info(kernel_info);
  AnfAlgo::SetSelectKernelBuildInfo(new_kernel_builder->Build(), new_cnode.get());
}

void SetKernelInfoForNewCNode(const CNodePtr &cnode, bool set_format_type) {
  MS_EXCEPTION_IF_NULL(cnode);

  auto kernel_info = std::make_shared<device::KernelInfo>();
  MS_EXCEPTION_IF_NULL(kernel_info);
  cnode->set_kernel_info(kernel_info);
  auto builder = std::make_shared<KernelBuildInfoBuilder>();
  MS_EXCEPTION_IF_NULL(builder);

  // Set input and output object type for subsequent type matching process.
  std::vector<KernelObjectType> input_obj_type;
  std::vector<KernelObjectType> output_obj_type;
  GenerateKernelObjectTypeForNewCNode(cnode, &input_obj_type, &output_obj_type);
  builder->SetInputsKernelObjectType(input_obj_type);
  builder->SetOutputsKernelObjectType(output_obj_type);

  if (set_format_type) {
    // Set input and output format.
    std::vector<std::string> inputs_format;
    std::vector<TypeId> inputs_type;
    size_t input_num = common::AnfAlgo::GetInputTensorNum(cnode);
    for (size_t input_index = 0; input_index < input_num; ++input_index) {
      inputs_format.emplace_back(AnfAlgo::GetPrevNodeOutputFormat(cnode, input_index));
      inputs_type.push_back(common::AnfAlgo::GetPrevNodeOutputInferDataType(cnode, input_index));
    }
    std::vector<std::string> outputs_format;
    std::vector<TypeId> outputs_type;
    // The output number should be 1 before object type is set.
    size_t output_num = AnfAlgo::GetOutputTensorNum(cnode);
    for (size_t output_index = 0; output_index < output_num; ++output_index) {
      outputs_format.emplace_back(GenerateOutputFormatForNewCNode(cnode));
      outputs_type.push_back(common::AnfAlgo::GetOutputInferDataType(cnode, output_index));
    }

    builder->SetInputsFormat(inputs_format);
    builder->SetInputsDeviceType(inputs_type);
    builder->SetOutputsFormat(outputs_format);
    builder->SetOutputsDeviceType(outputs_type);
  }

  AnfAlgo::SetSelectKernelBuildInfo(builder->Build(), cnode.get());
}

abstract::AbstractBasePtr GenerateAbsByUserNodeInput(const CNodePtr &user_node, size_t input_index) {
  MS_EXCEPTION_IF_NULL(user_node);
  auto shape = AnfAlgo::GetInputDeviceShape(user_node, input_index);
  auto type_id = AnfAlgo::GetInputDeviceDataType(user_node, input_index);
  // Defaultly the input is a tensor. Other cases should be handled respectively.
  auto abs = std::make_shared<abstract::AbstractTensor>(TypeIdToType(type_id), shape);
  MS_EXCEPTION_IF_NULL(abs);
  return abs;
}

std::string GenerateOutputFormatForNewCNode(const CNodePtr &cnode) {
  MS_EXCEPTION_IF_NULL(cnode);
  if (IsPrimitiveCNode(cnode, prim::kPrimRealMakeTuple) || IsPrimitiveCNode(cnode, prim::kPrimTupleToTensor)) {
    // We take first input format as the output format because multiple types and formats of RealMakeTuple/TupleToTensor
    // are not supported.
    std::string represent_format = AnfAlgo::GetPrevNodeOutputFormat(cnode, kIndex0);
    return represent_format;
  }
  return kOpFormat_DEFAULT;
}

void GenerateKernelObjectTypeForNewCNode(const CNodePtr &cnode, std::vector<KernelObjectType> *input_obj_type,
                                         std::vector<KernelObjectType> *output_obj_type) {
  MS_EXCEPTION_IF_NULL(cnode);
  MS_EXCEPTION_IF_NULL(input_obj_type);
  MS_EXCEPTION_IF_NULL(output_obj_type);

  // Simply trasverse all inputs and get their object types.
  // But if the input's object type is not set, this will throw exception so must pay attention when using this
  // function.
  auto general_input_obj_type_func = [&]() {
    for (size_t i = kIndex1; i < cnode->inputs().size(); i++) {
      // Set input kernel object type as input node's output kernel object type.
      auto kernel_build_info = AnfAlgo::GetSelectKernelBuildInfo(cnode->input(i));
      input_obj_type->push_back(kernel_build_info->GetOutputKernelObjectType(kIndex0));
    }
  };

  if (IsPrimitiveCNode(cnode, prim::kPrimRealMakeTuple)) {
    general_input_obj_type_func();
    output_obj_type->push_back(KernelObjectType::TUPLE);
  } else if (IsPrimitiveCNode(cnode, prim::kPrimTupleToTensor)) {
    general_input_obj_type_func();
    output_obj_type->push_back(KernelObjectType::TENSOR);
  } else if (IsPrimitiveCNode(cnode, prim::kPrimTupleGetItem)) {
    // First input of TupleGetItem must be TUPLE_UNFOLD.
    // Second is the index.
    *input_obj_type = {KernelObjectType::TUPLE_UNFOLD, KernelObjectType::TENSOR};
    // Get actual output type of TupleGetItem node.
    auto abs_type = AnfAlgo::GetAbstractObjectType(cnode->abstract());
    output_obj_type->push_back(kernel::TypeIdToKernelObjectType(abs_type));
  }
}

// A map of kernel object type pairs to processing functions.
static std::map<ObjectTypePair, ProcessTypeTransformFunc> kTypePairToProcessFunc;

// The nodes of which object types should be handled.
const std::vector<PrimitivePtr> need_handled_types = {prim::kPrimMakeTuple, prim::kPrimTupleGetItem};

InsertTypeTransformOp::InsertTypeTransformOp(bool multigraph)
    : PatternProcessPass("insert_type_transform_op", multigraph) {
  kTypePairToProcessFunc[{KernelObjectType::TUPLE_UNFOLD, KernelObjectType::TUPLE_UNFOLD}] =
    std::bind(&InsertTypeTransformOp::ProcessTupleUnfoldToTupleUnfold, this, std::placeholders::_1,
              std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);
  kTypePairToProcessFunc[{KernelObjectType::TUPLE_UNFOLD, KernelObjectType::TUPLE}] =
    std::bind(&InsertTypeTransformOp::ProcessTupleUnfoldToTuple, this, std::placeholders::_1, std::placeholders::_2,
              std::placeholders::_3, std::placeholders::_4);
  kTypePairToProcessFunc[{KernelObjectType::TUPLE_UNFOLD, KernelObjectType::TENSOR}] =
    std::bind(&InsertTypeTransformOp::ProcessTupleUnfoldToTensor, this, std::placeholders::_1, std::placeholders::_2,
              std::placeholders::_3, std::placeholders::_4);
}

const AnfNodePtr InsertTypeTransformOp::Process(const FuncGraphPtr &func_graph, const AnfNodePtr &node,
                                                const EquivPtr &) const {
  MS_EXCEPTION_IF_NULL(func_graph);
  MS_EXCEPTION_IF_NULL(node);
  if (!node->isa<CNode>()) {
    return nullptr;
  }
  if ((node->kernel_info() == nullptr) ||
      (!dynamic_cast<device::KernelInfo *>(node->kernel_info())->has_build_info())) {
    return nullptr;
  }

  auto cnode = node->cast<CNodePtr>();
  MS_EXCEPTION_IF_NULL(cnode);
  AnfNodePtrList new_input_list = {common::AnfAlgo::GetCNodePrimitiveNode(cnode)};
  // If kernel object types are matched, set this flag to true and new node will be created to replace original node.
  bool matched = false;
  for (size_t i = 0; i < common::AnfAlgo::GetInputNum(cnode); ++i) {
    const auto &input_node = common::AnfAlgo::GetInputNode(cnode, i);
    // Skip for monad input.
    if (HasAbstractMonad(input_node)) {
      new_input_list.push_back(input_node);
      continue;
    }

    const auto &real_input_node =
      common::AnfAlgo::VisitKernelWithReturnType(input_node, 0, false, need_handled_types).first;
    MS_EXCEPTION_IF_NULL(real_input_node);
    if ((real_input_node->kernel_info() == nullptr) ||
        (!dynamic_cast<device::KernelInfo *>(real_input_node->kernel_info())->has_build_info())) {
      MS_LOG(DEBUG) << node->fullname_with_scope() << " input index:" << i
                    << ", input node:" << real_input_node->fullname_with_scope() << " doesn't have build info.";
      new_input_list.push_back(input_node);
      continue;
    }

    auto needed_input_type = AnfAlgo::GetInputKernelObjectType(node, i);
    auto current_input_type = AnfAlgo::GetOutputKernelObjectType(real_input_node, 0);
    if ((kObjectTypeToString.count(needed_input_type) == 0) || (kObjectTypeToString.count(current_input_type) == 0)) {
      MS_LOG(EXCEPTION) << "The current input object type " << current_input_type << " or needed input object type "
                        << needed_input_type << " is not valid for node " << node->fullname_with_scope()
                        << " input index:" << i << ", input node:" << real_input_node->fullname_with_scope();
    }
    MS_LOG(DEBUG) << "The current input object type " << kObjectTypeToString[current_input_type]
                  << " or needed input object type " << kObjectTypeToString[needed_input_type]
                  << " is not valid for node " << node->fullname_with_scope() << " input index:" << i
                  << ", input node:" << real_input_node->fullname_with_scope();

    ObjectTypePair type_pair = {current_input_type, needed_input_type};
    if (kTypePairToProcessFunc.count(type_pair) != 0) {
      matched = true;
      MS_LOG(INFO) << "Kernel object type pair of input index " << i << " for node pair "
                   << input_node->fullname_with_scope() << " to " << cnode->fullname_with_scope() << " is "
                   << type_pair.to_string();
      bool new_prim = false;
      AnfNodePtrList processed_input_list = kTypePairToProcessFunc[type_pair](func_graph, input_node, cnode, &new_prim);
      if (new_prim) {
        // If new primitive is created, replace the old one, which is the first element of the input list.
        new_input_list[kIndex0] = processed_input_list[kIndex0];
        // Jump the primitive node the first one, and the rest is the new inputs.
        new_input_list.insert(new_input_list.end(), std::begin(processed_input_list) + kIndex1,
                              processed_input_list.end());
      } else {
        new_input_list.insert(new_input_list.end(), processed_input_list.begin(), processed_input_list.end());
      }
    } else {
      // If this input type is valid, just push back the origin input.
      new_input_list.push_back(input_node);
    }
  }

  if (matched) {
    // Create replacing node, update front-end node map, set kernel build info, inherit attributes, etc. These
    // operations could rely on the origin CNode.
    auto new_node = CreateNewNode(func_graph, new_input_list, cnode);
    return new_node;
  }
  return nullptr;
}

AnfNodePtrList InsertTypeTransformOp::ProcessTupleUnfoldToTupleUnfold(const FuncGraphPtr &func_graph,
                                                                      const AnfNodePtr &input, const CNodePtr &node,
                                                                      bool *) {
  MS_EXCEPTION_IF_NULL(func_graph);
  MS_EXCEPTION_IF_NULL(input);
  MS_EXCEPTION_IF_NULL(node);

  // If the input needs to be skipped as ConvertTupleInputToDynamicInput does, return the input node itself for caller
  // to construct input list.
  bool is_bprop_cut = common::AnfAlgo::CheckPrimitiveType(node, prim::kPrimBpropCut);
  bool skip = (is_bprop_cut && input->abstract()->isa<abstract::AbstractSparseTensor>()) ||
              IsPrimitiveCNode(node, prim::kPrimTupleGetItem);
  if (skip) {
    return {input};
  }

  AnfNodePtrList plant_inputs;
  int64_t unfold_num = SplitTupleInputs(func_graph, input, &plant_inputs);
  MS_LOG(DEBUG) << "Transform tuple unfold input: " << input->fullname_with_scope() << " to " << unfold_num
                << " inputs.";

  return plant_inputs;
}

AnfNodePtrList InsertTypeTransformOp::ProcessTupleUnfoldToTuple(const FuncGraphPtr &func_graph, const AnfNodePtr &input,
                                                                const CNodePtr &node, bool *) {
  MS_EXCEPTION_IF_NULL(func_graph);
  MS_EXCEPTION_IF_NULL(input);
  MS_EXCEPTION_IF_NULL(node);

  AnfNodePtrList result;
  AnfNodePtr real_make_tuple_node = nullptr;
  // If TupleUnfold input is a MakeTuple node, replace it with RealMakeTuple node.
  if (IsPrimitiveCNode(input, prim::kPrimMakeTuple)) {
    real_make_tuple_node = CreateRealMakeTupleByMakeTuple(func_graph, input->cast<CNodePtr>());
  } else {
    real_make_tuple_node = CreateRealMakeTupleByTupleUnfoldInput(func_graph, input);
  }
  result.push_back(real_make_tuple_node);
  return result;
}

AnfNodePtrList InsertTypeTransformOp::ProcessTupleUnfoldToTensor(const FuncGraphPtr &func_graph,
                                                                 const AnfNodePtr &input, const CNodePtr &node,
                                                                 bool *) {
  MS_EXCEPTION_IF_NULL(input);
  MS_EXCEPTION_IF_NULL(node);

  // Use TupleToTensor op as the input of this node. Then TupleUnfoldToTuple pattern will be matched.
  auto prim = NewValueNode(prim::kPrimTupleToTensor);
  MS_EXCEPTION_IF_NULL(prim);
  AnfNodePtrList inputs = {prim, input};
  CNodePtr tuple_to_tensor = func_graph->NewCNode(inputs);
  MS_EXCEPTION_IF_NULL(tuple_to_tensor);

  // Set abstract for TupleToTensor op according to user node's input shape and type.
  size_t input_index = GetInputNodeIndex(input, node);
  auto abs = GenerateAbsByUserNodeInput(node, input_index);
  MS_EXCEPTION_IF_NULL(abs);
  MS_LOG(DEBUG) << "Abstract for TupleToTensor op is " << abs->ToString();
  tuple_to_tensor->set_abstract(abs);

  // Need to check if input is MakeTuple node, the format and device type could be abtained or not.
  SetKernelInfoForNewCNode(tuple_to_tensor);

  // Set object type to TUPLE for TupleUnfoldToTuple pattern to be matched.
  KernelBuildInfoPtr tuple_to_tensor_build_info = AnfAlgo::GetSelectKernelBuildInfo(tuple_to_tensor);
  MS_EXCEPTION_IF_NULL(tuple_to_tensor_build_info);
  tuple_to_tensor_build_info->SetInputsKernelObjectType({KernelObjectType::TUPLE});

  return {tuple_to_tensor};
}
}  // namespace opt
}  // namespace mindspore