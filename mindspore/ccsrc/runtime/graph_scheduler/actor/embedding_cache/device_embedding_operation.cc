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
#include <limits>
#include "kernel/common_utils.h"
#include "backend/common/optimizer/dynamic_shape/dynamic_shape_helper.h"
#include "runtime/graph_scheduler/actor/embedding_cache/embedding_cache_prefetch_actor.h"
#include "runtime/graph_scheduler/actor/embedding_cache/device_embedding_operation.h"

namespace mindspore {
namespace runtime {
bool DeviceEmbeddingOperation::Initialize() {
  BuildEmbeddingCacheLookupKernel();
  BuildEmbeddingCacheUpdateKernel();
  return true;
}
bool DeviceEmbeddingOperation::PullCacheFromLocalHostToDevice(const HashTableInfo &hash_info) {
  auto swap_indices_size = statistics_info_->host_to_device_size_;
  if (swap_indices_size == 0) {
    return true;
  }

  MS_ERROR_IF_NULL(embedding_device_cache_);
  MS_ERROR_IF_NULL(embedding_host_cache_);

  auto host_cache_host_to_device_index = embedding_host_cache_->host_to_device_index.get();
  auto device_cache_host_to_device_index = embedding_device_cache_->host_to_device_index.get();
  MS_ERROR_IF_NULL(host_cache_host_to_device_index);
  MS_ERROR_IF_NULL(device_cache_host_to_device_index);

  auto embedding_size = hash_info.embedding_size;
  MS_ERROR_IF_NULL(hash_info.device_address.addr);
  auto hash_table_addr = reinterpret_cast<float *>(hash_info.device_address.addr);
  auto cache_vocab_size = hash_info.cache_vocab_size;
  MS_ERROR_IF_NULL(hash_info.host_address);
  auto host_hash_table_addr = reinterpret_cast<float *>(hash_info.host_address.get());
  auto swap_out_data = std::make_unique<float[]>(swap_indices_size * embedding_size);
  RETURN_IF_FALSE_WITH_LOG(actor_->LookupLocalHostCache(embedding_size, swap_indices_size, host_hash_table_addr,
                                                        host_cache_host_to_device_index, swap_out_data.get()),
                           "Lookup local host cache failed.");

  RETURN_IF_FALSE_WITH_LOG(
    MemcpyHostToDeviceAsync(embedding_device_cache_->hash_swap_value_addr_, swap_out_data.get(),
                            swap_indices_size * embedding_size * sizeof(float), device_context_, stream_id_),
    "Memcpy host to device asynchronously failed.");
  RETURN_IF_FALSE_WITH_LOG(
    MemcpyHostToDeviceAsync(embedding_device_cache_->hash_swap_index_addr_, device_cache_host_to_device_index,
                            swap_indices_size * sizeof(int), device_context_, stream_id_),
    "Memcpy host to device asynchronously failed.");

  RETURN_IF_FALSE_WITH_LOG(
    UpdateDeviceCache(embedding_device_cache_->hash_swap_index_addr_, embedding_device_cache_->hash_swap_value_addr_,
                      swap_indices_size, cache_vocab_size, embedding_size, hash_table_addr),
    "Update device embedding cache failed.");
  MS_ERROR_IF_NULL(device_context_);
  MS_ERROR_IF_NULL(device_context_->device_res_manager_);
  RETURN_IF_FALSE_WITH_LOG(device_context_->device_res_manager_->SyncStream(stream_id_), "Synchronize stream failed.");
  return true;
}

bool DeviceEmbeddingOperation::PushCacheFromDeviceToLocalHost(const HashTableInfo &hash_info) {
  auto swap_indices_size = statistics_info_->device_to_host_size_;
  if (swap_indices_size == 0) {
    return true;
  }

  MS_ERROR_IF_NULL(embedding_device_cache_);
  MS_ERROR_IF_NULL(embedding_host_cache_);

  auto device_cache_device_to_host_index = embedding_device_cache_->device_to_host_index.get();
  auto host_cache_device_to_host_index = embedding_host_cache_->device_to_host_index.get();
  MS_ERROR_IF_NULL(device_cache_device_to_host_index);
  MS_ERROR_IF_NULL(host_cache_device_to_host_index);
  auto hash_table_addr = reinterpret_cast<float *>(hash_info.device_address.addr);
  auto cache_vocab_size = hash_info.cache_vocab_size;
  auto host_hash_table_addr = reinterpret_cast<float *>(hash_info.host_address.get());
  auto embedding_size = hash_info.embedding_size;
  auto swap_out_data = std::make_unique<float[]>(swap_indices_size * embedding_size);

  RETURN_IF_FALSE_WITH_LOG(
    MemcpyHostToDeviceAsync(embedding_device_cache_->hash_swap_index_addr_, device_cache_device_to_host_index,
                            swap_indices_size * sizeof(int), device_context_, stream_id_),
    "Memcpy host to device asynchronously failed.");

  RETURN_IF_FALSE_WITH_LOG(
    LookupDeviceCache(embedding_device_cache_->hash_swap_index_addr_, hash_table_addr, swap_indices_size,
                      cache_vocab_size, embedding_size, embedding_device_cache_->hash_swap_value_addr_),
    "Lookup device cache failed.");

  RETURN_IF_FALSE_WITH_LOG(
    MemcpyDeviceToHostAsync(swap_out_data.get(), embedding_device_cache_->hash_swap_value_addr_,
                            swap_indices_size * embedding_size * sizeof(float), device_context_, stream_id_),
    "Memcpy device to host asynchronously failed.");

  MS_ERROR_IF_NULL(device_context_);
  MS_ERROR_IF_NULL(device_context_->device_res_manager_);
  RETURN_IF_FALSE_WITH_LOG(device_context_->device_res_manager_->SyncStream(stream_id_), "Synchronize stream failed.");
  RETURN_IF_FALSE_WITH_LOG(
    actor_->InsertLocalHostCache(embedding_size, IntToSize(swap_indices_size), host_cache_device_to_host_index,
                                 swap_out_data.get(), host_hash_table_addr),
    "Insert local host cache failed.");
  return true;
}

bool DeviceEmbeddingOperation::UpdateDeviceCache(void *indices, void *update_value, size_t indices_num,
                                                 size_t cache_size, size_t embedding_size, void *embedding_cache) {
  MS_ERROR_IF_NULL(indices);
  MS_ERROR_IF_NULL(update_value);
  MS_ERROR_IF_NULL(embedding_cache);
  MS_ERROR_IF_NULL(embedding_cache_update_node_);

  // 1. Update parameter nodes' shape.
  auto input_param_node = common::AnfAlgo::GetInputNode(embedding_cache_update_node_, kInputIndexZero);
  MS_ERROR_IF_NULL(input_param_node);
  const ShapeVector input_param_shape = {SizeToLong(cache_size), SizeToLong(embedding_size)};
  auto input_param_abstract = std::make_shared<abstract::AbstractTensor>(kFloat32, input_param_shape);
  input_param_node->set_abstract(input_param_abstract);

  auto input_indices_node = common::AnfAlgo::GetInputNode(embedding_cache_update_node_, kInputIndexOne);
  MS_ERROR_IF_NULL(input_indices_node);
  const ShapeVector input_indices_shape = {SizeToLong(indices_num)};
  auto input_indices_abstract = std::make_shared<abstract::AbstractTensor>(kInt32, input_indices_shape);
  input_indices_node->set_abstract(input_indices_abstract);

  auto update_values_node = common::AnfAlgo::GetInputNode(embedding_cache_update_node_, kInputIndexTwo);
  MS_ERROR_IF_NULL(update_values_node);
  const ShapeVector update_values_shape = {SizeToLong(indices_num), SizeToLong(embedding_size)};
  auto update_values_abstract = std::make_shared<abstract::AbstractTensor>(kFloat32, update_values_shape);
  update_values_node->set_abstract(update_values_abstract);

  // 2. Infer shape for embedding cache update kernel(operator name: 'ScatterUpdate') which is dynamic shape kernel.
  if (!InferOpShape(embedding_cache_update_node_)) {
    MS_LOG(ERROR) << "Infer operator shape failed, op name: " << embedding_cache_update_node_->fullname_with_scope();
    return false;
  }

  // 3. Do update cache on device.
  AddressPtrList kernel_inputs = {
    std::make_shared<Address>(embedding_cache, cache_size * embedding_size * sizeof(float)),
    std::make_shared<Address>(indices, indices_num * sizeof(int)),
    std::make_shared<Address>(update_value, indices_num * embedding_size * sizeof(float))};
  AddressPtrList kernel_outputs = {
    std::make_shared<Address>(embedding_cache, cache_size * embedding_size * sizeof(float))};

  MS_ERROR_IF_NULL(device_context_);
  MS_ERROR_IF_NULL(device_context_->kernel_executor_);
  auto ret = device_context_->kernel_executor_->LaunchKernel(embedding_cache_update_node_, kernel_inputs, {},
                                                             kernel_outputs, stream_id_);
  if (!ret) {
    MS_LOG(ERROR) << "Launch kernel: " << embedding_cache_update_node_->fullname_with_scope() << " failed.";
    return false;
  }
  return true;
}

bool DeviceEmbeddingOperation::ParseHostDataHostToDevice(int id, size_t data_step, size_t graph_running_step,
                                                         bool *host_cache_need_wait_graph) {
  MS_ERROR_IF_NULL(embedding_host_cache_);
  int *host_to_device_index = embedding_host_cache_->host_to_device_index.get();
  MS_ERROR_IF_NULL(host_to_device_index);
  auto &host_hash_map = embedding_host_cache_->host_hash_map_;
  MS_ERROR_IF_NULL(host_hash_map);

  const auto &hash_id_to_index = host_hash_map->hash_id_to_index();
  const auto &iter = hash_id_to_index.find(id);
  if (iter != hash_id_to_index.end()) {
    auto index = iter->second;
    if (host_hash_map->hash_step(index) != data_step) {
      host_hash_map->set_hash_step(index, data_step);
    }
    host_to_device_index[statistics_info_->host_to_device_size_ - 1] = index;
  } else {
    int *host_to_server_index = embedding_host_cache_->host_to_server_index.get();
    int *host_to_server_ids = embedding_host_cache_->host_to_server_ids.get();
    while (true) {
      // Calculate the mapping of id to index.
      auto index = host_hash_map->ParseData(id, host_to_server_index, host_to_server_ids, data_step, graph_running_step,
                                            &(statistics_info_->host_to_server_size_), host_cache_need_wait_graph);
      if (index == INVALID_INDEX_VALUE) {
        RETURN_IF_FALSE_WITH_LOG(actor_->WaitGraphRun(), "Wait graph run failed.");
        continue;
      }
      host_to_device_index[statistics_info_->host_to_device_size_ - 1] = index;

      // This feature id has never been seen before, so it's value is initialized using the local random generator.
      if (initialized_ids_.find(id) == initialized_ids_.end()) {
        int *new_id_index = embedding_host_cache_->new_id_index.get();
        MS_ERROR_IF_NULL(new_id_index);
        new_id_index[statistics_info_->new_id_size_] = index;
        initialized_ids_.insert(id);
        // This feature id has been initialized already, so it's latest value has been kept in the remote server.
      } else {
        int *server_to_host_index = embedding_host_cache_->server_to_host_index.get();
        int *server_to_host_ids = embedding_host_cache_->server_to_host_ids.get();
        MS_ERROR_IF_NULL(server_to_host_index);
        MS_ERROR_IF_NULL(server_to_host_ids);
        server_to_host_index[statistics_info_->server_to_host_size_] = index;
        server_to_host_ids[statistics_info_->server_to_host_size_++] = id;
      }
      break;
    }
  }

  return true;
}

bool DeviceEmbeddingOperation::ParseHostDataDeviceToHost(size_t data_step, size_t graph_running_step,
                                                         bool *host_cache_need_wait_graph) {
  MS_ERROR_IF_NULL(embedding_device_cache_);
  MS_ERROR_IF_NULL(embedding_host_cache_);
  int *device_to_host_ids = embedding_device_cache_->device_to_host_ids.get();
  int *device_to_host_index = embedding_host_cache_->device_to_host_index.get();
  MS_ERROR_IF_NULL(device_to_host_ids);
  MS_ERROR_IF_NULL(device_to_host_index);

  auto &host_hash_map = embedding_host_cache_->host_hash_map_;
  MS_ERROR_IF_NULL(host_hash_map);
  int swap_device_to_host_id = device_to_host_ids[statistics_info_->device_to_host_size_ - 1];
  const auto &hash_id_to_index = host_hash_map->hash_id_to_index();
  const auto &iter = hash_id_to_index.find(swap_device_to_host_id);
  if (iter != hash_id_to_index.end()) {
    auto index = iter->second;
    if (host_hash_map->hash_step(index) != data_step) {
      host_hash_map->set_hash_step(index, data_step);
    }
    device_to_host_index[statistics_info_->device_to_host_size_ - 1] = index;
  } else {
    int *host_to_server_index = embedding_host_cache_->host_to_server_index.get();
    int *host_to_server_ids = embedding_host_cache_->host_to_server_ids.get();
    while (true) {
      // Calculate the mapping of id to index.
      auto index = host_hash_map->ParseData(swap_device_to_host_id, host_to_server_index, host_to_server_ids, data_step,
                                            graph_running_step, &statistics_info_->host_to_server_size_,
                                            host_cache_need_wait_graph);
      if (index == INVALID_INDEX_VALUE) {
        RETURN_IF_FALSE_WITH_LOG(actor_->WaitGraphRun(), "Wait graph run");
        continue;
      }
      device_to_host_index[statistics_info_->device_to_host_size_ - 1] = index;
      break;
    }
  }

  return true;
}
bool DeviceEmbeddingOperation::MemcpyHostToDeviceAsync(void *dst, const void *src, size_t size,
                                                       const DeviceContext *device_context, size_t stream_id) {
  MS_ERROR_IF_NULL(dst);
  MS_ERROR_IF_NULL(src);
  MS_ERROR_IF_NULL(device_context);
  MS_ERROR_IF_NULL(device_context->device_res_manager_);

  void *device_ptr = dst;
  const void *host_ptr = src;

  auto device_address = device_context->device_res_manager_->CreateDeviceAddress(device_ptr, size, kOpFormat_DEFAULT,
                                                                                 kTypeUnknown, ShapeVector());
  MS_ERROR_IF_NULL(device_address);
  RETURN_IF_FALSE_WITH_LOG(device_address->AsyncHostToDevice({}, size, kTypeUnknown, host_ptr, stream_id),
                           "Async memcpy host to device failed.");

  return true;
}

bool DeviceEmbeddingOperation::MemcpyDeviceToHostAsync(void *dst, const void *src, size_t size,
                                                       const DeviceContext *device_context, size_t stream_id) {
  MS_ERROR_IF_NULL(dst);
  MS_ERROR_IF_NULL(src);
  MS_ERROR_IF_NULL(device_context);
  MS_ERROR_IF_NULL(device_context->device_res_manager_);

  void *device_ptr = const_cast<void *>(src);
  void *host_ptr = dst;

  auto device_address = device_context->device_res_manager_->CreateDeviceAddress(device_ptr, size, kOpFormat_DEFAULT,
                                                                                 kTypeUnknown, ShapeVector());
  MS_ERROR_IF_NULL(device_address);
  RETURN_IF_FALSE_WITH_LOG(device_address->AsyncDeviceToHost({}, size, kTypeUnknown, host_ptr, stream_id),
                           "Async memcpy device to host failed.");

  return true;
}

ParameterPtr DeviceEmbeddingOperation::NewParameter(const KernelGraphPtr &graph, TypePtr type,
                                                    const ShapeVector &shape) {
  MS_EXCEPTION_IF_NULL(graph);
  MS_EXCEPTION_IF_NULL(type);

  auto param = graph->NewParameter();
  MS_EXCEPTION_IF_NULL(param);
  auto abstract = std::make_shared<abstract::AbstractTensor>(type, shape);
  param->set_abstract(abstract);

  auto kernel_build_info_builder = std::make_shared<kernel::KernelBuildInfo::KernelBuildInfoBuilder>();
  std::vector<std::string> formats = {kOpFormat_DEFAULT};
  std::vector<TypeId> types = {type->type_id()};
  kernel_build_info_builder->SetOutputsFormat(formats);
  kernel_build_info_builder->SetOutputsDeviceType(types);
  AnfAlgo::SetSelectKernelBuildInfo(kernel_build_info_builder->Build(), param.get());

  auto mutable_inputs = graph->MutableInputs();
  MS_EXCEPTION_IF_NULL(mutable_inputs);
  mutable_inputs->push_back(param);

  return param;
}

ValueNodePtr DeviceEmbeddingOperation::NewValueNode(int64_t value, const DeviceContext *device_context,
                                                    size_t stream_id) {
  MS_EXCEPTION_IF_NULL(device_context);

  auto tensor = std::make_shared<tensor::Tensor>(static_cast<int64_t>(value), kInt32);
  auto value_node = mindspore::NewValueNode(tensor);
  value_node->set_abstract(tensor->ToAbstract());

  // Create kernel build info.
  auto kernel_build_info_builder = std::make_shared<kernel::KernelBuildInfo::KernelBuildInfoBuilder>();
  std::vector<std::string> formats = {kOpFormat_DEFAULT};
  std::vector<TypeId> types = {kInt32->type_id()};
  kernel_build_info_builder->SetOutputsFormat(formats);
  kernel_build_info_builder->SetOutputsDeviceType(types);

  auto kernel_info = std::make_shared<device::KernelInfo>();
  MS_EXCEPTION_IF_NULL(kernel_info);
  value_node->set_kernel_info(kernel_info);
  AnfAlgo::SetSelectKernelBuildInfo(kernel_build_info_builder->Build(), value_node.get());

  // Create device address.
  size_t output_idx = 0;
  size_t tensor_size = AnfAlgo::GetOutputTensorMemSize(value_node, output_idx);
  TypeId output_type_id = AnfAlgo::GetOutputDeviceDataType(value_node, output_idx);
  std::string output_format = AnfAlgo::GetOutputFormat(value_node, output_idx);

  MS_EXCEPTION_IF_NULL(device_context->device_res_manager_);
  auto value_addr = device_context->device_res_manager_->AllocateMemory(tensor_size);
  MS_EXCEPTION_IF_NULL(value_addr);
  auto address = device_context->device_res_manager_->CreateDeviceAddress(
    value_addr, tensor_size, output_format, output_type_id, trans::GetRuntimePaddingShape(value_node, output_idx));
  MS_EXCEPTION_IF_NULL(address);

  // Sync tensor value.
  MS_EXCEPTION_IF_CHECK_FAIL(address->AsyncHostToDevice({}, tensor_size, output_type_id, tensor->data_c(), stream_id),
                             "Async memcpy host to device failed.");
  MS_EXCEPTION_IF_CHECK_FAIL(device_context->device_res_manager_->SyncStream(stream_id), "Synchronize stream failed.");

  address->set_from_persistent_mem(true);
  AnfAlgo::SetOutputAddr(address, output_idx, value_node.get());

  return value_node;
}

bool DeviceEmbeddingOperation::InferOpShape(const CNodePtr &kernel) {
  MS_EXCEPTION_IF_NULL(kernel);
  opt::dynamic_shape::InferOp(kernel);
  auto args = kernel::GetArgsFromCNode(kernel);
  MS_EXCEPTION_IF_NULL(args);

  auto kernel_mod = AnfAlgo::GetKernelMod(kernel);
  MS_EXCEPTION_IF_NULL(kernel_mod);
  if (kernel::KRET_OK != kernel_mod->Resize(args->op, args->inputs, args->outputs, args->depend_tensor_map)) {
    MS_LOG(ERROR) << "Kernel " << kernel->fullname_with_scope() << " resize failed.";
    return false;
  }
  return true;
}
}  // namespace runtime
}  // namespace mindspore
