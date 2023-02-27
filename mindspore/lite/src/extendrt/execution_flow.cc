/**
 * Copyright 2023 Huawei Technologies Co., Ltd
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

#include <vector>

#include "extendrt/execution_flow.h"

#include "litert/lite_kernel.h"
#include "litert/kernel_exec_util.h"
#include "litert/sub_graph_kernel.h"

namespace mindspore::infer {
ExecutionFlow::~ExecutionFlow() {
  for (auto tensor : inputs_) {
    if (tensor != nullptr) {
      delete tensor;
    }
  }
  for (auto tensor : outputs_) {
    if (tensor != nullptr) {
      delete tensor;
    }
  }
  for (auto kernel : kernels_) {
    if (kernel != nullptr) {
      delete kernel;
    }
  }
}

abstract::Kernel *ExecutionFlow::ConstructFusionKernel() {
  auto lite_kernel = new (std::nothrow) mindspore::kernel::LiteKernel(nullptr, inputs_, outputs_, context_);
  if (lite_kernel == nullptr) {
    MS_LOG(ERROR) << "ExecutionFlow::ConstructFusionKernel create lite kernel failed, may be memory is not enough";
    return nullptr;
  }

  std::vector<KernelExec *> input_kernels = mindspore::kernel::KernelExecUtil::SubgraphInputNodes(kernels_);
  std::vector<KernelExec *> output_kernels = mindspore::kernel::KernelExecUtil::SubgraphOutputNodes(kernels_);

  mindspore::kernel::SubGraphKernel *sub_graph_kernel =
    new CpuFp32SubGraph(input_kernels, output_kernels, kernels_, lite_kernel);
  if (sub_graph_kernel == nullptr) {
    MS_LOG(ERROR) << "ExecutionFlow::ConstructFusionKernel create sub graph kernel failed, may be memory is not enough";
    delete lite_kernel return nullptr;
  }
  sub_graph_kernel->set_context(context_);
  return sub_graph_kernel;
}
}  // namespace mindspore::infer
