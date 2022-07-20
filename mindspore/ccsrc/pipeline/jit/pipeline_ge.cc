/**
 * Copyright 2020-2022 Huawei Technologies Co., Ltd
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

#include "pipeline/jit/pipeline_ge.h"

#include <sstream>
#include <fstream>
#include <map>
#include <cstdlib>
#include <algorithm>

#include "utils/hash_map.h"
#include "include/common/debug/anf_ir_dump.h"
#include "ir/tensor.h"
#include "include/transform/graph_ir/utils.h"
#include "graph/model.h"
#include "include/common/debug/draw.h"
#include "abstract/abstract_value.h"
#include "include/common/utils/convert_utils_py.h"
#include "include/common/utils/utils.h"
#include "include/common/utils/python_adapter.h"
#include "runtime/hardware/device_context_manager.h"

namespace mindspore {
namespace pipeline {
using Tensor = mindspore::tensor::Tensor;
using MetaTensor = mindspore::tensor::MetaTensor;
using TensorOrderMap = std::map<std::string, std::shared_ptr<Tensor>>;
using mindspore::abstract::AbstractScalar;
using mindspore::abstract::AbstractTensor;
using mindspore::abstract::AbstractTuple;
using mindspore::abstract::AbstractTuplePtr;
using mindspore::transform::GeTensorPtr;
using mindspore::transform::MeTensorPtr;
using mindspore::transform::Status;

void DoExecNonInputGraph(const std::string &phase) {
  std::vector<GeTensorPtr> ge_tensors;
  std::vector<GeTensorPtr> ge_outputs;
  transform::RunOptions run_options;
  run_options.name = phase;
  auto graph_runner = transform::GetGraphRunner();
  if (graph_runner == nullptr) {
    MS_LOG(ERROR) << "Can not found GraphRunner";
    return;
  }

  {
    // Release GIL before calling into (potentially long-running) C++ code
    py::gil_scoped_release release;
    Status ret = transform::RunGraph(graph_runner, run_options, ge_tensors, &ge_outputs);
    if (ret != Status::SUCCESS) {
      MS_LOG(ERROR) << "Exec graph:" << run_options.name << " failed";
      return;
    }
  }
}

void CreateSessionAndGraphRunner(bool is_training = true) {
  std::shared_ptr<ge::Session> sess = transform::GetGeSession();
  if (sess == nullptr) {
    transform::SessionOptions options;
    if (is_training) {
      options["ge.trainFlag"] = "1";
      options["ge.streamNum"] = "100";
      options["ge.enabledLocalFmkop"] = "1";
      options["ge.hcomParallel"] = "1";
    } else {
      options["ge.trainFlag"] = "0";
    }

    options["ge.enablePrintOpPass"] = "0";
    auto context_ptr = MsContext::GetInstance();
    MS_EXCEPTION_IF_NULL(context_ptr);
    auto auto_tune_mode = context_ptr->get_param<std::string>(MS_CTX_TUNE_MODE);
    if (!auto_tune_mode.empty() && auto_tune_mode != "NO_TUNE") {
      options["ge.autoTuneMode"] = auto_tune_mode;
      MS_LOG(INFO) << "ge.autoTuneMode has set to " << auto_tune_mode;
    }
    sess = transform::NewSession(options);
    transform::SetGeSession(sess);
  }

  transform::GraphRunnerOptions options;
  options.sess_ptr = sess;
  auto graph_runner = transform::NewGraphRunner(options);
  transform::SetGraphRunner(graph_runner);
}

bool InitExecDatasetGe(const std::string &queue_name, int64_t size, int64_t batch_size,
                       const std::vector<TypePtr> &types, const std::vector<std::vector<int64_t>> &shapes,
                       const std::vector<int64_t> &input_indexes, const std::string &phase) {
  auto context_ptr = MsContext::GetInstance();
  MS_EXCEPTION_IF_NULL(context_ptr);
  const auto &device_context = device::DeviceContextManager::GetInstance().GetOrCreateDeviceContext(
    {context_ptr->get_param<std::string>(MS_CTX_DEVICE_TARGET), context_ptr->get_param<uint32_t>(MS_CTX_DEVICE_ID)});
  device_context->Initialize();
  std::vector<int64_t> ge_types;
  (void)std::transform(types.begin(), types.end(), std::back_inserter(ge_types),
                       [](const TypePtr &i) -> int64_t { return transform::ConvertDataType(i->type_id()); });

  ConfigManager::GetInstance().set_dataset_mode(DatasetMode::DS_SINK_MODE);
  ConfigManager::GetInstance().set_iter_num(queue_name, size);
  ConfigManager::GetInstance().set_dataset_phase(phase);

  DatasetGraphParam param(queue_name, size, batch_size, ge_types, shapes, input_indexes);
  ConfigManager::GetInstance().set_dataset_param(param);

  if (transform::CompileDatasetGraph(param, phase) != transform::SUCCESS) {
    MS_LOG(ERROR) << "Build dateset graph failed.";
    return false;
  }

  auto env_ge = common::GetEnv("MS_ENABLE_GE");
  auto env_training = common::GetEnv("MS_GE_TRAIN");
  bool training = false;
  if (env_ge == "1" && env_training == "1") {
    training = true;
  }
  if (training) {
    (void)setenv("GE_TRAIN", "1", 1);
  } else {
    (void)setenv("GE_TRAIN", "0", 1);
  }

  CreateSessionAndGraphRunner(training);

  MS_LOG(INFO) << "DoExecNonInputGraph:" << phase;
  DoExecNonInputGraph(phase);

  return true;
}

void ConvertObjectToTensors(const py::dict &dict, TensorOrderMap *const tensors) {
  for (auto item : dict) {
    if ((!py::isinstance<py::str>(item.first))) {
      MS_LOG(WARNING) << "Type of key of py_dict is not string, ignore it.";
      continue;
    }
    std::shared_ptr<Tensor> tensor;
    std::string name = py::cast<std::string>(item.first);
    if (py::isinstance<py::float_>(item.second.attr("data"))) {
      // convert float to tensor with shape([1])
      tensor = std::make_shared<Tensor>(kNumberTypeFloat32, std::vector<int64_t>({1}));
      *(static_cast<float *>(tensor->data_c())) = py::cast<float>(item.second.attr("data"));
    } else if (py::isinstance<py::int_>(item.second.attr("data"))) {
      // convert int64_t to tensor with shape([1])
      tensor = std::make_shared<Tensor>(kNumberTypeInt32, std::vector<int64_t>({1}));
      *(static_cast<float *>(tensor->data_c())) = py::cast<float>(item.second.attr("data"));
    } else if (py::isinstance<Tensor>(item.second.attr("data"))) {
      // cast tensor
      tensor = py::cast<std::shared_ptr<Tensor>>(item.second.attr("data"));
    }

    if (tensor == nullptr) {
      MS_LOG(EXCEPTION) << "Get default value for " << name << " failed";
    }
    (void)tensors->emplace(name, tensor);
  }
}

bool AddDFGraph(const std::map<std::string, ExecutorInfoPtr> &info, const py::dict &init_params,
                const std::string &phase, const py::object &broadcast_params) {
  FuncGraphPtr anf_graph = info.at(phase)->func_graph;
  auto converter = transform::NewConverter(anf_graph);

  size_t pos = phase.find('.');
  std::string net_id = ((pos == std::string::npos || pos == phase.size() - 1) ? phase : phase.substr(pos + 1));
  std::string phase_prefix = phase.substr(0, pos);
  if (phase_prefix == "export") {
    MS_LOG(INFO) << "Set DfGraphConvertor training : false";
    transform::SetTraining(converter, false);
  }

  TensorOrderMap init_tensors{};
  ConvertObjectToTensors(init_params, &init_tensors);
  transform::BuildGraph(converter, init_tensors);
  if (!broadcast_params.is_none()) {
    if (!py::isinstance<py::dict>(broadcast_params)) {
      MS_LOG(ERROR) << "Invalid broadcast params, it must be py::dict type";
      return false;
    }
    py::dict broadcast = broadcast_params.cast<py::dict>();
    if (broadcast.empty()) {
      transform::GenerateBroadcastGraph(converter, init_tensors);
    } else {
      TensorOrderMap broadcast_tensors{};
      ConvertObjectToTensors(broadcast, &broadcast_tensors);
      transform::GenerateBroadcastGraph(converter, broadcast_tensors);
    }
    MS_LOG(INFO) << "Generate broadcast graph with params and broadcast_empty is " << broadcast.empty();
  }
  transform::GenerateCheckpointGraph(converter);
  auto err_code = transform::ErrCode(converter);
  if (err_code != 0) {
    transform::ClearGraph();
    MS_LOG(ERROR) << "Convert df graph failed, err:" << err_code;
    return false;
  }
#ifdef ENABLE_DUMP_IR
  if (MsContext::GetInstance()->get_param<bool>(MS_CTX_SAVE_GRAPHS_FLAG)) {
    // for debug
    transform::DrawComputeGraph(converter, GetSaveGraphsPathName("ge_graph.dot"));
    // for debug
    transform::DrawInitGraph(converter, GetSaveGraphsPathName("init_graph.dot"));
    // for debug
    transform::DrawSaveCheckpointGraph(converter, GetSaveGraphsPathName("save_checkpoint_graph.dot"));
  }
#endif
  std::string init_graph = "init_subgraph." + net_id;
  std::string checkpoint_name = "save." + net_id;
  if (phase.find("train") != std::string::npos) {
    (void)transform::AddGraph(phase, transform::GetComputeGraph(converter), {}, {{"ge.exec.variable_acc", "1"}});
  } else {
    (void)transform::AddGraph(phase, transform::GetComputeGraph(converter));
  }

  (void)transform::AddGraph(init_graph, transform::GetInitGraph(converter));
  (void)transform::AddGraph(BROADCAST_GRAPH_NAME, transform::GetBroadcastGraph(converter));

  Status ret = transform::AddGraph(checkpoint_name, transform::GetSaveCheckpointGraph(converter));
  if (ret == Status::SUCCESS) {
    transform::SetAnfGraph(checkpoint_name, anf_graph);
  }

  return true;
}

FuncGraphPtr BuildDFGraph(const std::map<std::string, ExecutorInfoPtr> &info, const py::dict &init_params,
                          const std::string &phase, const py::object &broadcast_params) {
  if (info.count(phase) == 0) {
    MS_LOG(EXCEPTION) << "No phase in executor:" << GetPhasePrefix(phase);
  }
  FuncGraphPtr anf_graph = info.at(phase)->func_graph;
#ifdef ENABLE_DUMP_IR
  if (MsContext::GetInstance()->get_param<bool>(MS_CTX_SAVE_GRAPHS_FLAG)) {
    draw::Draw("anf_graph.dot", anf_graph);  // for debug
    DumpIR("anf_graph.ir", anf_graph, true);
  }
#endif

  if (!AddDFGraph(info, init_params, phase, broadcast_params)) {
    MS_LOG(ERROR) << "GenConvertor failed";
    return nullptr;
  }

  auto env_ge = common::GetEnv("MS_ENABLE_GE");
  auto env_training = common::GetEnv("MS_GE_TRAIN");
  bool training = false;
  if (env_ge == "1" && env_training == "1") {
    training = true;
  }
  if (training) {
    (void)setenv("GE_TRAIN", "1", 1);
  } else {
    (void)setenv("GE_TRAIN", "0", 1);
  }

  CreateSessionAndGraphRunner(training);

  return anf_graph;
}

py::object ExtractGeneralCnodeRet(const AbstractBasePtr &cnode_data, const py::tuple &data, size_t *count) {
  MS_EXCEPTION_IF_NULL(cnode_data);

  if (cnode_data->isa<AbstractTensor>()) {
    if (*count >= data.size()) {
      MS_LOG(EXCEPTION) << "The number of elements in the outputs : " << data.size()
                        << " less than the number of elements required. ";
    }

    BaseShapePtr shape = cnode_data->BuildShape();
    if (!shape->isa<abstract::Shape>()) {
      MS_LOG(EXCEPTION) << "The shape of the tensor derived is not Shape, is " << shape->ToString();
    }

    auto shape_me = shape->cast<abstract::ShapePtr>()->shape();
    auto shape_ge = py::cast<Tensor &>(data[*count]).shape();
    if (shape_ge != shape_me) {  // dynamic shape
      MS_LOG(WARNING) << "The shape of the " << *count << "th tensor returned: " << shape_ge
                      << " is not the same as the shape of the tensor derived: " << shape_me;
    }

    return data[(*count)++];
  }

  if (cnode_data->isa<AbstractScalar>()) {
    return data[(*count)++];
  }

  if (!cnode_data->isa<AbstractTuple>()) {
    MS_LOG(EXCEPTION) << "The output of operator in the final anf graph could "
                      << "only be a tensor or a tuple of tensor, but got " << cnode_data->BuildValue()->ToString()
                      << ".";
  }
  auto data_tp = cnode_data->cast<AbstractTuplePtr>();
  auto elements = data_tp->elements();
  size_t size = data_tp->size();
  auto tp = py::tuple(size);
  for (size_t i = 0; i < size; i++) {
    tp[i] = ExtractGeneralCnodeRet(elements[i], data, count);
  }
  return tp;
}

py::object StructureOutput(const AnfNodePtr &output_node, const py::tuple &data, size_t *count) {
  MS_EXCEPTION_IF_NULL(output_node);

  if (output_node->isa<ValueNode>()) {
    return ValueToPyData(GetValueNode(output_node));
  }

  if (output_node->isa<Parameter>()) {
    if (*count >= data.size()) {
      MS_LOG(EXCEPTION) << "The number of elements in the outputs : " << data.size()
                        << " less than the number of elements required. ";
    }
    return data[(*count)++];
  }

  auto output_c = output_node->cast<CNodePtr>();
  if (output_c == nullptr) {
    MS_LOG(EXCEPTION) << "The final anf graph could only have constant, parameter, and operator, but got "
                      << output_node->ToString();
  }

  if (output_c->IsApply(prim::kPrimMakeTuple)) {
    auto input_list = output_c->inputs();
    size_t size = input_list.size();
    auto tp = py::tuple(size - 1);
    for (size_t i = 1; i < size; i++) {
      tp[i - 1] = StructureOutput(input_list[i], data, count);
    }
    return tp;
  }
  if (output_c->IsApply(prim::kPrimDepend)) {
    return StructureOutput(output_c->input(1), data, count);
  }

  return ExtractGeneralCnodeRet(output_c->abstract(), data, count);
}

void GetMeRetDataType(const AbstractBasePtr &cnode_data, std::vector<TypeId> *me_types) {
  MS_EXCEPTION_IF_NULL(cnode_data);

  if (cnode_data->isa<AbstractTensor>()) {
    TypeId me_type = cnode_data->BuildType()->type_id();
    if (me_type == kObjectTypeTensorType) {
      me_type = dyn_cast<TensorType>(cnode_data->BuildType())->element()->type_id();
      (void)me_types->emplace_back(me_type);
    }
    return;
  }
  if (cnode_data->isa<AbstractScalar>()) {
    TypeId me_type = cnode_data->BuildType()->type_id();
    me_types->emplace_back(me_type);
    return;
  }
  auto abstract_tuple = cnode_data->cast<AbstractTuplePtr>();
  MS_EXCEPTION_IF_NULL(abstract_tuple);
  auto elements = abstract_tuple->elements();
  for (size_t i = 0; i < abstract_tuple->size(); ++i) {
    GetMeRetDataType(elements[i], me_types);
  }
}

void ExportDFGraph(const std::string &file_name, const std::string &phase, const py::object encrypt, char *key) {
  MS_LOG(DEBUG) << "Export graph begin.";
  transform::DfGraphWrapperPtr wrap_ptr = transform::GetGraphByName(phase);
  if (wrap_ptr == nullptr) {
    MS_LOG(ERROR) << "Get graph form DfGraphManager failed!";
    return;
  }

  transform::DfGraphPtr ge_graph = wrap_ptr->graph_ptr_;
  if (ge_graph == nullptr) {
    MS_LOG(ERROR) << "Graph is null!";
    return;
  }
  if (key != nullptr) {
    if (py::isinstance<py::none()>(encrypt)) {
      MS_LOG(ERROR) << "ERROR: encrypt is not a function";
      return;
    }
    // get model stream
    ge::Model model("", "");
    model.SetGraph(*ge_graph);
    ge::Buffer model_data;
    auto ge_ret = model.Save(model_data);
    if (ge_ret != ge::SUCCESS) {
      MS_LOG(ERROR) << "ERROR: GE model save fail";
      return;
    }
    // convert model and key into py::bytes
    const std::string str(reinterpret_cast<char *>(model_data.GetData()), model_data.GetSize());
    py::bytes model_bytes(str);
    py::bytes key_bytes(key);

    // call python encrypt func
    py::bytes encrypted_model_stream = encrypt(model_bytes, key_bytes);
    if (encrypted_model_stream == py::none()) {
      MS_LOG(ERROR) << "ERROR: Model encrypt fail";
      return;
    }
    // save to file
    std::ofstream ofs(file_name);
    if (!ofs.is_open()) {
      MS_LOG(ERROR) << "ERROR: Open File '" << file_name << "' failed!";
      return;
    }
    ofs << std::string(encrypted_model_stream);
    ofs.close();
  } else {
    if (ge_graph->SaveToFile(file_name) != 0) {
      MS_LOG(EXCEPTION) << "Export air model failed.";
    }
  }
  MS_LOG(INFO) << "Export air model finish.";
}
}  // namespace pipeline
}  // namespace mindspore
