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

#include "src/extendrt/kernel/default/lite_kernel_mod.h"
#include <string>
#include "src/extendrt/utils/tensor_utils.h"

using mindspore::lite::RET_ERROR;
using mindspore::lite::RET_OK;

namespace mindspore::kernel {
int LiteKernelMod::Prepare() {
  if (!InferShapeDone()) {
    return RET_OK;
  }
  auto inputs = CloudTensorUtils::LiteTensorToKernelTensorPtrVec(in_tensors_);
  auto outputs = CloudTensorUtils::LiteTensorToKernelTensorPtrVec(out_tensors_);

  bool ret = kernel_mod_->Init(this->base_operator_, inputs, outputs);
  return ret ? ReSize() : RET_ERROR;
}

int LiteKernelMod::ReSize() {
  auto inputs = CloudTensorUtils::LiteTensorToKernelTensorPtrVec(in_tensors_);
  auto outputs = CloudTensorUtils::LiteTensorToKernelTensorPtrVec(out_tensors_);
  return kernel_mod_->Resize(base_operator_, inputs, outputs);
}

int LiteKernelMod::Run() {
  auto inputs = CloudTensorUtils::LiteTensorToAddressPtrVec(in_tensors_);
  auto outputs = CloudTensorUtils::LiteTensorToAddressPtrVec(out_tensors_);

  AddressPtrList workspace;
  auto workspace_size = kernel_mod_->GetWorkspaceSizeList();
  for (size_t i = 0; i < workspace_size.size(); i++) {
    auto buffer = ms_context_->allocator->Malloc(workspace_size.at(i));
    std::shared_ptr<Address> address = std::make_shared<Address>(buffer, workspace_size.at(i));
    workspace.push_back(address);
  }

  auto ret = kernel_mod_->Launch(inputs, workspace, outputs, nullptr);

  for (auto address : workspace) {
    ms_context_->allocator->Free(address->addr);
  }
  return ret ? RET_OK : RET_ERROR;
}
}  // namespace mindspore::kernel
