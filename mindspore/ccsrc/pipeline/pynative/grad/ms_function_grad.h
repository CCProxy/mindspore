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

#ifndef MINDSPORE_MINDSPORE_CCSRC_PIPELINE_PYNATIVE_MS_FUNCTION_H_
#define MINDSPORE_MINDSPORE_CCSRC_PIPELINE_PYNATIVE_MS_FUNCTION_H_

#include <vector>
#include <memory>
#include <string>
#include "ir/anf.h"
#include "ir/tensor.h"
#include "pipeline/pynative/base.h"
#include "pipeline/pynative/grad/top_cell.h"
#include "pipeline/jit/pipeline.h"

namespace mindspore {
namespace pynative {
class TopCellInfo;
using TopCellInfoPtr = std::shared_ptr<TopCellInfo>;

class MsFunction {
 public:
  MsFunction() = default;
  ~MsFunction() = default;
  void set_graph_phase(const std::string &graph_phase) { graph_phase_ = graph_phase; }
  void MarkMsFunctionNodes(const pipeline::ResourcePtr &resource) const;
  py::object GradMsFunction(const py::object &out, const py::args &args);

 private:
  const std::string &graph_phase() const { return graph_phase_; }
  void GradMsFunctionInner(const std::string &phase, const py::object &out, const py::args &args,
                           const FuncGraphPtr &ms_func_graph, const FuncGraphPtr &grad_graph) const;
  // Update device address of value node in grad graph by forward tensors.
  void RunReplace(const CNodePtr &added_make_tuple, const std::vector<tensor::TensorPtr> &total_output_tensors,
                  const FuncGraphPtr &grad_graph) const;
  void ReplaceNewTensorsInGradGraph(const TopCellInfoPtr &top_cell, const FrontendOpRunInfoPtr &op_run_info,
                                    const ValuePtr &added_out, const FuncGraphPtr &ms_func_graph,
                                    const FuncGraphPtr &grad_graph) const;
  void UpdateMsFunctionForwardTensors(const FrontendOpRunInfoPtr &op_run_info, const ValuePtr &new_forward_value) const;
  // Make CNode for ms_function forward graph.
  void GetInputArgsNode(const py::args &args, AnfNodePtrList *input_nodes, ValuePtrList *input_values) const;
  void GetWeightsNode(const FuncGraphPtr &ms_func_graph, AnfNodePtrList *input_nodes, ValuePtrList *input_values,
                      const size_t input_args_index) const;
  void MakeCNodeForMsFunction(const FuncGraphPtr &ms_func_graph, const py::args &args, ValuePtrList *input_values,
                              CNodePtr *ms_function_cnode) const;
  // Make adjoint for ms_function fprop graph and connect it with previous op
  CNodePtr MakeAdjointForMsFunction(const FuncGraphPtr &ms_func_graph, const FuncGraphPtr &grad_graph,
                                    const py::object &actual_out, const py::args &args,
                                    const ValuePtr &actual_out_v) const;

  // The graph phase is used to obtain backend graph that is complied by ms_function
  std::string graph_phase_;
  // Stores parameter in ms_function
  std::vector<std::string> ms_function_params_;
};
}  // namespace pynative
}  // namespace mindspore

#endif  // MINDSPORE_MINDSPORE_CCSRC_PIPELINE_PYNATIVE_MS_FUNCTION_H_
