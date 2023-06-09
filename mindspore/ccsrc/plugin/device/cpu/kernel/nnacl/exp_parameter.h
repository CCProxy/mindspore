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
#ifndef MINDSPORE_NNACL_EXP_PARAMETER_H_
#define MINDSPORE_NNACL_EXP_PARAMETER_H_

#include "nnacl/op_base.h"

typedef struct ExpParameter {
  OpParameter op_parameter_;
  float base_;
  float scale_;
  float shift_;

  /* micro */
  float in_scale_;
  float out_scale_;
  int element_num_;
} ExpParameter;

#endif  // MINDSPORE_NNACL_EXP_PARAMETER_H_
