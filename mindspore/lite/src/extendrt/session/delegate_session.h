/**
 * Copyright 2019-2021 Huawei Technologies Co., Ltd
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
#ifndef MINDSPORE_LITE_EXTENDRT_SESSION_DELEGATE_SESSION_H_
#define MINDSPORE_LITE_EXTENDRT_SESSION_DELEGATE_SESSION_H_

#include <vector>
#include <memory>
#include <string>

#include "extendrt/infer_session.h"

namespace mindspore {
class DelegateSession : public InferSession {
 public:
  DelegateSession() = default;
  explicit DelegateSession(std::shared_ptr<mindspore::Delegate> delegate) : delegate_(delegate) {}
  virtual ~DelegateSession() = default;

  Status Init(const std::shared_ptr<Context> context) override;
  Status CompileGraph(FuncGraphPtr graph, const void *data = nullptr, size_t size = 0) override;
  Status RunGraph() override;
  Status RunGraph(const std::vector<tensor::Tensor> &inputs, std::vector<tensor::Tensor> *outputs) override;
  Status Resize(const std::vector<tensor::TensorPtr> &inputs, const std::vector<std::vector<int64_t>> &dims) override;

  std::vector<MutableTensorImplPtr> GetOutputs() override;
  std::vector<MutableTensorImplPtr> GetInputs() override;
  std::vector<std::string> GetOutputNames() override;
  std::vector<std::string> GetInputNames() override;
  MutableTensorImplPtr GetOutputByTensorName(const std::string &tensorName) override;
  MutableTensorImplPtr GetInputByTensorName(const std::string &name) override;

 private:
  std::shared_ptr<mindspore::Delegate> delegate_;
};
}  // namespace mindspore

#endif  // MINDSPORE_LITE_EXTENDRT_SESSION_DELEGATE_SESSION_H_
