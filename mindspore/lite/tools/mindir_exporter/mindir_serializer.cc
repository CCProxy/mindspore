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

#include "tools/mindir_exporter/mindir_serializer.h"
#include <sys/stat.h>
#include <dirent.h>
#include <fstream>
#include <set>
#include <algorithm>
#include "mindspore/ccsrc/include/common/debug/dump_proto.h"
#include "mindspore/ccsrc/include/common/utils/utils.h"
#include "src/common/file_utils.h"
#include "src/common/common.h"
#include "tools/converter/parser/parser_utils.h"
#include "mindspore/core/utils/file_utils.h"
#include "mindspore/core/ir/quantization_param.h"
#include "mindspore/lite/tools/converter/quantizer/quant_param_holder.h"
#include "mindspore/lite/tools/converter/quantizer/quant_params.h"
#include "mindspore/lite/tools/converter/quantizer/quantize_util.h"

namespace mindspore::lite {
// unit is byte. model size more than 1G need split.
constexpr const size_t TOTAL_SAVE = 1024 * 1024 * 1024;
constexpr const size_t PARA_ROUND = 1024;
constexpr const int64_t OFFSET = 64;

namespace {
bool DeleteDirRecursively(const std::string &dir_name) {
  DIR *dir = opendir(dir_name.c_str());
  dirent *dirent = nullptr;
  std::vector<std::string> file_names{};
  while ((dirent = readdir(dir)) != 0) {
    if (strcmp(dirent->d_name, ".") != 0 && strcmp(dirent->d_name, "..") != 0) {
      file_names.emplace_back(dirent->d_name);
    }
  }
  for (auto &file_name : file_names) {
    auto file_path = dir_name + "/" + file_name;
    auto real_file_path = RealPath(file_path.c_str());
    auto result = unlink(real_file_path.c_str());
    if (result != 0) {
      closedir(dir);
      MS_LOG(ERROR) << "Delete the file(" << real_file_path << ") failed." << ErrnoToString(errno);
      return false;
    }
  }
  closedir(dir);
  return true;
}
}  // namespace

int MindIRSerializer::RemoveQuantParameterHolder(FuncGraphPtr func_graph) {
  std::set<FuncGraphPtr> all_func_graphs = {};
  GetAllFuncGraph(func_graph, &all_func_graphs);
  for (auto &graph : all_func_graphs) {
    auto node_list = TopoSort(graph->get_return());
    for (auto &node : node_list) {
      if (!utils::isa<CNodePtr>(node)) {
        continue;
      }
      auto cnode = node->cast<CNodePtr>();
      if (cnode->inputs().empty() || cnode->input(0) == nullptr) {
        MS_LOG(ERROR) << "the cnode is invalid.";
        return lite::RET_NULL_PTR;
      }
      if (utils::isa<CNodePtr>(cnode->input(0))) {
        MS_LOG(DEBUG) << "call cnode no need to convert primitive.";
        return lite::RET_NO_CHANGE;
      }
      auto value_node = cnode->input(0)->cast<ValueNodePtr>();
      if (value_node == nullptr || value_node->value() == nullptr) {
        MS_LOG(ERROR) << "value node is invalid.";
        return lite::RET_NULL_PTR;
      }
      auto primitive = value_node->value()->cast<PrimitivePtr>();
      if (primitive == nullptr) {
        if (utils::isa<FuncGraphPtr>(value_node->value())) {
          MS_LOG(DEBUG) << "is a funcgraph.";
          return lite::RET_NO_CHANGE;
        } else {
          MS_LOG(ERROR) << "the value is not primitive.";
          return lite::RET_ERROR;
        }
      }
      primitive->EraseAttr("quant_params");
    }
  }
  return RET_OK;
}

int MindIRSerializer::UpdateParamCount(const FuncGraphPtr &func_graph) {
  auto fv_count = 0;
  std::vector<AnfNodePtr> params;
  std::vector<AnfNodePtr> reorder_param;
  reorder_param.reserve(func_graph->parameters().size());
  for (const auto &node : func_graph->parameters()) {
    auto param_node = node->cast<ParameterPtr>();
    if (param_node == nullptr) {
      MS_LOG(ERROR) << "The parameters() in func graph should be all Parameter Node. but got " << node->DebugString();
      return RET_ERROR;
    }
    if (param_node->has_default()) {
      (void)params.emplace_back(param_node);
      ++fv_count;
      continue;
    }
    (void)reorder_param.emplace_back(param_node);
  }
  std::copy(params.begin(), params.end(), std::back_inserter(reorder_param));
  func_graph->set_parameters(reorder_param);
  func_graph->set_fv_param_count(fv_count);
  return RET_OK;
}

int MindIRSerializer::PreProcSaveTogether(const FuncGraphPtr &func_graph) {
  if (func_graph == nullptr) {
    MS_LOG(ERROR) << "func_graph is nullptr.";
    return RET_ERROR;
  }

  auto ret = UpdateParamCount(func_graph);
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "Update parameter count failed.";
    return ret;
  }

  ret = ConvertQuantHolderToQuantizationParam(func_graph);
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "add quant parameter holder failed.";
    return ret;
  }

  ret = RemoveQuantParameterHolder(func_graph);
  if (ret != RET_OK && ret != RET_NO_CHANGE) {
    MS_LOG(ERROR) << "remove quant parameter holder failed.";
    return ret;
  }

  // Parse func_graph as model proto
  std::string proto_string = GetBinaryProtoString(func_graph);
  if (proto_string.empty()) {
    MS_LOG(ERROR) << "parse proto string failed.";
    return RET_ERROR;
  }

  if (!model_proto_.ParseFromString(proto_string)) {
    MS_LOG(ERROR) << "parse model proto from string failed.";
    return RET_ERROR;
  }

  ret = ParamDict(func_graph);
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "parse param form funcgraph failed.";
    return ret;
  }

  ret = IfSaveTogether(&save_together_);
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "error occur when check condition of saving together.";
    return ret;
  }

  return RET_OK;
}

int MindIRSerializer::Save(const std::shared_ptr<ConverterPara> &param, const FuncGraphPtr &func_graph) {
  if (func_graph == nullptr) {
    MS_LOG(ERROR) << "func_graph is nullptr.";
    return RET_NULL_PTR;
  }
  if (param == nullptr) {
    MS_LOG(ERROR) << "param is nullptr.";
    return RET_NULL_PTR;
  }
  auto output_file = param->output_file;
  auto ret = ParserPath(output_file);
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "parse path failed.";
    return ret;
  }

  // Serialize to protobuf using unique parameter name label.
  common::SetEnv("MS_DEV_TRACE_LABEL_WITH_UNIQUE_ID", "1", 0);

  // Do preprocess on func_graph and check conditions for saving together.
  ret = PreProcSaveTogether(func_graph);
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "PreProcSaveTogether failed";
    return ret;
  }

  if (save_together_) {
    ret = SaveMindIRTogether();
  } else {
    ret = SplitSave();
  }
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "save mindir weight failed.";
    return ret;
  }
  return RET_OK;
}

int MindIRSerializer::ConvertQuantHolderToQuantizationParam(const FuncGraphPtr &func_graph) {
  std::set<FuncGraphPtr> all_func_graphs = {};
  GetAllFuncGraph(func_graph, &all_func_graphs);
  for (auto &graph : all_func_graphs) {
    auto node_list = TopoSort(graph->get_return());
    for (auto &node : node_list) {
      if (!utils::isa<CNodePtr>(node)) {
        continue;
      }
      auto cnode = node->cast<CNodePtr>();
      if (cnode->inputs().empty() || cnode->input(0) == nullptr) {
        MS_LOG(ERROR) << "the cnode is invalid.";
        return lite::RET_NULL_PTR;
      }
      auto primitive = GetValueNode<std::shared_ptr<Primitive>>(cnode->input(0));
      if (primitive == nullptr) {
        MS_LOG(DEBUG) << cnode->fullname_with_scope() << " : primitive is nullptr";
        return RET_OK;
      }
      auto quant_params_holder = GetCNodeQuantHolder(primitive);
      if (quant_params_holder == nullptr || quant_params_holder->quant_type() == quant::QUANT_NONE) {
        continue;
      }

      auto quant_type = MakeValue(static_cast<int>(quant_params_holder->quant_type()));
      MS_CHECK_TRUE_MSG(quant_type != nullptr, RET_ERROR, "quant_type is nullptr.");

      primitive->AddAttr(quant::kQuantType, quant_type);
      auto input_quant_params = quant_params_holder->get_input_quant_params();
      for (unsigned int index = 0; index < input_quant_params.size(); index++) {
        auto input = cnode->input(index + quant::kPrimOffset);
        if (!input->isa<Parameter>()) {
          continue;
        }
        auto parameter_ptr = input->cast<ParameterPtr>();
        auto tensor = parameter_ptr->default_param()->cast<tensor::TensorPtr>();
        auto quant_cluster = quant_params_holder->GetQuantClusters(index);
        if (!quant_cluster.empty()) {
          QuantizationParam quantization(quant::kClusterQuant);
          quantization.AddAttr(quant::kClusterCentroidList, MakeValue(quant_cluster));
          tensor->set_quant_param(std::vector<std::shared_ptr<mindspore::QuantizationParam>>{
            std::make_shared<mindspore::QuantizationParam>(quantization)});
          continue;
        }
        if (quant_params_holder->CheckInit(index, true)) {
          auto quantization_ptr = ConvertQuantParamTToQuantizationParam(input_quant_params[index]);
          tensor->set_quant_param(std::vector<std::shared_ptr<mindspore::QuantizationParam>>{quantization_ptr});
        }
      }

      auto output_quant_params = quant_params_holder->get_output_quant_params();
      for (unsigned int index = 0; index < output_quant_params.size(); index++) {
        if (quant_params_holder->CheckInit(index, false)) {
          auto quantization_ptr = ConvertQuantParamTToQuantizationParam(output_quant_params[index]);
          primitive->AddAttr(quant::kQuantParam, quantization_ptr);
        }
      }
    }
  }
  return RET_OK;
}

std::shared_ptr<mindspore::QuantizationParam> MindIRSerializer::ConvertQuantParamTToQuantizationParam(
  std::vector<schema::QuantParamT> quant_param) {
  QuantizationParam quantization(quant::kLinearQuant);
  std::vector<ValuePtr> scale_list;
  std::vector<ValuePtr> zeroPoint_list;
  std::vector<ValuePtr> min_list;
  std::vector<ValuePtr> max_list;
  std::vector<ValuePtr> varCorr_list;
  std::vector<ValuePtr> meanCorr_list;
  std::vector<ValuePtr> numBits_list;
  std::vector<ValuePtr> narrowRange_list;
  for (auto quantparamt : quant_param) {
    scale_list.push_back(MakeValue(quantparamt.scale));
    zeroPoint_list.push_back(MakeValue(quantparamt.zeroPoint));
    min_list.push_back(MakeValue(quantparamt.min));
    max_list.push_back(MakeValue(quantparamt.max));
    varCorr_list.push_back(MakeValue(quantparamt.varCorr));
    meanCorr_list.push_back(MakeValue(quantparamt.meanCorr));
    numBits_list.push_back(MakeValue(quantparamt.numBits));
    narrowRange_list.push_back(MakeValue(quantparamt.narrowRange));
  }
  quantization.AddAttr(quant::kScaleList, std::make_shared<ValueList>(scale_list));
  quantization.AddAttr(quant::kZeroPointList, std::make_shared<ValueList>(zeroPoint_list));
  quantization.AddAttr(quant::kMinList, std::make_shared<ValueList>(min_list));
  quantization.AddAttr(quant::kMaxList, std::make_shared<ValueList>(max_list));
  quantization.AddAttr(quant::kVarCorrList, std::make_shared<ValueList>(varCorr_list));
  quantization.AddAttr(quant::kMeanCorrList, std::make_shared<ValueList>(meanCorr_list));
  quantization.AddAttr(quant::kNumBitList, std::make_shared<ValueList>(numBits_list));
  quantization.AddAttr(quant::kNarrowRangeList, std::make_shared<ValueList>(narrowRange_list));
  return std::make_shared<mindspore::QuantizationParam>(quantization);
}

int MindIRSerializer::SaveMindIRTogether() {
  for (auto &param_proto : *(model_proto_.mutable_graph()->mutable_parameter())) {
    std::string proto_name = param_proto.name();
    auto para = GetFgParaAccordingToProtoName(proto_name);
    if (para == nullptr) {
      return RET_ERROR;
    }
    if (!para->has_default()) {
      continue;
    }
    auto data = para->default_param()->cast<tensor::TensorPtr>();
    param_proto.clear_raw_data();
    param_proto.set_raw_data(data->data_c(), static_cast<size_t>(data->data().nbytes()));
  }

  return SaveProtoToFile(&model_proto_, save_model_path_);
}

int MindIRSerializer::CreateParameterDir() {
#ifdef _WIN32
  dir_name_ = save_path_ + "\\" + model_name_ + "_variables";
#else
  dir_name_ = save_path_ + "/" + model_name_ + "_variables";
#endif
  fs_ = system::Env::GetFileSystem();
  if (fs_ == nullptr) {
    MS_LOG(ERROR) << "create file system failed.";
    return RET_NULL_PTR;
  }

  if (fs_->FileExist(dir_name_)) {
    if (!DeleteDirRecursively(dir_name_)) {
      return RET_ERROR;
    }
  }

  if (!fs_->CreateDir(dir_name_)) {
    MS_LOG(ERROR) << "create dir failed.";
    return RET_ERROR;
  }

  ChangeFileMode(dir_name_, S_IWUSR | S_IRUSR | S_IXUSR);
  return RET_OK;
}

std::shared_ptr<Parameter> MindIRSerializer::GetFgParaAccordingToProtoName(const std::string &proto_name) {
  auto beg_pos = proto_name.find_first_of(':') + 1;
  if (beg_pos >= proto_name.size()) {
    MS_LOG(ERROR) << "begin pos exceed proto name length.";
    return nullptr;
  }
  auto name = proto_name.substr(beg_pos);
  if (param_dict_.find(name) == param_dict_.end()) {
    MS_LOG(ERROR) << "param proto name: " << name << " is not in param dict.";
    return nullptr;
  }
  return param_dict_.at(name);
}

int MindIRSerializer::ChangeParaDataFile(const std::string &file) {
  auto real_path = CreateExternalPath(file);
  if (fs_->FileExist(real_path)) {
    if (!fs_->DeleteFile(real_path)) {
      MS_LOG(ERROR) << "delete file failed.";
      return RET_ERROR;
    }
  }
  ChangeFileMode(real_path, S_IWUSR);
  data_fs_ = OpenFile(real_path, std::ios::app);
  if (data_fs_ == nullptr) {
    MS_LOG(ERROR) << "data_fs_ is nullptr.";
    return RET_NULL_PTR;
  }
  char front_info[OFFSET]{0};
  front_info[0] = IsSystemLittleEndidan();
  (void)data_fs_->write(front_info, OFFSET);
  return RET_OK;
}

bool MindIRSerializer::IsSystemLittleEndidan() const {
  int check = 0x01;
  auto address = reinterpret_cast<char *>(&check);
  return *address == 0x01;
}

int MindIRSerializer::GetDataFile(const std::string &data_file_name, std::ofstream *fout, int64_t *, int64_t *offset) {
  if (offset == nullptr) {
    MS_LOG(ERROR) << "offset is nullptr.";
    return RET_NULL_PTR;
  }
  *offset = OFFSET;
  std::shared_ptr<system::FileSystem> fs = system::Env::GetFileSystem();
  if (fs == nullptr) {
    MS_LOG(ERROR) << "create file system failed.";
    return RET_NULL_PTR;
  }
  if (fs->FileExist(data_file_name)) {
    ChangeFileMode(data_file_name, S_IWUSR);
  }

  std::byte place_holder[OFFSET];
  fout = new std::ofstream;
  (void)fout->write(reinterpret_cast<const char *>(place_holder), *offset);

  return RET_OK;
}

std::string MindIRSerializer::CreateExternalPath(const std::string &external_file) {
  dir_path_ = RealPath(dir_name_.c_str());
  std::string external_local_path{};
#ifdef _WIN32
  external_local_path = dir_path_ + "\\" + external_file;
#else
  external_local_path = dir_path_ + "/" + external_file;
#endif
  return external_local_path;
}

int MindIRSerializer::SplitSave() {
  MS_LOG(DEBUG) << "Parameters in the net capacity exceeds 1G, save MindIR model and parameters separately.";
  int ret = CreateParameterDir();
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "create parameter dir failed.";
    return RET_ERROR;
  }

  int index = 0;
  std::string external_local = "data_" + std::to_string(index);
  auto external_local_path = CreateExternalPath(external_local);
  if (fs_->FileExist(external_local_path)) {
    if (!fs_->DeleteFile(external_local_path)) {
      MS_LOG(ERROR) << "delete file failed.";
      return RET_ERROR;
    }
  }
  int64_t parameter_size = 0;
  int64_t offset = OFFSET;

  data_fs_ = OpenFile(external_local_path, std::ios::out | std::ios::binary | std::ios::trunc);
  if (data_fs_ == nullptr) {
    MS_LOG(ERROR) << "Open " << external_local_path << " failed";
    return RET_ERROR;
  }
  ret = ChangeParaDataFile(external_local);
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "change parameter data file failed.";
    return ret;
  }

  for (auto &param_proto : *(model_proto_.mutable_graph()->mutable_parameter())) {
    std::string proto_name = param_proto.name();
    auto para = GetFgParaAccordingToProtoName(proto_name);
    if (para == nullptr) {
      return RET_ERROR;
    }
    if (!para->has_default()) {
      continue;
    }
    auto data = para->default_param()->cast<tensor::TensorPtr>();
    int64_t data_length = static_cast<int64_t>(data->data().nbytes());
    int64_t append_size = 0;
    if (data_length % OFFSET != 0) {
      append_size = OFFSET - (data_length % OFFSET);
    }
    parameter_size += ((append_size + data_length) / PARA_ROUND);
    if (parameter_size > static_cast<int64_t>(TOTAL_SAVE)) {
      index++;
      external_local = "data_" + std::to_string(index);
      data_fs_->close();
      delete data_fs_;
      ret = ChangeParaDataFile(external_local);
      if (ret != RET_OK) {
        MS_LOG(ERROR) << "change parameter data file failed.";
        return ret;
      }
      parameter_size = OFFSET / PARA_ROUND;
    }
    std::string external_local_data = model_name_ + "_variables/" + external_local;
    *(param_proto.mutable_external_data()->mutable_location()) = external_local_data;
    param_proto.mutable_external_data()->set_length(data_length);
    param_proto.mutable_external_data()->set_offset(offset);
    data_fs_->write(static_cast<const char *>(data->data_c()), data_length);
    auto append_data = new char[append_size];
    if (append_data == nullptr) {
      return RET_NULL_PTR;
    }
    data_fs_->write(append_data, append_size);
    offset += (data_length + append_size);
    delete[] append_data;
  }
  std::string split_model_file_name = "";
#ifdef _WIN32
  split_model_file_name = save_path_ + "\\" + model_name_ + "_graph.mindir";
#else
  split_model_file_name = save_path_ + "/" + model_name_ + "_graph.mindir";
#endif
  return SaveProtoToFile(&model_proto_, split_model_file_name);
}

int MindIRSerializer::ParserPath(const std::string &output_path) {
  if (!ParserPathAndModelName(output_path, &save_path_, &model_name_)) {
    MS_LOG(ERROR) << "parser save path and model name from output_path failed.";
    return RET_ERROR;
  }
#ifdef _WIN32
  save_model_path_ = save_path_ + "\\" + model_name_ + ".mindir";
#else
  save_model_path_ = save_path_ + "/" + model_name_ + ".mindir";
#endif
  return RET_OK;
}

int MindIRSerializer::ParamDict(const FuncGraphPtr &func_graph) {
  std::set<FuncGraphPtr> all_func_graphs = {};
  GetAllFuncGraph(func_graph, &all_func_graphs);
  for (auto &fg : all_func_graphs) {
    for (auto &para : fg->parameters()) {
      if (!para->isa<Parameter>()) {
        MS_LOG(ERROR) << "fg parameters contains non-parameter type node.";
        return RET_ERROR;
      }
      auto para_node = para->cast<ParameterPtr>();
      param_dict_[para->ToString()] = para_node;
    }
  }
  return RET_OK;
}

int MindIRSerializer::IfSaveTogether(bool *save_together) {
  size_t data_total = model_proto_.ByteSizeLong();
  for (auto &param_proto : model_proto_.graph().parameter()) {
    std::string proto_name = param_proto.name();
    auto para = GetFgParaAccordingToProtoName(proto_name);
    if (para == nullptr) {
      return RET_ERROR;
    }
    if (!para->has_default()) {
      continue;
    }
    auto tensor = std::dynamic_pointer_cast<tensor::Tensor>(para->default_param());
    if (tensor == nullptr) {
      MS_LOG(ERROR) << "param node default_param is not tensor.";
      return RET_ERROR;
    }
    data_total += tensor->Size();
  }
  if (data_total > TOTAL_SAVE) {
    *save_together = false;
  } else {
    *save_together = true;
  }
  return RET_OK;
}

int MindIRSerializer::SaveProtoToFile(mind_ir::ModelProto *model_proto, const std::string &output_file) {
  auto realpath = Common::CreatePrefixPath(output_file, true);
  if (!realpath.has_value()) {
    MS_LOG(ERROR) << "Get real path of file " << output_file << " failed.";
    return RET_ERROR;
  }

  ChangeFileMode(realpath.value(), S_IWUSR);
  std::ofstream fout(realpath.value());
  if (!fout.is_open()) {
    MS_LOG(ERROR) << "Open the file '" << realpath.value() << "' failed!" << ErrnoToString(errno);
    return RET_ERROR;
  }

  if (!model_proto->SerializeToOstream(&fout)) {
    MS_LOG(ERROR) << "Failed to write the mindir proto to file " << realpath.value();
    fout.close();
    return RET_ERROR;
  }
  fout.close();
  ChangeFileMode(realpath.value(), S_IRUSR);
  return RET_OK;
}

int MindIRSerializer::GetBuffAndSize(void **buff, size_t *size) {
  if (buff == nullptr || size == nullptr) {
    MS_LOG(ERROR) << "param is nullptr";
    return RET_ERROR;
  }
  *size = model_proto_.ByteSize();
  *buff = malloc(*size);
  if (*buff == nullptr) {
    MS_LOG(ERROR) << "Malloc fail";
    return RET_ERROR;
  }
  model_proto_.SerializeToArray(*buff, *size);
  return RET_OK;
}

int MindIRSerialize(const std::shared_ptr<ConverterPara> &param, const FuncGraphPtr &func_graph, bool need_buff,
                    void **buff, size_t *size) {
  mindspore::lite::MindIRSerializer serializer;
  auto ret = serializer.Save(param, func_graph);
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "MindIR serialize fail";
    return ret;
  }
  if (need_buff) {
    return serializer.GetBuffAndSize(buff, size);
  }
  return RET_OK;
}
}  // namespace mindspore::lite
