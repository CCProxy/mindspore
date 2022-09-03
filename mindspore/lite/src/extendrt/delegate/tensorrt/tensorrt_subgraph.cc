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

#include "src/extendrt/delegate/tensorrt/tensorrt_subgraph.h"
#include <cuda_runtime_api.h>
#include <string>
#include <vector>
#include <set>
#include <queue>
#include <algorithm>
#include <numeric>
#include <functional>
#include <fstream>
#include "src/extendrt/delegate/delegate_utils.h"
#include "src/common/utils.h"

#include "ops/transpose.h"
#include "ops/reshape.h"
#include "ops/strided_slice.h"
#include "ops/expand_dims.h"
#include "ops/fusion/topk_fusion.h"

namespace mindspore::lite {
TensorRTSubGraph::TensorRTSubGraph(std::vector<TensorRTOp *> ops, const std::vector<TensorInfo> &inputs,
                                   const std::vector<TensorInfo> &outputs, const mindspore::Context *ctx,
                                   std::shared_ptr<GPUDeviceInfo> device_info, TensorRTRuntime *runtime,
                                   bool support_resize, bool support_hw_resize,
                                   const std::vector<nvinfer1::Dims> &min_dims,
                                   const std::vector<nvinfer1::Dims> &opt_dims,
                                   const std::vector<nvinfer1::Dims> &max_dims)
    : inputs_(inputs),
      outputs_(outputs),
      all_ops_(std::move(ops)),
      device_info_(device_info),
      runtime_(runtime),
      min_dims_(min_dims),
      opt_dims_(opt_dims),
      max_dims_(max_dims) {
  trt_specific_weight_handled_inner_ = {
    ops::kNameTranspose, ops::kNameReshape, ops::kNameStridedSlice, ops::kNameExpandDims, ops::kNameTopKFusion,
  };
  if (!support_resize) {
    input_batchsize_index_ = -1;
    input_hw_index_ = -1;
  }
  if (!support_hw_resize) {
    input_hw_index_ = -1;
  }
}

TensorRTSubGraph::~TensorRTSubGraph() {
  if (ctx_ != nullptr) {
    delete ctx_;
  }
  if (config_ != nullptr) {
    config_->destroy();
    config_ = nullptr;
  }
  if (trt_context_ != nullptr) {
    trt_context_->destroy();
    trt_context_ = nullptr;
  }
  if (engine_ != nullptr) {
    engine_->destroy();
    engine_ = nullptr;
  }
  if (tensor_bindings_ != nullptr) {
    delete[] tensor_bindings_;
    tensor_bindings_ = nullptr;
  }
  for (auto op : all_ops_) {
    delete op;
  }
}

int TensorRTSubGraph::Init(cudaStream_t stream) {
  auto ret = GetGraphInOutOps(inputs_, outputs_, &in_ops_, &out_ops_, all_ops_);
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "Get TensorRT subgraph input and output ops failed.";
    return RET_ERROR;
  }
  profile_ = runtime_->GetBuilder()->createOptimizationProfile();
  if (profile_ == nullptr) {
    MS_LOG(ERROR) << "createOptimizationProfile failed.";
    return RET_ERROR;
  }
  ctx_ = new (std::nothrow) TensorRTContext();
  if (ctx_ == nullptr) {
    MS_LOG(ERROR) << "New TensorRTContext failed.";
    return RET_OK;
  }
  ctx_->SetRuntime(runtime_);
  if (!ctx_->Init()) {
    MS_LOG(ERROR) << "New TensorRTContext failed.";
    return RET_OK;
  }
  if (SetDeviceConfig(stream) != RET_OK) {
    MS_LOG(WARNING) << "set tensorrt config failed.";
  }
  serializer_ = std::make_shared<TensorRTSerializer>(serialize_file_path_);
  if (serializer_ == nullptr) {
    MS_LOG(ERROR) << "create Serializer failed.";
    return RET_ERROR;
  }
  engine_ = serializer_->GetSerializedEngine();
  if (engine_ != nullptr) {
    MS_LOG(INFO) << "using serialized engine " << serialize_file_path_;
    return RET_OK;
  }
  for (size_t i = 0; i < inputs_.size(); i++) {
    if (inputs_[i].Shape().size() != DIMENSION_4D) {
      input_hw_index_ = -1;
    }
  }
  return RET_OK;
}

int TensorRTSubGraph::BuildEngine() {
  // print all network ops
  if (this->config_->addOptimizationProfile(profile_) == -1) {
    MS_LOG(ERROR) << "addOptimizationProfile failed.";
    return RET_ERROR;
  }
  MS_LOG(INFO) << "build engine for tensorrt network: " << ctx_->network()->getName();
  for (int i = 0; i < ctx_->network()->getNbLayers(); i++) {
    MS_LOG(DEBUG) << "tensorrt op: " << ctx_->network()->getLayer(i)->getName();
  }
  MS_LOG(DEBUG) << "end of tensorrt network: " << ctx_->network()->getName();

  this->engine_ = runtime_->GetBuilder()->buildEngineWithConfig(*ctx_->network(), *this->config_);
  if (this->engine_ == nullptr) {
    MS_LOG(ERROR) << "Create engine failed in TensorRT network";
    return RET_ERROR;
  }
  if (serialize_file_path_.size() > 0) {
    serializer_->SaveSerializedEngine(engine_);
  }
  return RET_OK;
}

int TensorRTSubGraph::SetDeviceConfig(cudaStream_t stream) {
  if (config_ == nullptr) {
    this->config_ = runtime_->GetBuilder()->createBuilderConfig();
    if (this->config_ == nullptr) {
      MS_LOG(ERROR) << "create builder config failed.";
      return RET_ERROR;
    }
  }
  // set fp16
  if (device_info_->GetEnableFP16() && runtime_->GetBuilder()->platformHasFastFp16()) {
    MS_LOG(INFO) << "set fp16 flag successfully for tensorrt.";
    config_->setFlag(nvinfer1::BuilderFlag::kFP16);
    runtime_->SetRuntimePrecisionMode(RuntimePrecisionMode_FP16);
  }

  // set int8
  if (IsInt8Mode() && runtime_->GetBuilder()->platformHasFastInt8()) {
    MS_LOG(INFO) << "set int8 flag successfully for tensorrt.";
    config_->setFlag(nvinfer1::BuilderFlag::kINT8);
    // Mark calibrator as null
    config_->setInt8Calibrator(nullptr);
    input_hw_index_ = -1;
  } else {
    MS_LOG(INFO) << "inputs no quant params or platform not support int8.";
  }
  runtime_->SetCudaStream(stream);
  config_->setProfileStream(stream);
  stream_ = stream;
  MS_LOG(INFO) << GetRankID() << " tensorrt subgraph stream: " << stream_;

  // config setMaxWorkspaceSize to 2047 MB for max limit
  config_->setMaxWorkspaceSize(2047 * (1 << 20));
  return RET_OK;
}

bool TensorRTSubGraph::IsInt8Mode() {
  for (auto cur_op : all_ops_) {
    if (cur_op->GetQuantType() == schema::QuantType_QUANT_ALL) {
      return true;
    }
  }
  return false;
}

nvinfer1::ITensor *TensorRTSubGraph::SetTensorRTNetworkInput(const TensorInfo &in_tensor, size_t index) {
  for (int i = 0; i < ctx_->network()->getNbInputs(); i++) {
    if (in_tensor.Name().compare(ctx_->network()->getInput(i)->getName()) == 0) {
      MS_LOG(INFO) << "input tensor is already added in network: " << in_tensor.Name();
      return ctx_->network()->getInput(i);
    }
  }

  auto cuda_dtype = ConvertDataType(in_tensor.DataType());
  if (static_cast<int>(cuda_dtype) == -1) {
    MS_LOG(ERROR) << "Unsupported input data type " << static_cast<int>(in_tensor.DataType());
    return nullptr;
  }
  nvinfer1::Dims input_dims;
  if (min_dims_.size() != 0 && min_dims_.size() == opt_dims_.size() && min_dims_.size() == max_dims_.size()) {
    input_dims = SetInputDimsProfile(in_tensor, index);
  } else {
    input_dims = ParseInputDimsProfile(in_tensor);
  }
  MS_LOG(INFO) << "add network input: " << in_tensor.Name();
  return ctx_->network()->addInput(in_tensor.Name().c_str(), cuda_dtype, input_dims);
}

nvinfer1::Dims TensorRTSubGraph::SetInputDimsProfile(const TensorInfo &in_tensor, size_t index) {
  nvinfer1::Dims input_dims;
  input_dims.nbDims = min_dims_[index].nbDims;
  for (int i = 0; i != input_dims.nbDims; ++i) {
    input_dims.d[i] = (max_dims_[index].d[i] != min_dims_[index].d[i]) ? -1 : min_dims_[index].d[i];
  }
  if (profile_ == nullptr) {
    MS_LOG(ERROR) << "profile is null.";
    return input_dims;
  }
  if (!profile_->setDimensions(in_tensor.Name().c_str(), nvinfer1::OptProfileSelector::kMIN, min_dims_[index])) {
    MS_LOG(ERROR) << "setDimensions of kMIN failed for " << in_tensor.Name();
    return input_dims;
  }
  if (!profile_->setDimensions(in_tensor.Name().c_str(), nvinfer1::OptProfileSelector::kOPT, opt_dims_[index])) {
    MS_LOG(ERROR) << "setDimensions of kOPT failed for " << in_tensor.Name();
    return input_dims;
  }
  if (!profile_->setDimensions(in_tensor.Name().c_str(), nvinfer1::OptProfileSelector::kMAX, max_dims_[index])) {
    MS_LOG(ERROR) << "setDimensions of kMAX failed for " << in_tensor.Name();
    return input_dims;
  }
  DebugDims(min_dims_[index]);
  DebugDims(opt_dims_[index]);
  DebugDims(max_dims_[index]);
  return input_dims;
}

nvinfer1::Dims TensorRTSubGraph::ParseInputDimsProfile(const TensorInfo &in_tensor) {
  nvinfer1::Dims input_dims = ConvertCudaDims(in_tensor.Shape());
  if (profile_ == nullptr) {
    MS_LOG(ERROR) << "profile is null.";
    return input_dims;
  }
  if (runtime_->GetBatchSize() == 0) {
    runtime_->SetBatchSize(input_dims.d[0]);
    MS_LOG(INFO) << "batch size init as " << runtime_->GetBatchSize();
    if (input_batchsize_index_ != -1) {
      input_dims.d[0] = -1;  // dynamic batch size with wildcard N, default batchsize is first dims
      input_batchsize_index_ = 0;
    }
  } else {
    if (input_batchsize_index_ != -1) {
      for (int n = 0; n < input_dims.nbDims; n++) {
        if (input_dims.d[n] == runtime_->GetBatchSize()) {
          runtime_->SetBatchSize(std::max(input_dims.d[0], runtime_->GetBatchSize()));
          // first dims equals to batchsize
          input_dims.d[n] = -1;
          input_batchsize_index_ = n;
          break;
        }
      }
    }
  }
  // only support NHWC HW dim resize
  if (input_hw_index_ != -1) {
    MS_LOG(INFO) << "input tensor format is (NHWC:1, NCHW:0): " << in_tensor.format();
    input_hw_index_ = in_tensor.format() == Format::NHWC ? 1 : 2;  // NCHW is 2
    input_dims.d[input_hw_index_] = -1;
    input_dims.d[input_hw_index_ + 1] = -1;
  }
  auto shape = in_tensor.Shape();
  // We do not need to check the return of setDimension and addOptimizationProfile here as all dims are explicitly set
  nvinfer1::Dims input_dims_min = ConvertCudaDims(shape);
  if (input_batchsize_index_ != -1) {
    input_dims_min.d[input_batchsize_index_] = 1;
    if (input_hw_index_ != -1) {
      input_dims_min.d[input_hw_index_] = 1;
      input_dims_min.d[input_hw_index_ + 1] = 1;
    }
  }
  if (!profile_->setDimensions(in_tensor.Name().c_str(), nvinfer1::OptProfileSelector::kMIN, input_dims_min)) {
    MS_LOG(ERROR) << "setDimensions of kMIN failed for " << in_tensor.Name();
    return input_dims;
  }
  nvinfer1::Dims input_dims_opt = ConvertCudaDims(shape);
  if (!profile_->setDimensions(in_tensor.Name().c_str(), nvinfer1::OptProfileSelector::kOPT, input_dims_opt)) {
    MS_LOG(ERROR) << "setDimensions of kOPT failed for " << in_tensor.Name();
    return input_dims;
  }
  nvinfer1::Dims input_dims_max = ConvertCudaDims(shape);
  // input_dims_max should be the same with input network dims
  if (!profile_->setDimensions(in_tensor.Name().c_str(), nvinfer1::OptProfileSelector::kMAX, input_dims_max)) {
    MS_LOG(ERROR) << "setDimensions of kMAX failed for " << in_tensor.Name();
    return input_dims;
  }
  DebugDims(input_dims);
  DebugDims(input_dims_min);
  DebugDims(input_dims_opt);
  DebugDims(input_dims_max);
  return input_dims;
}

int TensorRTSubGraph::ParseInputsProfile() {
  MS_LOG(INFO) << "using serialied engine.";
  for (auto in_tensor : inputs()) {
    auto dim = ParseInputDimsProfile(in_tensor);
    if (dim.nbDims <= 0) {
      MS_LOG(ERROR) << "input dims is invalid.";
      return RET_ERROR;
    }
  }
  return RET_OK;
}

int TensorRTSubGraph::BuildTensorRTGraph() {
  MS_ASSERT(!all_ops_.empty());
  int ret;
  if (engine_ != nullptr) {
    return ParseInputsProfile();
  }
  // build engine online
  for (auto cur_op : all_ops_) {
    cur_op->SetRuntime(runtime_);
    for (size_t i = 0; i != cur_op->inputs().size(); ++i) {
      // Data From CPU
      auto in_tensor = cur_op->inputs()[i];
      if (IsSubGraphInputTensor(this->inputs(), in_tensor)) {
        nvinfer1::ITensor *trt_tensor = SetTensorRTNetworkInput(in_tensor, i);
        if (trt_tensor == nullptr) {
          MS_LOG(ERROR) << "SetTensorRTNetworkInput failed for " << in_tensor.Name();
          return RET_ERROR;
        }
        // avoid bool input tensor
        cur_op->SetSupportInputBool(false);

        ctx_->RegisterTensorWithSameName(ITensorHelper{trt_tensor, in_tensor.format(), true}, in_tensor.Name());
        continue;
      }

      ITensorHelper trt_tensor = FindTensorRTInputs(cur_op, in_tensor);
      if (trt_tensor.trt_tensor_ == nullptr) {
        // weight tensor
        auto weight_handled_inner =
          cur_op->IsWeightInputHanledInner() ||
          trt_specific_weight_handled_inner_.find(cur_op->type()) != trt_specific_weight_handled_inner_.end();
        if (!weight_handled_inner) {
          if (!in_tensor.IsConst()) {
            MS_LOG(ERROR) << "Weight Tensor data is not const.";
            return RET_ERROR;
          }
          trt_tensor.trt_tensor_ = lite::ConvertConstantTensor(ctx_, in_tensor, cur_op->GetOpName());
          trt_tensor.format_ = Format::NCHW;
          MS_LOG(INFO) << "auto convert constant tensor for: " << in_tensor.Name();
          ctx_->RegisterTensor(trt_tensor, in_tensor.Name());
        }
      } else {
        ctx_->RegisterTensor(trt_tensor, in_tensor.Name());
      }
    }
    MS_LOG(DEBUG) << "Parsing TensorRT op for " << cur_op->GetOpName();

    ret = cur_op->AddInnerOp(ctx_);
    if (ret != RET_OK) {
      MS_LOG(ERROR) << "Add op failed in TensorRT network: " << cur_op->GetOpName();
      return RET_ERROR;
    }
    ret = cur_op->SetInt8DynamicRange(ctx_);
    if (ret != RET_OK) {
      MS_LOG(ERROR) << "Set Int8 dynamic range failed in TensorRT network: " << cur_op->GetOpName();
      return RET_ERROR;
    }
  }
  ret = MarkOutputs();
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "MarkOutputs failed in TensorRT network";
    return ret;
  }

  std::string network_name = "network_" + std::string(ctx_->network()->getInput(0)->getName()) + "_" +
                             std::string(ctx_->network()->getOutput(0)->getName());
  ctx_->network()->setName(network_name.c_str());
  this->name_ = network_name;
  ret = BuildEngine();
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "Create engine failed in TensorRT network";
    return ret;
  }
  return RET_OK;
}

int TensorRTSubGraph::MarkOutputs() {
  // Mark NetWork Output Tensor.
  for (const auto &out_tensor : outputs_) {
    for (auto out_op : this->out_ops_) {
      for (size_t index = 0; index < out_op->outputs().size(); index++) {
        if (out_op->outputs()[index] == out_tensor) {
          MS_LOG(INFO) << "markOutput for: " << out_tensor.Name();
          auto output_helper = out_op->output(ctx_, index);
          nvinfer1::ITensor *out_trt_tensor = output_helper.trt_tensor_;
          out_trt_tensor->setName(("__" + out_tensor.Name()).c_str());
          out_trt_tensor = ctx_->network()->addIdentity(*out_trt_tensor)->getOutput(0);
          out_trt_tensor->setName(out_tensor.Name().c_str());
          ctx_->network()->markOutput(*out_trt_tensor);
          for (int n = 0; n < out_trt_tensor->getDimensions().nbDims; n++) {
            if (out_trt_tensor->getDimensions().d[n] == -1) {
              output_batchsize_index_ = n;
              break;
            }
          }
        }
      }
    }
  }
  return RET_OK;
}

int TensorRTSubGraph::Prepare() {
  int ret = lite::SetCudaDevice(device_info_);
  if (ret != RET_OK) {
    return ret;
  }
  if (this->engine_ == nullptr) {
    MS_LOG(ERROR) << "engine_ is null in this builder_";
    return RET_ERROR;
  }
  this->trt_context_ = this->engine_->createExecutionContext();
  if (this->trt_context_ == nullptr) {
    MS_LOG(ERROR) << "TensorRTSubGraph create context failed.";
    return RET_ERROR;
  }
  int binding_num = this->engine_->getNbBindings();
  if (binding_num < 0) {
    MS_LOG(ERROR) << "TensorRTSubGraph binding num < 0.";
    return RET_ERROR;
  }
  tensor_bindings_ = new (std::nothrow) void *[binding_num];
  if (tensor_bindings_ == nullptr) {
    MS_LOG(ERROR) << "malloc tensor binding array failed.";
    return RET_ERROR;
  }

  for (auto tensor : inputs_) {
    auto device_ptr = runtime_->GetAllocator()->MallocDeviceMem(tensor, tensor.DataSize());
    if (device_ptr == nullptr) {
      MS_LOG(ERROR) << "malloc for inputs tensor device memory failed.";
      return RET_ERROR;
    }
    runtime_->SetBatchSize(tensor.Shape()[0]);
    int index = this->engine_->getBindingIndex(tensor.Name().c_str());
    MS_LOG(INFO) << "device index " << index << " for tensor : " << tensor.Name() << " attr: " << device_ptr;
    tensor_bindings_[index] = device_ptr;
    trt_in_tensor_name_.push_back(tensor.Name());
    nvinfer1::Dims input_dims = ConvertCudaDims(tensor.Shape());
    for (int od = 0; od < input_dims.nbDims; od++) {
      MS_LOG(DEBUG) << "in tensor " << tensor.Name() << " dims at " << od << " is " << input_dims.d[od];
    }

    if (!this->trt_context_->setBindingDimensions(index, input_dims)) {
      MS_LOG(ERROR) << "invalid input dims of " << tensor.Name();
      return RET_ERROR;
    }
  }

  if (!this->trt_context_->allInputDimensionsSpecified()) {
    MS_LOG(ERROR) << "input dims need to be specified.";
    return RET_ERROR;
  }
  for (auto op : all_ops_) {
    ret = op->Prepare(tensor_bindings_, engine_);
    if (ret != RET_OK) {
      MS_LOG(ERROR) << "prepare op failed of " << op->GetOpName();
      return RET_ERROR;
    }
  }
  for (auto &tensor : outputs_) {
    int index = this->engine_->getBindingIndex(tensor.Name().c_str());
    auto out_dims = trt_context_->getBindingDimensions(index);
    int elem_num = std::accumulate(out_dims.d, out_dims.d + out_dims.nbDims, 1, std::multiplies<int>());
    DebugDims(out_dims);
    auto new_shape = lite::ConvertMSShape(out_dims);
    MS_LOG(INFO) << "Set output shape of " << tensor.Name() << " to " << new_shape << "  by tensorrt binding output";
    tensor.SetShape(new_shape);
    auto type_size = DataTypeSize(static_cast<enum TypeId>(tensor.DataType()));
    auto device_ptr = runtime_->GetAllocator()->MallocDeviceMem(tensor, elem_num * type_size);
    if (device_ptr == nullptr) {
      MS_LOG(ERROR) << "malloc for outputs tensor device memory failed.";
      return RET_ERROR;
    }
    tensor_bindings_[index] = device_ptr;
    trt_out_tensor_name_.push_back(tensor.Name());
  }
  return RET_OK;
}

int TensorRTSubGraph::OnNewInputShapes(const std::vector<ShapeVector> &new_shapes) {
  if (inputs_.size() != new_shapes.size()) {
    MS_LOG(ERROR) << "Graph inputs size " << inputs_.size() << " != resize input size " << new_shapes.size();
    return RET_ERROR;
  }
  int batch_size = -1;
  for (size_t i = 0; i < trt_in_tensor_name_.size(); i++) {
    if (inputs_[i].Shape() == new_shapes[i]) {
      continue;
    }
    if (input_batchsize_index_ == -1) {
      MS_LOG(ERROR) << "current network don't support resize.";
      return RET_ERROR;
    }
    inputs_[i].SetShape(new_shapes[i]);
    if (ctx_->network() != nullptr) {
      for (int j = 0; j < ctx_->network()->getNbInputs(); j++) {
        if (trt_in_tensor_name_[i].compare(ctx_->network()->getInput(j)->getName()) != 0) {
          continue;
        }
        nvinfer1::Dims construct_dims = ctx_->network()->getInput(j)->getDimensions();
        bool ret = ValidInputResizeDims(construct_dims, inputs_[i].Shape());
        if (!ret) {
          MS_LOG(ERROR) << "input resize shape is invalid.";
          return RET_ERROR;
        }
      }
    }

    MS_LOG(INFO) << "resize at input_batch_index " << input_batchsize_index_ << ", update batch size to "
                 << inputs_[i].Shape()[input_batchsize_index_];
    int new_batch_size = inputs_[i].Shape()[input_batchsize_index_];
    if (batch_size != -1 && batch_size != new_batch_size) {
      MS_LOG(ERROR) << "Batch size " << batch_size << " of input 0 != batch size " << new_batch_size << " of input "
                    << i;
      return RET_ERROR;
    }
    batch_size = new_batch_size;
    runtime_->SetBatchSize(batch_size);

    int index = this->engine_->getBindingIndex(trt_in_tensor_name_[i].c_str());
    // Set actual input size
    nvinfer1::Dims input_dims = ConvertCudaDims(inputs_[i].Shape());
    for (int od = 0; od < input_dims.nbDims; od++) {
      MS_LOG(DEBUG) << "in tensor " << trt_in_tensor_name_[i] << " dims at " << od << " is " << input_dims.d[od];
    }

    if (!this->trt_context_->setBindingDimensions(index, input_dims)) {
      MS_LOG(ERROR) << "invalid input dims of " << inputs_[i].Name();
      return RET_ERROR;
    }
  }
  if (!this->trt_context_->allInputDimensionsSpecified()) {
    MS_LOG(ERROR) << "input dims need to be specified.";
    return RET_ERROR;
  }
  if (batch_size != -1) {
    for (size_t i = 0; i < trt_out_tensor_name_.size(); i++) {
      int index = this->engine_->getBindingIndex(trt_out_tensor_name_[i].c_str());
      auto out_dims = trt_context_->getBindingDimensions(index);
      auto new_shape = lite::ConvertMSShape(out_dims);
      MS_LOG(INFO) << "Set output shape of " << trt_out_tensor_name_[i] << " to " << new_shape
                   << "  by tensorrt binding output";
      outputs_[i].SetShape(new_shape);
    }
  }
  return RET_OK;
}

int TensorRTSubGraph::PreExecute(const std::vector<tensor::Tensor> &inputs,
                                 const std::vector<tensor::Tensor> &outputs) {
  if (inputs_.size() != inputs.size()) {
    MS_LOG(ERROR) << "Graph inputs size " << inputs_.size() << " != execute inputs size " << inputs.size();
    return RET_ERROR;
  }
  if (!outputs.empty() && outputs.size() != outputs_.size()) {
    MS_LOG(ERROR) << "Graph outputs size " << outputs_.size() << " != execute outputs size " << outputs.size();
    return RET_ERROR;
  }
  std::vector<ShapeVector> new_shapes;
  std::transform(inputs.begin(), inputs.end(), std::back_inserter(new_shapes), [](auto &t) { return t.shape_c(); });
  auto ret = OnNewInputShapes(new_shapes);
  if (ret != RET_OK) {
    return ret;
  }
  for (size_t i = 0; i < trt_in_tensor_name_.size(); i++) {
    auto trt_tensor_name = trt_in_tensor_name_[i];
    void *device_ptr = nullptr;
    auto input_device_address = inputs[i].device_address();
    if (input_device_address != nullptr && input_device_address->GetMutablePtr() != nullptr) {
      device_ptr = input_device_address->GetMutablePtr();
    } else {
      device_ptr = runtime_->GetAllocator()->MallocDeviceMem(trt_tensor_name, inputs_[i].DataSize(),
                                                             ConvertDataType(inputs_[i].DataType()));
      if (device_ptr == nullptr) {
        MS_LOG(ERROR) << "realloc for input tensor device memory failed.";
        return RET_ERROR;
      }
      ret = runtime_->GetAllocator()->SyncMemHostToDevice(inputs[i], trt_tensor_name);
      if (ret != RET_OK) {
        MS_LOG(ERROR) << "sync mem from host to device failed for " << trt_tensor_name;
        return RET_ERROR;
      }
      runtime_->GetAllocator()->MarkMemValid(trt_tensor_name, true);
    }
    int index = this->engine_->getBindingIndex(trt_tensor_name.c_str());
    MS_LOG(INFO) << "device index " << index << " for tensor : " << trt_tensor_name << " attr: " << device_ptr;
    tensor_bindings_[index] = device_ptr;
  }
  for (size_t i = 0; i < trt_out_tensor_name_.size(); i++) {
    const auto &trt_out_tensor_name = trt_out_tensor_name_[i];
    int index = this->engine_->getBindingIndex(trt_out_tensor_name.c_str());
    void *device_ptr = nullptr;
    if (outputs.size() > i) {
      auto &output = outputs[i];
      if (output.device_address() && output.device_address()->GetMutablePtr()) {
        if (output.Size() < outputs_[i].DataSize()) {
          MS_LOG(ERROR) << "Specified output device data size " << output.Size()
                        << " cannot less than execute output data size " << outputs_[i].DataSize()
                        << ", output shape: " << outputs_[i].Shape();
          return RET_ERROR;
        }
        device_ptr = output.device_address()->GetMutablePtr();
      }
    }
    if (!device_ptr) {
      device_ptr = runtime_->GetAllocator()->MallocDeviceMem(trt_out_tensor_name, outputs_[i].DataSize(),
                                                             ConvertDataType(outputs_[i].DataType()));
      if (device_ptr == nullptr) {
        MS_LOG(ERROR) << "realloc for outputs tensor device memory failed.";
        return RET_ERROR;
      }
    }
    tensor_bindings_[index] = device_ptr;
  }
  return RET_OK;
}

int TensorRTSubGraph::PostExecute(std::vector<tensor::Tensor> *outputs) {
  if (!outputs->empty() && outputs->size() != outputs_.size()) {
    MS_LOG(ERROR) << "Graph outputs size " << outputs_.size() << " != execute outputs size " << outputs->size();
    return RET_ERROR;
  }
  auto has_outputs = !outputs->empty();
  for (size_t i = 0; i < trt_out_tensor_name_.size(); i++) {
    const auto &trt_out_tensor_name = trt_out_tensor_name_[i];
    int index = this->engine_->getBindingIndex(trt_out_tensor_name.c_str());
    // actual output tensor dims
    auto out_dims = this->trt_context_->getBindingDimensions(index);
    std::vector<int64_t> new_shape = lite::ConvertMSShape(out_dims);
    // batchsize resize need set new batch size
    if (input_batchsize_index_ != -1) {
      if (runtime_->GetBatchSize() != new_shape[output_batchsize_index_]) {
        new_shape[output_batchsize_index_] = runtime_->GetBatchSize();
      }
    }
    outputs_[i].SetShape(new_shape);
    for (int od = 0; od < out_dims.nbDims; od++) {
      MS_LOG(DEBUG) << "out tensor " << trt_out_tensor_name << " dims at " << od << " is " << new_shape[od];
    }
    runtime_->GetAllocator()->MarkMemValid(trt_out_tensor_name, true);
    if (has_outputs) {
      auto &tensor = outputs->at(i);
      auto dst_device = tensor.device_address();
      if (dst_device == nullptr || dst_device->GetMutablePtr() == nullptr) {
        if (tensor.Size() < outputs_[i].DataSize()) {
          MS_LOG(ERROR) << "Specified output host data size " << tensor.Size()
                        << " cannot less than execute output data size " << outputs_[i].DataSize()
                        << ", output shape: " << new_shape;
          return RET_ERROR;
        }
        auto host_address = tensor.data_c();
        if (host_address == nullptr) {
          MS_LOG(ERROR) << "Specified output device or host address cannot be nullptr";
          return RET_ERROR;
        }
        int sync_ret =
          runtime_->GetAllocator()->SyncMemDeviceToHost(host_address, outputs_[i].DataSize(), trt_out_tensor_name);
        if (sync_ret != RET_OK) {
          MS_LOG(ERROR) << "sync mem from device to host failed for " << trt_out_tensor_name;
          return sync_ret;
        }
      }
    } else {
      tensor::Tensor output_tensor(static_cast<enum TypeId>(outputs_[i].DataType()), new_shape);
      int sync_ret = runtime_->GetAllocator()->SyncMemDeviceToHost(&output_tensor, trt_out_tensor_name);
      if (sync_ret != RET_OK) {
        MS_LOG(ERROR) << "sync mem from device to host failed for " << trt_out_tensor_name;
        return sync_ret;
      }
      outputs->push_back(output_tensor);
    }
    runtime_->GetAllocator()->MarkMemValid(trt_out_tensor_name, false);
  }
  // make mem invalid, prepare for next execute
  for (size_t i = 0; i < inputs_.size(); i++) {
    runtime_->GetAllocator()->MarkMemValid(trt_in_tensor_name_[i], false);
  }
  return RET_OK;
}

bool TensorRTSubGraph::ValidInputResizeDims(const nvinfer1::Dims &construct_dims,
                                            const std::vector<int64_t> &resize_input_shape) {
  if (static_cast<size_t>(construct_dims.nbDims) != resize_input_shape.size()) {
    MS_LOG(ERROR) << "invalid resize input.";
    return false;
  }
  if (input_hw_index_ == -1) {
    // only NHWC format support HW resize, otherwise only support batchsize resize
    for (int d = 0; d < construct_dims.nbDims; d++) {
      if (d != input_batchsize_index_ && construct_dims.d[d] != resize_input_shape[d]) {
        MS_LOG(ERROR) << "only support dynamic batch size resize input.";
        return false;
      }
    }
  } else if ((input_hw_index_ == 1 && construct_dims.d[DIMENSION_3D] != resize_input_shape[DIMENSION_3D]) ||
             (input_hw_index_ == DIMENSION_2D && construct_dims.d[1] != resize_input_shape[1])) {
    // input may be nhwc || nchw
    MS_LOG(ERROR) << "don't support dynamic channel resize input.";
    return false;
  }
  return true;
}

int TensorRTSubGraph::Execute(const std::vector<tensor::Tensor> &inputs, std::vector<tensor::Tensor> *outputs) {
  int ret = lite::SetCudaDevice(device_info_);
  if (ret != RET_OK) {
    return ret;
  }
  ret = PreExecute(inputs, *outputs);
  if (ret != RET_OK) {
    return ret;
  }
  if (!this->trt_context_->executeV2(tensor_bindings_)) {
    MS_LOG(ERROR) << "TensorRT execute failed.";
    return RET_ERROR;
  }
  return PostExecute(outputs);
}

int TensorRTSubGraph::Resize(const std::vector<tensor::Tensor> &, const std::vector<ShapeVector> &new_shapes) {
  return OnNewInputShapes(new_shapes);
}

ITensorHelper TensorRTSubGraph::FindTensorRTInputs(TensorRTOp *cur_op, const TensorInfo &in_tensor) {
  for (auto input_op : cur_op->in_ops()) {
    for (size_t i = 0; i < input_op->outputs().size(); i++) {
      auto out_tensor = input_op->outputs().at(i);
      if (in_tensor.Name().compare(out_tensor.Name()) == 0) {
        return input_op->output(ctx_, i);
      }
    }
  }
  return ITensorHelper{};
}
}  // namespace mindspore::lite
