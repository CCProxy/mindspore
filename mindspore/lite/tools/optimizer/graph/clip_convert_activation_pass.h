/**
 * Copyright 2020-2021 Huawei Technologies Co., Ltd
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

#ifndef MINDSPORE_LITE_TOOLS_OPTIMIZER_GRAPH_CLIP_CONVERT_ACTIVATION_PASS_H_
#define MINDSPORE_LITE_TOOLS_OPTIMIZER_GRAPH_CLIP_CONVERT_ACTIVATION_PASS_H_
#include <string>
#include "include/backend/optimizer/pass.h"

namespace mindspore::opt {
class ClipConvertActivationPass : public Pass {
 public:
  explicit ClipConvertActivationPass(bool only_relu = false) : Pass("clip_convert_activation_pass") {
    only_relu_ = only_relu;
  }
  ~ClipConvertActivationPass() override = default;
  bool Run(const FuncGraphPtr &graph) override;

 private:
  bool only_relu_ = false;
};
}  // namespace mindspore::opt
#endif  // MINDSPORE_LITE_TOOLS_OPTIMIZER_GRAPH_CLIP_CONVERT_ACTIVATION_PASS_H_
