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
#ifndef MINDSPORE_LITE_TOOLS_CONVERTER_QUANTIZER_QUANT_HELPER_QUANT_TYPE_DETERMINER_H
#define MINDSPORE_LITE_TOOLS_CONVERTER_QUANTIZER_QUANT_HELPER_QUANT_TYPE_DETERMINER_H

#include <utility>
#include "ir/anf.h"
#include "tools/converter/quantizer/quantize_util.h"

namespace mindspore::lite::quant {
class QuantTypeDeterminer {
 public:
  explicit QuantTypeDeterminer(const FuncGraphPtr &func_graph) : func_graph_(func_graph) {}
  ~QuantTypeDeterminer() = default;

  int Determine();

 private:
  bool DetermineQuantAll(const CNodePtr &cnode);
  std::pair<size_t, size_t> GetQuantParamsNum(const QuantParamHolderPtr &quant_holder);
  bool DetermineQuantWeight(const CNodePtr &cnode);

 private:
  FuncGraphPtr func_graph_ = nullptr;
};
}  // namespace mindspore::lite::quant
#endif  // MINDSPORE_LITE_TOOLS_CONVERTER_QUANTIZER_QUANT_HELPER_QUANT_TYPE_DETERMINER_H
