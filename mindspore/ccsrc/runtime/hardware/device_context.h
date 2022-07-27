/**
 * Copyright 2021-2022 Huawei Technologies Co., Ltd
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

#ifndef MINDSPORE_CCSRC_RUNTIME_HARDWARE_DEVICE_CONTEXT_H_
#define MINDSPORE_CCSRC_RUNTIME_HARDWARE_DEVICE_CONTEXT_H_

#include <string>
#include <vector>
#include <memory>
#include <map>
#include "runtime/hardware/device_type.h"
#include "runtime/device/device_address.h"
#include "runtime/device/bucket.h"
#include "runtime/collective/collective_communication_lib.h"
#include "runtime/collective/collective_comm_lib_loader.h"
#include "backend/common/session/kernel_graph.h"
#include "backend/common/session/anf_runtime_algorithm.h"
#include "include/common/utils/anfalgo.h"
#include "backend/common/optimizer/common_backend_optimization.h"

namespace mindspore {
namespace device {
using mindspore::kernel::AddressPtr;
using mindspore::kernel::KernelMod;

const size_t kDeviceContextsNumOne = 1;
const size_t kDeviceContextsNumTwo = 2;

struct DeviceContextKey {
  // device type name, such as 'GPU' 'Ascend' 'CPU'.
  std::string device_name_;
  uint32_t device_id_{0};

  // Use the result of ToString() as key to look up DeviceContext
  // in cache map which maintains created DeviceContext objects.
  std::string ToString() const { return device_name_ + "_" + std::to_string(device_id_); }
};

class DeviceResManager;
class GraphExecutor;
class KernelExecutor;

// DeviceContext is unified interface of interaction with device.
class DeviceContext {
 public:
  explicit DeviceContext(const DeviceContextKey &device_context_key) : device_context_key_(device_context_key) {}
  virtual ~DeviceContext() = default;

  // Initialize the device context.
  virtual void Initialize() = 0;

  // Destroy device context and release device resource.
  virtual void Destroy() {}

  // Analysis the function graph to check whether all nodes are supported, if yes, return true, if no, return false and
  // mark the unsupported node as "NotSupport" through SetCNodeNotSupported()
  virtual bool PartitionGraph(const FuncGraphPtr &func_graph) const { return false; }

  // Analysis the function graph and select the appropriate run mode for the graph
  virtual RunMode GetRunMode(const FuncGraphPtr &func_graph) const = 0;

  // Get device_context_key_ to obtain device name and device id.
  const DeviceContextKey &device_context_key() const { return device_context_key_; }

  // Get device address type according different device type, such GPU, Ascend.
  DeviceType GetDeviceType() const { return GetDeviceTypeByName(device_context_key_.device_name_); }

  DeviceContextKey device_context_key_;
  std::unique_ptr<DeviceResManager> device_res_manager_;
  std::unique_ptr<GraphExecutor> graph_executor_;
  std::unique_ptr<KernelExecutor> kernel_executor_;
};
using DeviceContextPtr = std::shared_ptr<DeviceContext>;

class DeviceResManager {
 public:
  DeviceResManager() : collective_comm_lib_(nullptr), device_context_(nullptr) {}
  virtual ~DeviceResManager() = default;

  // Initialize the device resource manager.
  virtual void Initialize() {}

  // Destroy device resource manager and release device resource.
  virtual void Destroy() {}

  // Bind device to current thread to gain device control privileges
  virtual bool BindDeviceToCurrentThread() const { return true; }

  // Relevant function to allocate and free device memory of raw ptr.
  virtual void *AllocateMemory(size_t size) const = 0;
  virtual void FreeMemory(void *ptr) const = 0;

  // Relevant function to allocate and free device memory of DeviceAddress.
  virtual bool AllocateMemory(DeviceAddress *const &address) const;
  virtual void FreeMemory(DeviceAddress *const &address) const;

  // Allocate continuous device memory according to size list.
  // Communication operators may need continuous memory for input and output
  // to optimize the communication performance.
  virtual std::vector<void *> AllocateContinuousMemory(const std::vector<size_t> &size_list) const {
    MS_LOG(EXCEPTION) << "Unimplemented interface.";
  }

  // Create concrete device address according different device type.
  virtual DeviceAddressPtr CreateDeviceAddress(void *const device_ptr, size_t device_size, const string &format,
                                               TypeId type_id, const ShapeVector &shape) const = 0;

  // Create a stream with assigning a stream id, the assigned stream id will be written to the variable '*stream_id';
  bool CreateStream(size_t *stream_id);

  // Synchronize stream, device such as GPU and Ascend need stream to launch kernel asynchronously,
  // using 'SyncStream' to block thread and wait for completing all tasks in stream.
  // Devices that do not need stream could ignore the implementation of this function.
  // Since the current entry for creating streams is not unified, the implementation of the 'SyncStream'
  // interface is implemented by subclasses.
  virtual bool SyncStream(size_t stream_id = 0) const { return true; }

  // Destroy all streams created by 'CreateStream'.
  bool DestroyAllStreams();

  // Get physical stream based on logical stream id.
  void *GetStream(size_t stream_id) const;

  // Dynamically load collective communication library.
  // Currently, four types are supported: OpenMPI and self developed framework for CPU. NCCL for GPU. HCCL for Ascend.
  virtual bool LoadCollectiveCommLib() { return true; }

  // Return collective communication object for caller to access
  CollectiveCommunicationLib *collective_comm_lib() const { return collective_comm_lib_; }

 protected:
  // Create a stream on the device of this device context.
  virtual bool CreateStream(void **stream) const { return true; }
  // Destroy a stream on the device of this device context.
  virtual bool DestroyStream(void *stream) const { return true; }

  // Record stream ids to stream address, key: stream id, value: address of stream.
  std::map<size_t, void *> stream_ids_;

  // Ensure the thread safety for creating stream.
  std::mutex stream_mutex_;

  // Ensure the thread safety for allocating device memory.
  mutable std::mutex alloc_mem_mutex_;

  // The collective communication library.
  CollectiveCommunicationLib *collective_comm_lib_;

  DeviceContext *device_context_;

 private:
  template <class... Args>
  friend class DeviceInterface;

  void SetDeviceContext(DeviceContext *device_context) { device_context_ = device_context; }
};

class GraphExecutor {
 public:
  virtual ~GraphExecutor() = default;
  virtual bool CompileGraph(const FuncGraphPtr &graph, const std::map<string, string> &compile_options) { return true; }
  virtual bool RunGraph(const FuncGraphPtr &graph, const std::vector<tensor::Tensor> &inputs,
                        std::vector<tensor::Tensor> *outputs, const std::map<string, string> &compile_options) {
    MS_LOG(EXCEPTION) << "Unimplemented interface.";
  }

 protected:
  DeviceContext *device_context_;

 private:
  template <class... Args>
  friend class DeviceInterface;

  void SetDeviceContext(DeviceContext *device_context) { device_context_ = device_context; }
};

class KernelExecutor {
 public:
  virtual ~KernelExecutor() = default;

  // Optimize the kernel graph for graph mode.
  virtual void OptimizeGraph(const FuncGraphPtr &graph) const {}

  // Generate 'KernelMod' for all kernels and set 'KernelMod' into kernel,
  // 'KernelMod' is real executive object of kernel.
  virtual void CreateKernel(const std::vector<CNodePtr> &nodes) const {}

  // Adjust kernel graph before run graph.
  virtual void PreprocessBeforeRun(const FuncGraphPtr &graph) const {}

  // Launch a kernel via 'KernelMod' of the kernel.
  virtual bool LaunchKernel(const CNodePtr &kernel, const std::vector<AddressPtr> &inputs,
                            const std::vector<AddressPtr> &workspace, const std::vector<AddressPtr> &outputs) const {
    MS_LOG(EXCEPTION) << "Unimplemented interface.";
  }

 protected:
  DeviceContext *device_context_;

 private:
  template <class... Args>
  friend class DeviceInterface;

  void SetDeviceContext(DeviceContext *device_context) { device_context_ = device_context; }
};

class DeprecatedKernelExecutor : public KernelExecutor {
 public:
  // Unify the MindIR, the default behavior uses the common unified MindIR.
  // It is deprecated and will be removed in a future version
  virtual void UnifyMindIR(const KernelGraphPtr &graph) const { opt::CommonUnifyMindIR(graph); }

  // Get rank id for distributed training.
  // It is deprecated and will be removed in a future version
  virtual uint32_t GetRankID() const { return 0; }

  // Create and initialize bucket for every allreduce operator. Bucket is used in PyNative distributed training mode,
  // one bucket handles all resource to launch and sync allreduce operator.
  // It is deprecated and will be removed in a future version
  virtual std::shared_ptr<Bucket> CreateBucket(uint32_t bucket_id, uint32_t bucket_size) const { return nullptr; }
};

template <class... Args>
class DeviceInterface : public DeviceContext {};

template <>
class DeviceInterface<> : public DeviceContext {
 public:
  explicit DeviceInterface(const DeviceContextKey &key) : DeviceContext(key) {}

 protected:
  void CheckUnset(void *ptr, const std::string &error_msg) {
    if (ptr != nullptr) {
      MS_LOG(EXCEPTION) << error_msg;
    }
  }
};

template <class T, class... Args>
class DeviceInterface<T, Args...> : public DeviceInterface<Args...> {
 public:
  explicit DeviceInterface(const DeviceContextKey &key) : DeviceInterface<Args...>(key) {
    if constexpr (std::is_base_of_v<DeviceResManager, T>) {
      DeviceInterface::CheckUnset(reinterpret_cast<void *>(DeviceContext::device_res_manager_.get()),
                                  "DeviceResManager has been registered!");
      DeviceContext::device_res_manager_ = std::make_unique<T>();
      DeviceContext::device_res_manager_->SetDeviceContext(this);
    } else if constexpr (std::is_base_of_v<GraphExecutor, T>) {
      DeviceInterface::CheckUnset(reinterpret_cast<void *>(DeviceContext::graph_executor_.get()),
                                  "GraphExecutor has been registered!");
      DeviceContext::graph_executor_ = std::make_unique<T>();
      DeviceContext::graph_executor_->SetDeviceContext(this);
    } else if constexpr (std::is_base_of_v<KernelExecutor, T>) {
      DeviceInterface::CheckUnset(reinterpret_cast<void *>(DeviceContext::kernel_executor_.get()),
                                  "KernelExecutor has been registered!");
      DeviceContext::kernel_executor_ = std::make_unique<T>();
      DeviceContext::kernel_executor_->SetDeviceContext(this);
    }
  }

 private:
  template <typename = std::enable_if_t<std::is_base_of_v<DeviceResManager, T> || std::is_base_of_v<GraphExecutor, T> ||
                                        std::is_base_of_v<KernelExecutor, T>>>
  void Assert() {}
};
}  // namespace device
}  // namespace mindspore
#endif  // MINDSPORE_CCSRC_RUNTIME_HARDWARE_DEVICE_CONTEXT_H_
