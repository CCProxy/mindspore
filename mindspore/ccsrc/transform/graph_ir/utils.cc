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
#include "include/transform/graph_ir/utils.h"
#include "transform/graph_ir/convert.h"
#include "transform/graph_ir/op_adapter_map.h"
#include "transform/graph_ir/op_adapter_util.h"
#include "transform/graph_ir/df_graph_manager.h"
#include "transform/graph_ir/op_adapter_desc.h"
#include "transform/graph_ir/transform_util.h"
#include "transform/graph_ir/graph_builder.h"

namespace mindspore {
namespace transform {
namespace {
constexpr size_t kSwitchInputSize = 4;
constexpr size_t kSwitchBodyIndex = 2;
constexpr size_t kSwitchAfterIndex = 3;
}  // namespace

OpAdapterPtr FindAdapter(const AnfNodePtr node, bool train) {
  MS_EXCEPTION_IF_NULL(node);
  if (node->isa<CNode>()) {
    auto cnode = node->cast<CNodePtr>();

    std::string name = kNameCustomOp;
    if (!IsCustomCNode(cnode)) {
      name = GetCNodeTargetFuncName(cnode);
    }

    auto it_adpt = OpAdapterMap::get().find(name);
    if (it_adpt != OpAdapterMap::get().end()) {
      return it_adpt->second->Get(train);
    }
    MS_LOG(EXCEPTION) << "Can't find OpAdapter for " << name;
  }

  if (node->isa<ValueNode>()) {
    return OpAdapterMap::get()[kNameConst]->Get(train);
  }
  if (node->isa<Parameter>()) {
    return OpAdapterMap::get()[kNameParam]->Get(train);
  }
  return OpAdapterPtr(nullptr);
}

OpAdapterPtr FindAdapter(const std::string &name, bool train) {
  auto it = OpAdapterMap::get().find(name);
  if (it != OpAdapterMap::get().end()) {
    return it->second->Get(train);
  }
  MS_LOG(EXCEPTION) << "Can't find OpAdapter for " << name;
}

void EraseGeResource() {
  DfGraphManager::GetInstance().DeleteGraphRunner();
  DfGraphManager::GetInstance().EraseAnfGraph();
  DfGraphManager::GetInstance().DeleteGeSession();
}

void ClearGraphWrapper() { DfGraphManager::GetInstance().ClearGraph(); }

void ClearGeSessionAndRunner() {
  DfGraphManager::GetInstance().DeleteGraphRunner();
  DfGraphManager::GetInstance().DeleteGeSession();
}

bool IsPartialSuccNode(const AnfNodePtr node) {
  MS_EXCEPTION_IF_NULL(node);
  if (!node->isa<CNode>()) {
    return false;
  }
  auto cnode = node->cast<CNodePtr>();
  if (!cnode->inputs().empty()) {
    for (size_t i = 0; i < cnode->inputs().size(); i++) {
      if (IsPartialCNode(cnode->input(i))) {
        return true;
      }
    }
  }
  return false;
}

bool IsPartialCNode(const AnfNodePtr node) {
  MS_EXCEPTION_IF_NULL(node);
  if (!node->isa<CNode>()) {
    return false;
  }
  auto cnode = node->cast<CNodePtr>();
  if (GetCNodeFuncName(cnode) == prim::kPrimPartial->name()) {
    return true;
  }
  return false;
}

bool IsWhileNode(const AnfNodePtr &node) {
  if (!node->isa<CNode>()) {
    return false;
  }
  if (!IsPartialSuccNode(node)) {
    return false;
  }
  auto cnode = node->cast<CNodePtr>();
  if (!IsPartialCNode(cnode->input(0))) {
    return false;
  }
  auto partial_node = cnode->input(0);
  MS_EXCEPTION_IF_NULL(partial_node);

  auto c_partial_node = partial_node->cast<CNodePtr>();
  MS_EXCEPTION_IF_NULL(c_partial_node);

  auto graph_node_input = c_partial_node->input(1);
  MS_EXCEPTION_IF_NULL(graph_node_input);
  auto graph_node = graph_node_input->cast<ValueNodePtr>();
  MS_EXCEPTION_IF_NULL(graph_node);
  auto graph_node_value = graph_node->value();
  MS_EXCEPTION_IF_NULL(graph_node_value);
  auto cond_graph = graph_node_value->cast<FuncGraphPtr>();
  MS_EXCEPTION_IF_NULL(cond_graph);
  if (!cond_graph->recursive()) {
    return false;
  }
  const auto &cond_set = cond_graph->nodes();
  for (auto beg = cond_set.begin(); beg != cond_set.end(); ++beg) {
    if (!((*beg)->isa<CNode>())) {
      continue;
    }
    auto c_beg = (*beg)->cast<CNodePtr>();
    if (IsPartialSuccNode(c_beg) && c_beg->inputs().size() == kSwitchInputSize &&
        IsPartialCNode(c_beg->input(kSwitchBodyIndex)) && IsPartialCNode(c_beg->input(kSwitchAfterIndex)) &&
        GetCNodeFuncName(c_beg) == prim::kPrimSwitch->name()) {
      auto func_graph = node->func_graph();
      MS_LOG(DEBUG) << "there is while node: " << node->ToString() << " in graph: " << func_graph->ToString();
      return true;
    }
  }
  return false;
}

std::string GetCNodeTargetFuncName(const CNodePtr cnode) {
  if (IsCaseNode(cnode)) {
    return string(kNameCase);
  }
  if (IsWhileNode(cnode)) {
    return string(kNameWhile);
  }
  auto name = GetCNodeFuncName(cnode);
  if (name == "switch_layer") {
    name = "";
  }
  return name;
}

bool IsCaseNode(const CNodePtr node) {
  MS_EXCEPTION_IF_NULL(node);
  if (!node->inputs().empty() && node->input(0)->isa<CNode>() &&
      GetCNodeFuncName(node->input(0)->cast<CNodePtr>()) == "switch_layer") {
    return true;
  }
  return false;
}

std::vector<GeTensorPtr> ConvertInputTensors(const std::vector<MeTensorPtr> &me_tensors, const std::string &format) {
  return TransformUtil::ConvertInputTensors(me_tensors, format);
}

std::vector<MeTensorPtr> ConvertGeTensors(const std::vector<GeTensorPtr> &ge_tensors) {
  return TransformUtil::ConvertGeTensors(ge_tensors);
}

GeDataType ConvertDataType(const MeDataType &type) { return TransformUtil::ConvertDataType(type); }

MeTensorPtr ConvertGeTensor(GeTensorPtr ge_tensor, const ShapeVector &request_dims) {
  return TransformUtil::ConvertGeTensor(ge_tensor, request_dims);
}

MeTensorPtr ConvertGeTensor(const GeTensorPtr &tensor) { return TransformUtil::ConvertGeTensor(tensor); }

MeTensorPtr ConvertGeTensor(const GeTensorPtr &tensor, const TypeId &me_type) {
  return TransformUtil::ConvertGeTensor(tensor, me_type);
}

std::shared_ptr<transform::GraphRunner> GetGraphRunner() { return DfGraphManager::GetInstance().GetGraphRunner(); }

std::shared_ptr<ge::Session> GetGeSession() { return DfGraphManager::GetInstance().GetGeSession(); }

void SetGeSession(const std::shared_ptr<ge::Session> &sess_ptr) {
  DfGraphManager::GetInstance().SetGeSession(sess_ptr);
}

GraphRunnerPtr NewGraphRunner(const GraphRunnerOptions &options) {
  auto graph_runner = std::make_shared<transform::GraphRunner>(options);
  return graph_runner;
}

void SetGraphRunner(const GraphRunnerPtr &runner) { DfGraphManager::GetInstance().SetGraphRunner(runner); }
void ClearGraph() { DfGraphManager::GetInstance().ClearGraph(); }
Status AddGraph(const std::string &name, const DfGraphPtr &graph, const std::vector<transform::GeTensorPtr> &inputs,
                const OptionMap &options) {
  return DfGraphManager::GetInstance().AddGraph(name, graph, inputs, options);
}
void SetAnfGraph(const std::string &name, const AnfGraphPtr &anf_graph_ptr) {
  DfGraphManager::GetInstance().SetAnfGraph(name, anf_graph_ptr);
}

FuncGraphPtr GetAnfGraph(uint32_t graph_id) { return DfGraphManager::GetInstance().GetAnfGraph(graph_id); }

DfGraphWrapperPtr GetGraphByName(const std::string &name) { return DfGraphManager::GetInstance().GetGraphByName(name); }

// convert

DfGraphConvertorPtr NewConverter(const FuncGraphPtr &graph) {
  auto converter = std::make_shared<transform::DfGraphConvertor>(graph);
  return converter;
}

void SetTraining(DfGraphConvertorPtr converter, bool training) {
  MS_EXCEPTION_IF_NULL(converter);
  converter->set_training(training);
}

void BuildGraph(DfGraphConvertorPtr converter, const std::map<std::string, std::shared_ptr<tensor::Tensor>> &maps) {
  MS_EXCEPTION_IF_NULL(converter);
  (void)converter->ConvertAllNode().InitParam(maps).BuildGraph();
}

void GenerateBroadcastGraph(DfGraphConvertorPtr converter, const TensorOrderMap &tensors) {
  MS_EXCEPTION_IF_NULL(converter);
  (void)converter->GenerateBroadcastGraph(tensors);
}
void GenerateCheckpointGraph(DfGraphConvertorPtr converter) {
  MS_EXCEPTION_IF_NULL(converter);
  (void)converter->GenerateCheckpointGraph();
}
int ErrCode(DfGraphConvertorPtr converter) {
  MS_EXCEPTION_IF_NULL(converter);
  return converter->ErrCode();
}

void DrawComputeGraph(DfGraphConvertorPtr converter, const std::string &name) {
  MS_EXCEPTION_IF_NULL(converter);
  return converter->DrawComputeGraph(name);
}
void DrawInitGraph(DfGraphConvertorPtr converter, const std::string &name) {
  MS_EXCEPTION_IF_NULL(converter);
  return converter->DrawInitGraph(name);
}
void DrawSaveCheckpointGraph(DfGraphConvertorPtr converter, const std::string &name) {
  MS_EXCEPTION_IF_NULL(converter);
  return converter->DrawSaveCheckpointGraph(name);
}

DfGraphPtr GetComputeGraph(DfGraphConvertorPtr converter) {
  MS_EXCEPTION_IF_NULL(converter);
  return converter->GetComputeGraph();
}
DfGraphPtr GetInitGraph(DfGraphConvertorPtr converter) {
  MS_EXCEPTION_IF_NULL(converter);
  return converter->GetInitGraph();
}
DfGraphPtr GetSaveCheckpointGraph(DfGraphConvertorPtr converter) {
  MS_EXCEPTION_IF_NULL(converter);
  return converter->GetSaveCheckpointGraph();
}
DfGraphPtr GetBroadcastGraph(DfGraphConvertorPtr converter) {
  MS_EXCEPTION_IF_NULL(converter);
  return converter->GetBroadcastGraph();
}

std::shared_ptr<ge::Session> NewSession(const SessionOptions &sess_options) {
  return transform::GraphRunner::NewSession(sess_options);
}

Status RunGraph(const std::shared_ptr<transform::GraphRunner> &runner, const RunOptions &options,
                const std::vector<GeTensorPtr> &inputs, std::vector<GeTensorPtr> *outputs) {
  MS_EXCEPTION_IF_NULL(runner);
  return runner->RunGraph(options, inputs, outputs);
}

Status RunGraphAsync(const std::shared_ptr<GraphRunner> &runner, const RunOptions &options,
                     const std::vector<GeTensorPtr> &inputs, std::vector<GeTensorPtr> *outputs) {
  MS_EXCEPTION_IF_NULL(runner);
  return runner->RunGraphAsync(options, inputs, outputs);
}

void ClearOpAdapterMap() { transform::OpAdapterMap::get().clear(); }

transform::Status CompileDatasetGraph(const DatasetGraphParam &param, const std::string &phase) {
  return BuildDatasetGraph(param, phase);
}
}  // namespace transform
}  // namespace mindspore
