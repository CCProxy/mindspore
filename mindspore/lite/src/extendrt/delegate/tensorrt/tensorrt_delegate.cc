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

#include "src/extendrt/delegate/tensorrt/tensorrt_delegate.h"
#include <cuda_runtime.h>
#include <vector>
#include <fstream>
#include <string>
#include <map>
#include "src/runtime/delegate/delegate_utils.h"
#include "src/runtime/delegate/auto_registration_factory.h"

namespace mindspore::lite {
namespace {
std::vector<std::string> Split(const std::string &str, const std::string &delim) {
  auto start = 0U;
  auto end = str.find(delim);
  std::vector<std::string> substrs;
  while (end != std::string::npos) {
    substrs.push_back(str.substr(start, end - start));
    start = end + delim.length();
    end = str.find(delim, start);
  }
  substrs.push_back(str.substr(start, end));
  return substrs;
}
std::vector<nvinfer1::Dims> StringToDims(const std::string &str) {
  std::vector<nvinfer1::Dims> all_dims;
  std::vector<std::string> all_str_dims = Split(str, ";");
  for (const std::string &str_dims : all_str_dims) {
    std::vector<std::string> str_dims_vec = Split(str_dims, ",");
    nvinfer1::Dims dims;
    dims.nbDims = str_dims_vec.size();
    for (size_t i = 0; i != str_dims_vec.size(); ++i) {
      dims.d[i] = std::stoi(str_dims_vec[i]);
    }
    all_dims.push_back(dims);
  }
  return all_dims;
}
}  // namespace
TensorRTDelegate::TensorRTDelegate(mindspore::Context *context, const std::string &cache_model_path, size_t vocab_size,
                                   size_t device_cache_size, const std::string &serialize_path,
                                   const std::map<std::string, std::string> &input_ranges)
    : context_(context),
      cache_model_path_(cache_model_path),
      vocab_size_(vocab_size),
      device_cache_size_(device_cache_size),
      serialize_path_(serialize_path) {
  if (input_ranges.find("min_dims") != input_ranges.end()) {
    min_dims_ = StringToDims(input_ranges.at("min_dims"));
  }
  if (input_ranges.find("opt_dims") != input_ranges.end()) {
    opt_dims_ = StringToDims(input_ranges.at("opt_dims"));
  }
  if (input_ranges.find("max_dims") != input_ranges.end()) {
    max_dims_ = StringToDims(input_ranges.at("max_dims"));
  }
  if (min_dims_.size() != opt_dims_.size() || min_dims_.size() != max_dims_.size()) {
    MS_LOG(WARNING) << "number of min_dims, opt_dims, max_dims are not equal";
  }
}
TensorRTDelegate::~TensorRTDelegate() {
  if (runtime_ != nullptr) {
    delete runtime_;
  }
  if (stream_ != nullptr) {
    cudaStreamDestroy(stream_);
  }
}
bool IsHardwareSupport() {
  int driver_version = 0;
  int ret = cudaDriverGetVersion(&driver_version);
  if (ret != cudaSuccess || driver_version == 0) {
    MS_LOG(WARNING) << "No nvidia GPU driver.";
    return false;
  }
  return true;
}

Status TensorRTDelegate::Init() {
  if (!IsHardwareSupport()) {
    return mindspore::kLiteNotSupport;
  }
  std::vector<std::shared_ptr<DeviceInfoContext>> device_list = context_->MutableDeviceInfo();
  auto iter = std::find_if(device_list.begin(), device_list.end(), [](std::shared_ptr<DeviceInfoContext> device) {
    return device->GetDeviceType() == DeviceType::kGPU;
  });
  if (iter == device_list.end()) {
    MS_LOG(ERROR) << "no gpu device info found for TensorRT.";
    return mindspore::kLiteError;
  }
  auto gpu_info = (*iter)->Cast<GPUDeviceInfo>();
  if (gpu_info == nullptr) {
    MS_LOG(ERROR) << "no gpu device info found for TensorRT.";
    return mindspore::kLiteError;
  }
  device_info_ = gpu_info;
  int ret = lite::SetCudaDevice(device_info_);
  if (ret != RET_OK) {
    return mindspore::kLiteError;
  }
  if (runtime_ == nullptr) {
    runtime_ = new (std::nothrow) TensorRTRuntime();
    if (runtime_ == nullptr) {
      MS_LOG(ERROR) << "create TensorRTRuntime failed.";
      return mindspore::kLiteError;
    }
  }
  if (runtime_->Init() != RET_OK) {
    MS_LOG(ERROR) << "TensorRTRuntime init failed.";
    return mindspore::kLiteError;
  }
  runtime_->SetDeviceID(device_info_->GetDeviceID());

  auto cuda_ret = cudaStreamCreate(&stream_);
  if (cuda_ret != cudaSuccess) {
    MS_LOG(ERROR) << "Cuda create stream failed";
    return mindspore::kLiteError;
  }

  cache_mgr_ = std::make_shared<cache::EmbeddingCacheManager>();
  if (cache_mgr_ == nullptr) {
    MS_LOG(ERROR) << "malloc EmbeddingCacheManager failed.";
    return kLiteMemoryFailed;
  }
  auto cache_ret = cache_mgr_->Init(cache_model_path_, vocab_size_, device_cache_size_);
  if (cache_ret != mindspore::kSuccess) {
    MS_LOG(ERROR) << "cache_mgr_ init failed.";
    return cache_ret;
  }

  return mindspore::kSuccess;
}

Status TensorRTDelegate::BuildSubGraph(DelegateModel<schema::Primitive> *model) {
  KernelIter from, end;
  std::vector<TensorRTOp *> tensorrt_ops;
  int tensorrt_subgraph_index = 0;
  for (KernelIter iter = model->BeginKernelIterator(); iter != model->EndKernelIterator(); iter++) {
    kernel::Kernel *kernel = *iter;
    auto tensorrt_op = FindTensorRTOp(kernel, model->GetPrimitive(kernel));
    if (tensorrt_op != nullptr) {
      if (cache_mgr_->CheckIsCacheKernel(kernel)) {
        auto cache_ret = cache_mgr_->InitCacheKernel(kernel, device_info_->GetDeviceID(), &stream_);
        if (cache_ret != kSuccess) {
          MS_LOG(ERROR) << "InitCacheKernel failed " << kernel->name();
          return cache_ret;
        }
      }

      // If tensorrt_ops does not equal nullptr, this kernel can be supported by delegate
      if (tensorrt_ops.size() == 0) {
        from = iter;
      }
      tensorrt_op->SetRuntime(this->runtime_);
      tensorrt_ops.push_back(tensorrt_op);
      end = iter;
    } else {
      if (tensorrt_ops.size() > 0) {
        auto tensorrt_subgraph = CreateTensorRTGraph(tensorrt_ops, model, from, end, tensorrt_subgraph_index);
        if (tensorrt_subgraph == nullptr) {
          MS_LOG(ERROR) << "Create TensorRT Graph failed.";
          return mindspore::kLiteNullptr;
        }
        tensorrt_subgraph_index++;
        iter = model->Replace(from, end + 1, tensorrt_subgraph);
        tensorrt_ops.clear();
      }
    }
  }
  if (tensorrt_ops.size() > 0) {
    auto tensorrt_subgraph = CreateTensorRTGraph(tensorrt_ops, model, from, end, tensorrt_subgraph_index);
    if (tensorrt_subgraph == nullptr) {
      MS_LOG(ERROR) << "Create TensorRT Graph failed.";
      return mindspore::kLiteNullptr;
    }
    model->Replace(from, end + 1, tensorrt_subgraph);
    tensorrt_ops.clear();
  }
  return mindspore::kSuccess;
}

Status TensorRTDelegate::Build(DelegateModel<schema::Primitive> *model) {
  int ret = lite::SetCudaDevice(device_info_);
  if (ret != RET_OK) {
    return mindspore::kLiteError;
  }
  if (cache_model_path_.empty() && vocab_size_ > 0) {
    auto cache_ret = cache_mgr_->Init(model, vocab_size_, device_cache_size_);
    if (cache_ret != mindspore::kSuccess) {
      MS_LOG(ERROR) << "cache_mgr_ init failed.";
      return cache_ret;
    }
  }

  auto build_ret = BuildSubGraph(model);
  if (build_ret != kSuccess) {
    MS_LOG(INFO) << "BuildSubGraph failed";
    return build_ret;
  }

  return mindspore::kSuccess;
}

TensorRTOp *TensorRTDelegate::FindTensorRTOp(kernel::Kernel *kernel, const schema::Primitive *primitive) {
  auto in_tensors = kernel->inputs();
  auto out_tensors = kernel->outputs();
  auto name = kernel->name();
  auto node_type = primitive->value_type();
  auto &plugin_factory = AutoRegistrationFactory<schema::PrimitiveType, TensorRTGetOp>::Get();
  if (plugin_factory.HasKey(node_type)) {
    TensorRTOp *tensorrt_op =
      plugin_factory.GetCreator(node_type)(primitive, in_tensors, out_tensors, name, kernel->quant_type());
    if (tensorrt_op == nullptr) {
      return nullptr;
    }
    if (!support_resize_) {
      return tensorrt_op;
    }
    support_resize_ = tensorrt_op->GetDynamicShapeParams().support_dynamic_ ? support_resize_ : false;
    if (!tensorrt_op->GetDynamicShapeParams().support_dynamic_) {
      MS_LOG(WARNING) << "TensorRT subgraph don't support dynamic shape resize, because of op " << name;
      support_hw_resize_ = false;
      return tensorrt_op;
    }
    if (!support_hw_resize_) {
      return tensorrt_op;
    }
    support_hw_resize_ = tensorrt_op->GetDynamicShapeParams().support_hw_dynamic_ ? support_hw_resize_ : false;
    if (!tensorrt_op->GetDynamicShapeParams().support_hw_dynamic_) {
      MS_LOG(WARNING) << "TensorRT subgraph don't support dynamic hw dims resize, because of op " << name;
    }
    return tensorrt_op;
  } else {
    MS_LOG(WARNING) << "Unsupported op type for TensorRT. kernel->name:" << kernel->name()
                    << " type:" << schema::EnumNamePrimitiveType(primitive->value_type());
    return nullptr;
  }
}

TensorRTSubGraph *TensorRTDelegate::CreateTensorRTGraph(const std::vector<TensorRTOp *> &ops,
                                                        DelegateModel<schema::Primitive> *model, KernelIter from,
                                                        KernelIter end, int index) {
  auto in_tensors = GraphInTensors<TensorRTOp>(ops, model, from, end);
  auto out_tensors = GraphOutTensors<TensorRTOp>(ops, model, from, end);
  auto *tensorrt_graph =
    new (std::nothrow) TensorRTSubGraph(ops, in_tensors, out_tensors, context_, device_info_, runtime_, support_resize_,
                                        support_hw_resize_, min_dims_, opt_dims_, max_dims_);
  if (tensorrt_graph == nullptr) {
    MS_LOG(ERROR) << "new tensorrt_graph failed.";
    return nullptr;
  }
  tensorrt_graph->SetCacheManager(cache_mgr_);
  if (serialize_path_.size() > 0) {
    tensorrt_graph->SetSerializePath(serialize_path_ + "_trt" + std::to_string(GetRankID()) + ".bin_" +
                                     std::to_string(index));
  }

  // 1. For every op, find pre and next ops
  FindPreNextOps<TensorRTOp>(ops);

  // 2. Init TensorRT SubGraph.
  auto ret = tensorrt_graph->Init(stream_);
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "TensorRTGraph init failed.";
    delete tensorrt_graph;
    return nullptr;
  }

  // 3. Build TensorRT Model.
  ret = tensorrt_graph->BuildTensorRTGraph();
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "TensorRTGraph build failed.";
    delete tensorrt_graph;
    return nullptr;
  }

  return tensorrt_graph;
}
}  // namespace mindspore::lite
