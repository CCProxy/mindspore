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
#ifndef MINDSPORE_LITE_SRC_EXTENDRT_DELEGATE_TENSORRT_TENSORRT_DELEGATE_H_
#define MINDSPORE_LITE_SRC_EXTENDRT_DELEGATE_TENSORRT_TENSORRT_DELEGATE_H_
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <set>
#include <map>
#include "include/api/delegate.h"
#include "src/extendrt/delegate/tensorrt/tensorrt_subgraph.h"
#include "src/extendrt/delegate/parameter_cache/embedding_cache_manager.h"
#include "include/api/kernel.h"
#include "include/errorcode.h"
#include "src/common/log_adapter.h"
#include "include/api/context.h"
#include "core/base/base.h"

#include "extendrt/session/lite_graph_executor.h"
#include "ccsrc/backend/common/session/kernel_graph.h"

namespace mindspore::lite {
struct TrtGraphContext {
  std::vector<TensorRTOp *> tensorrt_ops;
  std::vector<TensorInfo> inputs;
  std::vector<TensorInfo> outputs;
  std::shared_ptr<TensorRTSubGraph> sub_graph = nullptr;
};

class TensorRTExecutor : public LiteGraphExecutor {
 public:
  TensorRTExecutor(const std::shared_ptr<mindspore::Context> &context, const std::string &cache_model_path,
                   size_t vocab_size, size_t device_cache_size, const std::map<std::string, std::string> &ms_cache);
  explicit TensorRTExecutor(const std::shared_ptr<mindspore::Context> &context);

  ~TensorRTExecutor() override;

  Status Init();

  bool CompileGraph(const FuncGraphPtr &graph, const std::map<string, string> &compile_options) override;
  bool RunGraph(const FuncGraphPtr &graph, const std::vector<tensor::Tensor> &inputs,
                std::vector<tensor::Tensor> *outputs, const std::map<string, string> &compile_options) override;

  bool Resize(const FuncGraphPtr &, const std::vector<tensor::Tensor> &inputs,
              const std::vector<std::vector<int64_t>> &new_shapes) override;
  std::vector<tensor::Tensor> GetInputInfos(const FuncGraphPtr &) override;
  std::vector<tensor::Tensor> GetOutputInfos(const FuncGraphPtr &) override;

 private:
  Status BuildSubGraph(const KernelGraphPtr &graph);
  Status UpdateTrtSubGraphInputsDepend();

  TensorRTOp *FindTensorRTOp(const CNodePtr &cnode, const BaseOperatorPtr &base_operator,
                             const std::vector<TensorInfo> &input_tensors,
                             const std::vector<TensorInfo> &output_tensors);

  std::shared_ptr<TensorRTSubGraph> CreateTensorRTGraph(const std::vector<TensorRTOp *> &ops,
                                                        const KernelGraphPtr &graph, int index,
                                                        const std::vector<TensorInfo> &inputs,
                                                        const std::vector<TensorInfo> &outputs);

  std::shared_ptr<mindspore::Context> context_{nullptr};
  std::shared_ptr<GPUDeviceInfo> device_info_{nullptr};
  TensorRTRuntime *runtime_{nullptr};
  bool support_hw_resize_{true};
  bool support_resize_{true};
  const std::string cache_model_path_;
  size_t vocab_size_{0};
  size_t device_cache_size_{0};
  std::string serialize_path_;
  cudaStream_t stream_{nullptr};
  std::vector<kernel::Kernel> kernel_list_;

  std::vector<TrtGraphContext> tensorrt_graph_list_;

  std::vector<nvinfer1::Dims> min_dims_;
  std::vector<nvinfer1::Dims> opt_dims_;
  std::vector<nvinfer1::Dims> max_dims_;

  std::vector<TensorInfo> inputs_;
  std::vector<TensorInfo> outputs_;
};
}  // namespace mindspore::lite
#endif  // MINDSPORE_LITE_SRC_RUNTIME_DELEGATE_TENSORRT_DELEGATE_
