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

#include <string>
#include <algorithm>
#include <utility>
#include <vector>
#include <map>
#include <memory>

#include "src/extendrt/utils/func_graph_utils.h"
#include "include/common/utils/convert_utils.h"
#include "mindspore/ccsrc/include/backend/optimizer/helper.h"

#include "ops/op_name.h"
#include "tools/optimizer/format/to_nhwc_format.h"
#include "tools/optimizer/graph/decrease_transpose_algo.h"

namespace mindspore {
const PrimitivePtr kPrimMakeTupleV2 = std::make_shared<Primitive>("make_tuple");
ValuePtr FuncGraphUtils::GetNodeValuePtr(AnfNodePtr input_node) {
  if (input_node == nullptr) {
    return nullptr;
  }
  if (IsPrimitiveCNode(input_node, prim::kPrimDepend)) {
    input_node = AnfUtils::VisitKernel(input_node, 0).first;
  }
  ValuePtr value = nullptr;
  if (input_node->isa<ValueNode>() && !HasAbstractMonad(input_node)) {
    auto value_node = input_node->cast<ValueNodePtr>();
    if (value_node) {
      value = value_node->value();
    }
  } else if (input_node->isa<Parameter>()) {
    auto parameter = input_node->cast<ParameterPtr>();
    if (parameter->has_default()) {
      value = parameter->default_param();
    }
  }
  return value;
}

tensor::TensorPtr FuncGraphUtils::GetConstNodeValue(AnfNodePtr input_node) {
  ValuePtr value = GetNodeValuePtr(input_node);
  if (value == nullptr) {
    return nullptr;
  }
  if (value->isa<tensor::Tensor>()) {
    auto tensor = value->cast<tensor::TensorPtr>();
    if (tensor == nullptr || tensor->data().const_data() == nullptr) {
      return nullptr;
    }
    return tensor;
  }
  if (value->isa<Scalar>()) {
    return ScalarToTensor(value->cast<ScalarPtr>());
  }
  if (value->isa<ValueTuple>()) {
    return opt::CreateTupleTensor(value->cast<ValueTuplePtr>());
  }
  if (value->isa<Type>()) {
    auto type_ptr = value->cast<TypePtr>();
    if (type_ptr == nullptr) {
      return nullptr;
    }
    return std::make_shared<tensor::Tensor>(static_cast<int64_t>(type_ptr->type_id()), type_ptr->type());
  }
  MS_LOG(WARNING) << "Unexpected value type " << value->type_name() << " for " << input_node->fullname_with_scope();
  return nullptr;
}

bool FuncGraphUtils::GetCNodeOperator(const mindspore::CNodePtr &cnode,
                                      mindspore::kernel::BaseOperatorPtr *base_operator) {
  if (!cnode || !base_operator) {
    MS_LOG(ERROR) << "Input cnode or base_operator cannot be nullptr";
    return false;
  }
  auto prim = GetValueNode<PrimitivePtr>(cnode->input(0));
  MS_EXCEPTION_IF_NULL(prim);
  if (!prim) {
    MS_LOG(ERROR) << "Primitive of cnode " << cnode->fullname_with_scope() << " cannot be nullptr";
    return false;
  }
  auto kernel_name = prim->name();
  ops::PrimitiveCPtr primc_ptr = nullptr;
  static auto &primc_fns = ops::OpPrimCRegister::GetInstance().GetPrimCMap();
  auto primc_it = primc_fns.find(kernel_name);
  if (primc_it != primc_fns.end() && primc_it->second) {
    primc_ptr = primc_it->second();
  }
  if (primc_ptr == nullptr) {
    MS_LOG(ERROR) << "OpPrimCRegister can not find " << kernel_name;
    return false;
  }
  (void)primc_ptr->SetAttrs(prim->attrs());

  *base_operator = nullptr;
  static auto &operator_fns = ops::OperatorRegister::GetInstance().GetOperatorMap();
  auto op_it = operator_fns.find(kernel_name);
  if (op_it != operator_fns.end() && op_it->second) {
    *base_operator = op_it->second(primc_ptr);
  }
  if (*base_operator == nullptr) {
    MS_LOG(ERROR) << "Failed to create operator of type " << kernel_name;
    return false;
  }
  return true;
}

bool CheckPrimitiveType(const AnfNodePtr &node, const PrimitivePtr &primitive_type) {
  if (node == nullptr || primitive_type == nullptr) {
    return false;
  }
  if (node->isa<CNode>()) {
    auto cnode = node->cast<CNodePtr>();
    return IsPrimitive(cnode->input(kAnfPrimitiveIndex), primitive_type);
  } else if (node->isa<ValueNode>()) {
    return IsPrimitive(node, primitive_type);
  }
  return false;
}

std::vector<common::KernelWithIndex> FuncGraphUtils::GetNodeInputs(const AnfNodePtr &anf_node) {
  if (!anf_node) {
    return {};
  }
  if (!anf_node->isa<CNode>()) {
    return {{anf_node, 0}};
  }
  auto cnode = anf_node->cast<CNodePtr>();
  std::vector<common::KernelWithIndex> inputs;
  size_t input_num = common::AnfAlgo::GetInputTensorNum(cnode);
  for (size_t input_idx = 0; input_idx < input_num; ++input_idx) {
    const auto &pre_node_output = common::AnfAlgo::GetPrevNodeOutput(cnode, input_idx);
    auto pre_node = pre_node_output.first;
    if (CheckPrimitiveType(pre_node, prim::kPrimMakeTuple) || CheckPrimitiveType(pre_node, kPrimMakeTupleV2)) {
      auto tuple_inputs = GetNodeInputs(pre_node);
      std::copy(tuple_inputs.begin(), tuple_inputs.end(), std::back_inserter(inputs));
    } else if (CheckPrimitiveType(pre_node, prim::kPrimSplit) &&
               CheckPrimitiveType(cnode->input(1), prim::kPrimSplit)) {
      inputs = common::AnfAlgo::GetAllOutputWithIndex(pre_node);
    } else {
      inputs.push_back(pre_node_output);
    }
  }
  return inputs;
}

bool FuncGraphUtils::GetCNodeInputsOutputs(const mindspore::CNodePtr &cnode,
                                           std::vector<AnfWithOutIndex> *input_tensors,
                                           std::vector<AnfWithOutIndex> *output_tensors) {
  if (!cnode || !input_tensors || !output_tensors) {
    MS_LOG(ERROR) << "Input cnode, input_tensors or output_tensors cannot be nullptr";
    return false;
  }
  // Makeup input tensors.
  *input_tensors = GetNodeInputs(cnode);
  // Makeup output tensors.
  output_tensors->clear();
  auto output_num = AnfUtils::GetOutputTensorNum(cnode);
  for (size_t output_idx = 0; output_idx < output_num; ++output_idx) {
    session::KernelWithIndex tensor_id = {cnode, output_idx};
    output_tensors->push_back(tensor_id);
  }
  return true;
}

bool FuncGraphUtils::GetFuncGraphInputs(const FuncGraphPtr &func_graph, std::vector<AnfWithOutIndex> *inputs) {
  if (!func_graph || !inputs) {
    MS_LOG(ERROR) << "Input func_graph or inputs cannot be nullptr";
    return false;
  }
  auto graph_inputs = func_graph->get_inputs();
  // find parameters of graph inputs
  for (size_t i = 0; i < graph_inputs.size(); ++i) {
    auto input = graph_inputs[i];
    auto parameter = input->cast<ParameterPtr>();
    if (!parameter) {
      MS_LOG(ERROR) << "Input " << parameter->fullname_with_scope() << " of FuncGraph is not type of Parameter.";
      return false;
    }
    if (common::AnfAlgo::IsParameterWeight(parameter)) {
      continue;
    }
    inputs->push_back(std::make_pair(input, 0));
  }
  return true;
}

bool FuncGraphUtils::GetFuncGraphOutputs(const FuncGraphPtr &func_graph, std::vector<AnfWithOutIndex> *outputs) {
  if (!func_graph || !outputs) {
    MS_LOG(ERROR) << "Input func_graph or outputs cannot be nullptr";
    return false;
  }
  *outputs = GetNodeInputs(func_graph->get_return());
  return true;
}

DataType FuncGraphUtils::GetTensorDataType(const AnfWithOutIndex &tensor) {
  auto node = tensor.first;
  auto output_idx = tensor.second;
  auto tensor_val = GetConstNodeValue(node);
  TypeId type_id;
  if (tensor_val) {
    type_id = tensor_val->Dtype()->type_id();
  } else {
    type_id = common::AnfAlgo::GetOutputInferDataType(node, output_idx);
  }
  return static_cast<enum DataType>(type_id);
}

ShapeVector FuncGraphUtils::GetTensorShape(const AnfWithOutIndex &tensor) {
  auto node = tensor.first;
  auto output_idx = tensor.second;
  auto tensor_val = GetConstNodeValue(node);
  ShapeVector shape;
  if (tensor_val) {
    shape = tensor_val->shape_c();
  } else {
    shape = common::AnfAlgo::GetOutputInferShape(node, output_idx);
  }
  return shape;
}

Status FuncGraphUtils::UnifyGraphToNHWCFormat(const FuncGraphPtr &graph) {
  auto value = graph->get_attr(ops::kFormat);
  if (value != nullptr && GetValue<int64_t>(value) != mindspore::NHWC) {
    auto format_pass = std::make_shared<opt::ToNHWCFormat>();
    MS_CHECK_TRUE_RET(format_pass != nullptr, kLiteNullptr);
    if (!format_pass->Run(graph)) {
      MS_LOG(ERROR) << "DefaultGraphCompiler::Partition Run ToNHWCFormat pass failed";
      return kLiteNullptr;
    }
    auto transpose_pass = std::make_shared<opt::DecreaseTransposeAlgo>();
    MS_CHECK_TRUE_RET(transpose_pass != nullptr, kLiteNullptr);
    if (!transpose_pass->Run(graph)) {
      MS_LOG(ERROR) << "DefaultGraphCompiler::Partition Run DecreaseTransposeAlgo pass failed";
      return kLiteNullptr;
    }
  }
  return kSuccess;
}

std::string FuncGraphUtils::GetTensorName(const AnfWithOutIndex &tensor) {
  auto node = tensor.first;
  auto idx = tensor.second;
  MS_EXCEPTION_IF_NULL(node);
  AbstractBasePtr abstract = node->abstract();
  MS_EXCEPTION_IF_NULL(abstract);
  if (utils::isa<abstract::AbstractTuplePtr>(abstract)) {
    auto abstract_tuple = utils::cast<abstract::AbstractTuplePtr>(abstract);
    MS_EXCEPTION_IF_NULL(abstract_tuple);
    auto abstract_list = abstract_tuple->elements();
    if (abstract_list.size() <= idx) {
      MS_LOG(ERROR) << "AbstractTuple's size[" << abstract_list.size() << "] is smaller than expect size[" << idx
                    << "]";
      return "";
    }
    abstract = abstract_list[idx];
    MS_EXCEPTION_IF_NULL(abstract);
  }
  MS_EXCEPTION_IF_NULL(abstract);
  std::string output_name;
  if (!abstract->name().empty()) {
    output_name = abstract->name();
  } else if (idx > 0) {
    output_name = node->fullname_with_scope() + ":" + std::to_string(idx);
  } else {
    output_name = node->fullname_with_scope();
  }
  return output_name;
}

void FuncGraphUtils::GetFuncGraphInputsInfo(const FuncGraphPtr &func_graph, std::vector<tensor::TensorPtr> *inputs,
                                            std::vector<std::string> *inputs_name) {
  MS_EXCEPTION_IF_NULL(func_graph);
  MS_EXCEPTION_IF_NULL(inputs);
  MS_EXCEPTION_IF_NULL(inputs_name);
  std::vector<AnfWithOutIndex> input_idxs;
  if (!GetFuncGraphInputs(func_graph, &input_idxs)) {
    MS_LOG(ERROR) << "Failed to get input infos from graph";
    return;
  }
  inputs->clear();
  inputs_name->clear();
  for (auto &tensor : input_idxs) {
    auto name = FuncGraphUtils::GetTensorName(tensor);
    auto data_type = FuncGraphUtils::GetTensorDataType(tensor);
    auto shape = FuncGraphUtils::GetTensorShape(tensor);
    auto ms_tensor = std::make_shared<tensor::Tensor>(static_cast<TypeId>(data_type), shape);
    ms_tensor->set_name(name);
    inputs->push_back(ms_tensor);
    inputs_name->push_back(name);
  }
}

void FuncGraphUtils::GetFuncGraphOutputsInfo(const FuncGraphPtr &func_graph, std::vector<tensor::TensorPtr> *outputs,
                                             std::vector<std::string> *output_names) {
  MS_EXCEPTION_IF_NULL(func_graph);
  MS_EXCEPTION_IF_NULL(outputs);
  MS_EXCEPTION_IF_NULL(output_names);
  std::vector<AnfWithOutIndex> output_idxs;
  if (!GetFuncGraphOutputs(func_graph, &output_idxs)) {
    MS_LOG(ERROR) << "Failed to get input infos from graph";
    return;
  }
  outputs->clear();
  output_names->clear();
  for (auto &tensor : output_idxs) {
    auto name = FuncGraphUtils::GetTensorName(tensor);
    auto data_type = FuncGraphUtils::GetTensorDataType(tensor);
    auto shape = FuncGraphUtils::GetTensorShape(tensor);
    auto ms_tensor = std::make_shared<tensor::Tensor>(static_cast<TypeId>(data_type), shape);
    ms_tensor->set_name(name);
    outputs->push_back(ms_tensor);
    output_names->push_back(name);
  }
}
}  // namespace mindspore
