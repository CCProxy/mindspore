# Copyright 2022 Huawei Technologies Co., Ltd
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ============================================================================
"""Parse ast.ClassDef which is subclass of Cell to SymbolTree."""
import sys
import ast
import inspect
import astunparse
from mindspore import log as logger
from mindspore._extends.parse.namespace import CellNamespace
from mindspore.rewrite.ast_creator_register import ast_creator_registry
from ..symbol_tree import SymbolTree
from ..parser import Parser
from ..parser_register import ParserRegister, reg_parser
from ..ast_helpers import AstReplacer
from ..common import error_str


class AstScopeChecker:
    """
    Check scope of ast node meets the constraints recursively.

    Args:
        scope_constraints (str): A string represents constraints of scope.
    """

    def __init__(self, scope_constraints: str):
        self._scope = scope_constraints

    def check(self, node: ast.AST) -> bool:
        """
        Check scope of `node` meets the constraints recursively.

        Args:
            node (ast.AST): A ast.AST node to be checked.

        Returns:
            A bool represents if input `node` meets constraints.
        """
        if isinstance(node, ast.Compare):
            return self._check_compare(node)
        if isinstance(node, ast.Attribute):
            return self._check_attribute(node)
        if isinstance(node, ast.Name):
            return False
        if isinstance(node, ast.BoolOp):
            return self._check_bool(node)
        if isinstance(node, ast.UnaryOp):
            return self._check_unary(node)
        if isinstance(node, ast.Call):
            return self._check_call(node)
        if isinstance(node, (ast.Constant, ast.NameConstant, ast.Bytes, ast.Str, ast.Num)):
            return True
        raise RuntimeError(error_str(f"only support (ast.Compare, ast.Attribute, ast.Name, ast.BoolOp, ast.UnaryOp"
                                     f"ast.Call, ast.Constant, ast.NameConstant, ast.Bytes, ast.Str, ast.Num"
                                     f") as test check, but got ast type '{type(node).__name__}'", father_node=node))

    def _check_attribute(self, node: ast.Attribute):
        """Check an ast.Attribute meets the constraints recursively."""
        if not isinstance(node.value, ast.Name):
            return False
        if node.value.id != self._scope:
            return False
        return True

    def _check_compare(self, node: ast.Compare):
        """Check an ast.Compare meets the constraints recursively."""
        left = node.left
        for comparator in node.comparators:
            if not self.check(comparator):
                return False
        return self.check(left)

    def _check_bool(self, node: ast.BoolOp):
        """Check an ast.BoolOp meets the constraints recursively."""
        for value in node.values:
            if not self.check(value):
                return False
        return True

    def _check_call(self, node: ast.Call):
        """Check an ast.Call meets the constraints recursively."""
        for arg in node.args:
            if not self.check(arg):
                return False
        for kwarg in node.keywords:
            if not self.check(kwarg):
                return False
        return self.check(node.func)

    def _check_unary(self, node: ast.UnaryOp):
        """Check an ast.UnaryOp meets the constraints recursively."""
        return self.check(node.operand)


class ClassDefParser(Parser):
    """Parse ast.ClassDef which is subclass of Cell to SymbolTree."""

    def __init__(self):
        """Constructor"""
        super(ClassDefParser, self).__init__()
        self._cell_namespace = CellNamespace('mindspore.nn')

    def target(self):
        """Parse target type"""
        return ast.ClassDef

    def _is_subtree_field(self, ori_net, field) -> bool:
        op = getattr(ori_net, field)
        return not type(op).__name__ in self._cell_namespace

    def _process_init_func_ast(self, stree: SymbolTree, init_ast: ast.FunctionDef):
        """Process init func"""
        super_index = ClassDefParser._find_super_expr_of_init_func(init_ast)
        ClassDefParser._modify_arguments_of_init_func(init_ast)
        self._replace_ori_field_of_init_func(stree, init_ast.body, super_index)

    @staticmethod
    def _find_super_expr_of_init_func(ast_init_fn: ast.FunctionDef) -> int:
        """Find index of super(XXnet).__init__() in body of init ast.FunctionDef"""
        if not ast_init_fn.body:
            return -1
        super_index = -1
        while True:
            super_index += 1
            expr = ast_init_fn.body[super_index]
            if not isinstance(expr, ast.Expr):
                continue
            expr_value = expr.value
            if not isinstance(expr_value, ast.Call):
                continue
            expr_value_func = expr_value.func
            if not isinstance(expr_value_func, ast.Attribute):
                continue
            expr_value_func_value = expr_value_func.value
            if expr_value_func.attr != "__init__" or not isinstance(expr_value_func_value, ast.Call):
                continue
            expr_value_func_value_func = expr_value_func_value.func
            if not isinstance(expr_value_func_value_func, ast.Name) or expr_value_func_value_func.id != "super":
                continue
            break
        return super_index

    @staticmethod
    def _modify_arguments_of_init_func(ast_init_fn: ast.FunctionDef):
        """Replace init function input parameters to self and global_vars."""
        arg_self = ast.arg(arg="self", annotation="")
        arg_global_vars = ast.arg(arg="obj", annotation="")
        ast_init_fn.args = ast.arguments(args=[arg_self, arg_global_vars], posonlyargs=[], kwonlyargs=[],
                                         kw_defaults=[], defaults=[], vararg=None, kwarg=None)
        ast.fix_missing_locations(ast_init_fn)

    @staticmethod
    def _remove_empty_ast_in_init_func(bodies: []):
        """Remove ast.If, ast.For or other ast node with body when their body is empty recursively."""
        body_index_to_be_deleted = []
        for body_index, body in enumerate(bodies):
            if isinstance(body, ast.If):
                ClassDefParser._remove_empty_ast_in_init_func(body.body)
                ClassDefParser._remove_empty_ast_in_init_func(body.orelse)
                if not body.body and not body.orelse:
                    body_index_to_be_deleted.append(body_index)
                if not body.body and body.orelse:
                    body.body.append(ast.Pass())
                continue
            if isinstance(body, ast.For):
                ClassDefParser._remove_empty_ast_in_init_func(body.body)
                ClassDefParser._remove_empty_ast_in_init_func(body.orelse)
                if not body.body or not body.orelse:
                    body_index_to_be_deleted.append(body_index)
                continue
            if hasattr(body, "body"):
                ClassDefParser._remove_empty_ast_in_init_func(body.body)
                if not body.body:
                    body_index_to_be_deleted.append(body_index)
        for counter, index in enumerate(body_index_to_be_deleted):
            bodies.pop(index - counter)

    def _replace_ori_field_of_init_func(self, stree: SymbolTree, bodies: [], super_index: int):
        """
        Replace original field in init func to self.XX = getattr(self._handler, "XX").
        Only keep following two kinds of ast nodes in bodies right now:
            1. Ast.If and test is self.XX.
            2. Ast.Assign and target is self.XX.

        Args:
            bodies ([]): bodied of init ast.FunctionDef.
            super_index (int): index of super().__init__() in bodies.

        Raises:
            RuntimeError: Not support multi-targets in assign.
            RuntimeError: Only support target.value in [ast.Name] in assign node.
        """
        def _handle_tuple(t: ast.Tuple):
            for e in t.elts:
                if isinstance(e, ast.Attribute):
                    field_name = e.attr
                    value = ast.Call(ast.Name('getattr', ast.Load()),
                                     [ast.Name('obj', ast.Load()),
                                      ast.Constant(value=field_name, kind=None)], [])
                    new_assign = ast_creator_registry.get("Assign")(targets=[e], value=value)
                    new_node_to_be_inserted.append(new_assign)
        body_index_to_be_deleted = []
        new_node_to_be_inserted = []
        scope_checker = AstScopeChecker("self")
        for body_index, body in enumerate(bodies):
            if body_index == super_index:
                continue  # ignoring super.__init__()
            if isinstance(body, ast.If):
                if scope_checker.check(body.test):
                    self._replace_ori_field_of_init_func(stree, body.body, -1)
                    self._replace_ori_field_of_init_func(stree, body.orelse, -1)
                    continue
                logger.info("Ignoring un-eval-able if: %s", astunparse.unparse(body.test))
            if not isinstance(body, ast.Assign):  # if not assign node, delete
                body_index_to_be_deleted.append(body_index)
                continue
            if len(body.targets) != 1:
                raise RuntimeError("not support multi-targets in assign now!", father_node=body)
            target = body.targets[0]
            if isinstance(target, ast.Tuple):
                _handle_tuple(target)
                body_index_to_be_deleted.append(body_index)
                continue
            if not isinstance(target, ast.Attribute):  # only keep class member
                body_index_to_be_deleted.append(body_index)
                continue
            if not isinstance(target.value, ast.Name):
                logger.info(f"Ignoring {astunparse.unparse(target)} in __init__ function.")
                body_index_to_be_deleted.append(body_index)
                continue
            target_value: ast.Name = target.value
            if target_value.id != "self":
                body_index_to_be_deleted.append(body_index)
                continue
            field_name = target.attr
            body.value = ast.Call(ast.Name('getattr', ast.Load()),
                                  [ast.Name('obj', ast.Load()),
                                   ast.Constant(value=field_name, kind=None)], [], lineno=0, col_offset=0)
        for counter, index in enumerate(body_index_to_be_deleted):
            bodies.pop(index - counter)
        for n in new_node_to_be_inserted:
            bodies.append(n)
        ClassDefParser._remove_empty_ast_in_init_func(bodies)

    def handle_father_class(self, stree, node: ast.ClassDef):
        """Handle father class."""
        for base in node.bases:
            parser: Parser = ParserRegister.instance().get_parser(type(base))
            father_class = parser.process(stree, base)
            if "Cell" not in father_class:
                for k, m in sys.modules.items():
                    if k in ("_ast", "ast"):
                        continue
                    if hasattr(m, father_class):
                        cls = getattr(m, father_class)
                        source_code = inspect.getsource(cls)
                        ast_root: ast.Module = ast.parse(source_code)
                        stree._father_class_ast.append(ast_root) # pylint: disable=protected-access
                        break

    def process(self, stree: SymbolTree, node: ast.ClassDef):
        """
        Parse init and construct in ast.ClassDef.

        Args:
            stree ([SymbolTree]): Symbol Tree under parsing.
            node ([ast.ClassDef]): An ast.ClassDef node.
        """
        replacer = AstReplacer(node)
        replacer.replace_all(stree.get_ori_cls_name(), stree.get_opt_cls_name())

        stree.set_class_ast(node)
        self.handle_father_class(stree, node)
        for body in node.body:
            if isinstance(body, ast.FunctionDef):
                if body.name == "__init__":
                    self._process_init_func_ast(stree, body)
                    stree.set_init_func_ast(body)
                elif body.name == "construct":
                    parser: Parser = ParserRegister.instance().get_parser(ast.FunctionDef)
                    parser.process(stree, body)
                else:
                    logger.info(
                        "Ignoring ast.FunctionDef in ast.ClassDef except __init__ and construct function: %s",
                        body.name)
            else:
                logger.info("Ignoring unsupported node(%s) in ast.ClassDef.", type(body).__name__)


g_classdef_parser = reg_parser(ClassDefParser())
