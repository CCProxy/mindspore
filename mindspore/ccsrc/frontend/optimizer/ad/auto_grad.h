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

#ifndef MINDSPORE_CCSRC_FRONTEND_OPTIMIZER_AD_AUTO_GRAD_H_
#define MINDSPORE_CCSRC_FRONTEND_OPTIMIZER_AD_AUTO_GRAD_H_

#include <memory>
#include <utility>
#include <map>
#include <vector>
#include "ir/anf.h"
#include "ir/func_graph.h"

namespace mindspore {
namespace ad {
struct GradAttr {
  GradAttr(bool get_all, bool get_by_list, bool sens_param, bool get_by_position, bool weight_param_is_tuple)
      : grad_all_inputs(get_all),
        grad_weights(get_by_list),
        has_sens(sens_param),
        get_by_position(get_by_position),
        weight_param_is_tuple(weight_param_is_tuple) {}

  bool grad_all_inputs;
  bool grad_weights;
  bool has_sens;
  bool get_by_position;
  bool weight_param_is_tuple;
};

struct GradParam {
  GradParam(const CNodePtr &cnode, const ValuePtrList &op_args, const ValuePtr &out, FuncGraphPtr fprop_fg = nullptr)
      : cnode(cnode), op_args(op_args), out(out), fprop_fg(std::move(fprop_fg)) {}

  // Primal CNode create by op forward process
  const CNodePtr &cnode;
  // Input value for cnode
  const ValuePtrList &op_args;
  // Output of op
  const ValuePtr &out;
  // Bprop func graph
  const FuncGraphPtr fprop_fg;
  // High order used this, which
  bool grad_by_value = true;
};
using GradParamPtr = std::shared_ptr<GradParam>;

class FunctionNode {
 public:
  FunctionNode(const FuncGraphPtr &tape, const AnfNodePtr &dout)
      : tape_(tape), accumulate_dout_(dout), fake_dout_(dout) {}
  void AddEdge(const AnfNodePtr &next_node, const AnfNodePtr &din);
  void UpdateAccumulativeDout(const AnfNodePtr &new_dout);
  const std::vector<std::pair<AnfNodePtr, AnfNodePtr>> &next_edges() const { return next_edges_; }
  AnfNodePtr RealDout() const { return accumulate_dout_; }
  void ReplaceEdges();
  const AnfNodePtr fake_dout() const { return fake_dout_; }

 private:
  AnfNodePtr HyperAdd(const AnfNodePtr &left_node, const AnfNodePtr &right_node);
  // Bprop func graph
  const FuncGraphPtr tape_;
  // Input of dout for this bprop function
  AnfNodePtr accumulate_dout_;
  // First we generate a fake dout
  const AnfNodePtr fake_dout_;
  // Represent where thd dins backpropagate to other bprop function or variable
  std::vector<std::pair<AnfNodePtr, AnfNodePtr>> next_edges_;
  // Replace next_edges where din == dout in brprop function
  std::vector<int> need_replace_edges_;
};
using FunctionNodePtr = std::shared_ptr<FunctionNode>;

class VariableNode {
 public:
  VariableNode(const FunctionNodePtr &fn, const ValuePtr &out_value) : fn_(fn), out_value_(out_value) {}

  ValuePtr out_value() const { return out_value_; }
  FunctionNodePtr fn() const { return fn_; }
  bool is_need_grad() const { return is_need_grad_; }
  void set_is_need_grad(bool is_need_grad) { is_need_grad_ = is_need_grad; }
  AnfNodePtr k_node() const { return k_node_; }
  void set_k_node(const AnfNodePtr &k_node) { k_node_ = k_node; }

 private:
  // Abstract bprop function
  FunctionNodePtr fn_;
  ValuePtr out_value_;
  bool is_need_grad_{false};
  // k mapped cnode for primal CNode; primal CNode is owned by primal funcgraph, this is owned by tape_;
  AnfNodePtr k_node_{nullptr};
};
using VariableNodePtr = std::shared_ptr<VariableNode>;

class AutoGradCellImpl {
 public:
  using UserType = std::map<AnfNodePtr, std::vector<std::pair<CNodePtr, int>>>;
  AutoGradCellImpl(const AnfNodePtrList &cell_inputs, const std::vector<ValuePtr> &input_param_values);
  ~AutoGradCellImpl() = default;
  // Reverse connect bprop of op
  bool KPynativeOp(const CNodePtr &cnode, const ValuePtrList &op_args, const ValuePtr &out);
  // Reverse connect ms_function or higher order sub bprop funcgraph
  bool KPynativeWithFProp(const GradParamPtr &grad_param);
  CNodePtr GetBPropFromFProp(const FuncGraphPtr &fprop_fg, const AnfNodePtrList &args, const ValuePtr &out,
                             AnfNodePtr *const tape_dout);
  // Update top cell output, record last_node
  void UpdateOutputNodeOfTopCell(const AnfNodePtr &output_node, const ValuePtr &sens_out);
  // Build a back propagate funcgraph, each cnode in primal funcgraph is replaced by value node or formal cnode, so it
  // can be grad again.
  FuncGraphPtr Finish(const AnfNodePtrList &weights, const std::vector<size_t> &grad_position,
                      const GradAttr &grad_attr, bool build_formal_param);

 private:
  // Last cnode of this Cell, may be a primitive op or cell with user defined bprop.
  AnfNodePtr last_node_{nullptr};
  ValuePtr sens_value_{nullptr};
  // Bprop funcgraph
  FuncGraphPtr tape_;
  // Top cell inputs
  AnfNodePtrList cell_inputs_;
  // These weights need to calculate gradient.
  mindspore::HashSet<AnfNodePtr> need_grad_weights_;
  // Bprop dins of each variable or middle out
  OrderedMap<AnfNodePtr, VariableNodePtr> anfnode_to_variable_adjoint_;
  AnfNodePtrList weights_;
  // Record cnode's input map for tape_
  UserType users_;
  // Flag for ms_funtcion and high order
  bool has_fbprop_{false};

  bool IsCNodeNeedGrad(const AnfNodePtr &node_ptr) const;
  std::vector<bool> GetNeedGradFlags(const CNodePtr &cnode);

  // construct input as cnode for expander
  CNodePtr ConstructBpropGraphInput(const CNodePtr &cnode, const ValuePtrList &op_args, const ValuePtr &out,
                                    const AnfNodePtr &dout);
  // Back propagate for one node;
  void UpdateNextEdges(const FunctionNodePtr &fn, const CNodePtr &cnode, const std::vector<CNodePtr> &dins,
                       const ValuePtrList &op_args);
  void UpdateNextEdges(const FunctionNodePtr &fn, const AnfNodePtr &node, const AnfNodePtr &din,
                       const ValuePtr &op_arg);

  void BuildForwardLastNode();
  // Add parameter(weights) to anfnode_to_variable_adjoint_
  void AddParameterNode(const AnfNodePtr &parameter, const ValuePtr &tensor);
  AnfNodePtr GetRealDin(const FunctionNodePtr &fn, const ValuePtr &out_value, const ValuePtr &sub_value,
                        const AnfNodePtr &din);
  void BuildBPropCutCNode(const CNodePtr &cnode, std::vector<CNodePtr> *outputs);
  void BuildCustomBpropCNode(const CNodePtr &cnode, std::vector<CNodePtr> *outputs);
  void BuildFakeBpropCNode(const CNodePtr &cnode, std::vector<CNodePtr> *outputs);
  // Replace input or weights parameter from primal funcgraph to parameters of tape_;
  void ReplacePrimalParameter(const AnfNodePtrList &weights, bool has_sens_arg);
  // Set sens and weights parameter nodes by user input info
  void SetSensAndWeights(const AnfNodePtrList &weights, bool has_sens_arg);
  // get last reverse iterator
  OrderedMap<AnfNodePtr, VariableNodePtr>::reverse_iterator GetLastNodeReverseIter();

  void BackPropagate();
  // Set return node according to grad flag
  void SetOutput(const AnfNodePtrList &weights, const std::vector<size_t> &grad_position, const GradAttr &grad_attr);
  AnfNodePtr GetGradNodeByIndex(const AnfNodePtrList &node_list, size_t index);
  AnfNodePtr GetInputGrad(bool grad_all_inputs, bool get_by_position, const std::vector<size_t> &grad_position);
  AnfNodePtr GetWeightGrad(bool grad_weights, const AnfNodePtrList &weights, bool weight_param_is_tuple);
  bool IsOutputBothEmpty(const AnfNodePtr &inputs_grad, const AnfNodePtr &weights_grad) const;
  AnfNodePtr GenerateEmptyTupleValue();
  void AddUser(const AnfNodePtr &node, const CNodePtr &user, size_t index);
  void Replace(const AnfNodePtr &old_node, const AnfNodePtr &new_node);
  void ElimateTupleGetItem();
  void ClearDeviceAddress(const ValuePtr &out);

  // Fbprop
  AnfNodePtr BuildKNode(const GradParamPtr &grad_param);
  AnfNodePtrList BuildKNodeListFromPrimalCNode(const CNodePtr &cnode, const VariableNodePtr &adjoint);
  AnfNodePtr BuildKNodeForCNodeInput(const ValuePtrList &op_args, const AnfNodePtr &input_node, size_t input_index);
};
using AutoGradCellImplPtr = std::shared_ptr<AutoGradCellImpl>;

// Start building back propagate funcgraph for this cell.
// cell_inputs: the input parameter list of this cell except the weights;
AutoGradCellImplPtr GradPynativeCellBegin(const AnfNodePtrList &cell_inputs,
                                          const std::vector<ValuePtr> &input_param_values);

// Return the back propagate funcgraph for this cell.
// weights: weights parameters used in this cell.
// grad_inputs: return sensitivity for input parameters;
// grad_weights: return sensitivity for weights;
// has_sens_arg: caller will pass sens args;
// return: the returned funcgraph will have prototype:
// if has_sens_arg is true
// (sens_input1, sens_input2, ..., sens_weight0, sens_weight1, ) bprop_fg(input1, input2, ..., weight0, weight1, ...,
// sens_out)
// else:
// (sens_input1, sens_input2, ..., sens_weight0, sens_weight1, ) bprop_fg(input1, input2, ..., weight0, weight1, ...)
// if build_formal_param is true
// each cnode in primal funcgraph is replaced by formal cnode
// else:
// each cnode in primal funcgraph is replaced by value node
FuncGraphPtr GradPynativeCellEnd(const AutoGradCellImplPtr &k_cell, const AnfNodePtrList &weights,
                                 const std::vector<size_t> &grad_position, const GradAttr &grad_attr,
                                 bool build_formal_param = false);

// Grad for each operation.
// c_node: CNode with contains the prim (index 0) and the formal input parameters of that prim.
// op_args: the arguments list of each input parameters.
// out: the op result.
bool GradPynativeOp(const AutoGradCellImplPtr &k_cell, const CNodePtr &cnode, const ValuePtrList &op_args,
                    const ValuePtr &out);

// adjoint bprop form ms_function and high grad
void GradPynativeFBprop(const CNodePtr &cnode, const ValuePtrList &op_args, const ValuePtr &out,
                        const FuncGraphPtr &fprop_fg);
}  // namespace ad
}  // namespace mindspore

#endif  // MINDSPORE_CCSRC_FRONTEND_OPTIMIZER_AD_AUTO_GRAD_H_