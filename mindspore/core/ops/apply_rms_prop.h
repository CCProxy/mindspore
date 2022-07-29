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

#ifndef MINDSPORE_CORE_OPS_APPLY_RMS_PROP_H_
#define MINDSPORE_CORE_OPS_APPLY_RMS_PROP_H_
#include <vector>
#include <memory>
#include <string>
#include <set>
#include <map>

#include "ops/base_operator.h"
#include "mindapi/base/types.h"

namespace mindspore {
namespace ops {
constexpr auto kNameApplyRMSProp = "ApplyRMSProp";
class MIND_API ApplyRMSProp : public BaseOperator {
 public:
  MIND_API_BASE_MEMBER(ApplyRMSProp);
  double get_attr(const char *attr) const;
  ApplyRMSProp() : BaseOperator(kNameApplyRMSProp) {
    InitIOName({"var", "mean_square", "moment", "learning_rate", "grad", "decay", "momentum", "epsilon"},
               {"var", "mean_square", "moment"});
  }
};
abstract::AbstractBasePtr ApplyRMSPropInfer(const abstract::AnalysisEnginePtr &, const PrimitivePtr &primitive,
                                            const std::vector<abstract::AbstractBasePtr> &input_args);
using kPrimApplyRMSPropPtr = std::shared_ptr<ApplyRMSProp>;
}  // namespace ops
}  // namespace mindspore
#endif  // MINDSPORE_CORE_OPS_APPLY_RMS_PROP_H_
