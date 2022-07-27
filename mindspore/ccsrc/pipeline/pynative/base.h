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

#ifndef MINDSPORE_CCSRC_PIPELINE_PYNATIVE_BASE_H_
#define MINDSPORE_CCSRC_PIPELINE_PYNATIVE_BASE_H_

#include <vector>
#include <utility>
#include <string>
#include <memory>
#include <set>

#include "utils/hash_map.h"
#include "utils/hash_set.h"
#include "pybind11/pytypes.h"
#include "ir/anf.h"
#include "pybind_api/ir/primitive_py.h"
#include "pipeline/jit/parse/parse.h"
#include "abstract/abstract_value.h"

namespace mindspore {
namespace pynative {
namespace py = pybind11;

enum RunOpArgsEnum { PY_PRIM = 0, PY_NAME, PY_INPUTS, PY_ARGS_NUM };
struct OpExecInfo {
  bool is_nop_prim = false;
  bool is_dynamic_shape = false;
  bool is_mixed_precision_cast = false;
  size_t next_input_index = 0;
  std::string op_name;
  std::string op_info;
  std::string next_op_name;
  PrimitivePyPtr py_primitive;
  AbstractBasePtr abstract;
  py::list op_inputs;
  // dynamic shape
  bool has_dynamic_input = false;
  bool has_dynamic_output = false;
  // Tensor id with Shape info
  mindspore::HashMap<std::string, abstract::ShapePtr> id_with_dynamic_shape;
  // Tensor input and with its value
  std::vector<std::pair<size_t, ValuePtr>> index_with_value;
  std::vector<tensor::TensorPtr> input_tensors;
  py::dict op_attrs;
  std::vector<int64_t> inputs_mask;
  bool lazy_build = false;
};
using OpExecInfoPtr = std::shared_ptr<OpExecInfo>;
}  // namespace pynative
}  // namespace mindspore

#endif  // MINDSPORE_CCSRC_PIPELINE_PYNATIVE_BASE_H_
