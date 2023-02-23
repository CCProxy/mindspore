# This is the Python adaptation and derivative work of Myia (https://github.com/mila-iqia/myia/).
#
# Copyright 2021-2022 Huawei Technologies Co., Ltd
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
"""The names of functional part are summarized here."""

from mindspore.common._register_for_tensor import tensor_operator_registry
from mindspore.ops import _constants
from mindspore.ops.function import *
from mindspore.ops.function.array_func import narrow, flatten
from mindspore.ops import operations as P
from mindspore.ops.primitive import Primitive
from mindspore.ops.operations import _grad_ops, _csr_ops, _inner_ops, linalg_ops, _scalar_ops
from mindspore.ops.operations.math_ops import Median
from mindspore.ops.operations.array_ops import UniqueConsecutive, Triu
from mindspore.ops.operations.nn_ops import AdaptiveMaxPool2D
from mindspore.ops.operations.math_ops import Roll
from mindspore.ops.composite.math_ops import mm, dot

typeof = Primitive('typeof')
hastype = Primitive('hastype')
cast = P.Cast()
dtype = P.DType()
isconstant = Primitive('is_constant')
isconstant.set_const_prim(True)
merge = P.Merge()
geswitch = P.GeSwitch()
reduce_sum = P.ReduceSum()
reduce_max = P.ReduceMax()
reduce_min = P.ReduceMin()
reduce_mean = P.ReduceMean()
tensor_range = P.Range()
tensor_scatter_update = P.TensorScatterUpdate()
scatter_nd_update = P.ScatterNdUpdate()
mixed_precision_cast = _inner_ops.MixedPrecisionCast()

# Dynamic shape
is_sequence_value_unknown = Primitive("IsShapeUnKnown")
is_sequence_shape_unknown = Primitive("IsDimUnKnown")
is_dynamic_sequence_element_unknown = Primitive("IsElementUnknown")

partial = P.Partial()
# depend: mount a node to another node
depend = P.Depend()
identity = P.Identity()
# tuple/list/scalar ops
scalar_div = _scalar_ops.ScalarDiv()
scalar_mod = _scalar_ops.ScalarMod()
scalar_add = _scalar_ops.ScalarAdd()
scalar_mul = _scalar_ops.ScalarMul()
scalar_sub = _scalar_ops.ScalarSub()
scalar_gt = _scalar_ops.scalar_gt()
scalar_ge = _scalar_ops.scalar_ge()
scalar_le = _scalar_ops.scalar_le()
scalar_lt = _scalar_ops.scalar_lt()
scalar_eq = _scalar_ops.scalar_eq()
scalar_floordiv = _scalar_ops.ScalarFloordiv()

tuple_setitem = Primitive('tuple_setitem')
tuple_getitem = Primitive(_constants.kTupleGetItem)
list_getitem = Primitive('list_getitem')
list_setitem = Primitive('list_setitem')
dict_getitem = Primitive('dict_getitem')
dict_setitem = Primitive('dict_setitem')
tuple_div = Primitive("tuple_div")
tuple_len = Primitive("sequence_len")
list_len = Primitive("sequence_len")
tuple_reversed = Primitive("tuple_reversed")
make_range = Primitive("make_range")
make_tuple = Primitive('MakeTuple')
make_dict = Primitive('make_dict')
make_list = Primitive('make_list')
make_slice = Primitive('make_slice')
tuple_equal = Primitive("tuple_equal")
list_equal = Primitive("list_equal")
scalar_log = Primitive('scalar_log')
scalar_pow = Primitive(_constants.kScalarPow)
scalar_ne = Primitive('scalar_ne')
scalar_uadd = Primitive(_constants.kScalarUadd)
scalar_usub = Primitive(_constants.kScalarUsub)
string_eq = Primitive('string_eq')
string_concat = Primitive('string_concat')
bool_not = Primitive("bool_not")
bool_or = Primitive("bool_or")
bool_and = Primitive("bool_and")
bool_eq = Primitive("bool_eq")
array_to_scalar = Primitive('array_to_scalar')
is_ = Primitive("is_")
is_not = Primitive("is_not")
in_dict = Primitive("in_dict")
not_in_dict = Primitive("not_in_dict")
broadcast_gradient_args = Primitive('BroadcastGradientArgs')
array_reduce = Primitive('array_reduce')
distribute = Primitive('distribute')
embed = Primitive('embed')
ref_to_embed = _grad_ops.RefToEmbed()
environ_create = Primitive('EnvironCreate')
environ_set = Primitive('EnvironSet')
environ_get = Primitive('EnrironGet')
environ_add = Primitive('EnvironAdd')
J = Primitive('J')
SliceGetItem = Primitive("SliceGetItem")
switch = Primitive('Switch')
switch_layer = Primitive('switch_layer')
# for sum bprop
reduced_shape = Primitive("reduced_shape")
# shape_mul:input must be shape multiply elements in tuple(shape)
shape_mul = Primitive("shape_mul")

tensor_operator_registry.register('add', P.Add)
tensor_operator_registry.register('addr', addr)
tensor_operator_registry.register('addcdiv', P.Addcdiv)
tensor_operator_registry.register('addcmul', P.Addcmul)
tensor_operator_registry.register('all', P.ReduceAll)
tensor_operator_registry.register('angle', angle)
tensor_operator_registry.register('any', P.ReduceAny)
tensor_operator_registry.register('atan2', atan2)
tensor_operator_registry.register('abs', P.Abs)
tensor_operator_registry.register('baddbmm', baddbmm)
tensor_operator_registry.register('geqrf', geqrf)
tensor_operator_registry.register('histc', histc)
tensor_operator_registry.register('real', real)
tensor_operator_registry.register('reciprocal', reciprocal)
tensor_operator_registry.register('rsqrt', rsqrt)
tensor_operator_registry.register('bincount', bincount)
tensor_operator_registry.register('slogdet', slogdet)
tensor_operator_registry.register('tril', tril)
tensor_operator_registry.register('chunk', chunk)
tensor_operator_registry.register('sqrt', sqrt)
tensor_operator_registry.register('square', square)
tensor_operator_registry.register('sub', sub)
tensor_operator_registry.register('triu', Triu)
tensor_operator_registry.register('tan', P.Tan)
tensor_operator_registry.register('t', t)
tensor_operator_registry.register('acos', acos)
tensor_operator_registry.register('cos', cos)
tensor_operator_registry.register('acosh', acosh)
tensor_operator_registry.register('cosh', P.Cosh)
tensor_operator_registry.register('cov', cov)
tensor_operator_registry.register('asin', asin)
tensor_operator_registry.register('sin', sin)
tensor_operator_registry.register('sinc', sinc)
tensor_operator_registry.register('pow', P.Pow)
tensor_operator_registry.register('negative', neg)
tensor_operator_registry.register('amin', amin)
tensor_operator_registry.register('amax', amax)
tensor_operator_registry.register('mean', P.ReduceMean)
tensor_operator_registry.register('prod', prod)
tensor_operator_registry.register('round', P.Round)
tensor_operator_registry.register('reshape', P.Reshape)
tensor_operator_registry.register('reverse', P.ReverseV2)
tensor_operator_registry.register('reverse_sequence', P.ReverseSequence)
tensor_operator_registry.register('xlogy', P.Xlogy)
tensor_operator_registry.register('flatten', flatten)
tensor_operator_registry.register('transpose', P.Transpose)
tensor_operator_registry.register('broadcast_to', P.BroadcastTo)
tensor_operator_registry.register('matmul', matmul)
tensor_operator_registry.register('inner', inner)
tensor_operator_registry.register('xdivy', P.Xdivy)
tensor_operator_registry.register('argmax', P.Argmax)
tensor_operator_registry.register('argmin', P.Argmin)
tensor_operator_registry.register('cumsum', P.CumSum)
tensor_operator_registry.register('cummin', cummin)
tensor_operator_registry.register('cummax', cummax)
tensor_operator_registry.register('nelement', numel)
tensor_operator_registry.register('numel', numel)
tensor_operator_registry.register('positive', positive)
tensor_operator_registry.register('permute', permute)
tensor_operator_registry.register('remainder', remainder)
tensor_operator_registry.register('index_fill', index_fill)
tensor_operator_registry.register('index_select', index_select)
tensor_operator_registry.register('flip', flip)
tensor_operator_registry.register('fliplr', fliplr)
tensor_operator_registry.register('flipud', flipud)
tensor_operator_registry.register('float_power', float_power)
tensor_operator_registry.register('fmod', fmod)
tensor_operator_registry.register('is_floating_point', is_floating_point)
tensor_operator_registry.register('bitwise_and', bitwise_and)
tensor_operator_registry.register('bitwise_or', bitwise_or)
tensor_operator_registry.register('bitwise_xor', bitwise_xor)
tensor_operator_registry.register('bitwise_left_shift', bitwise_left_shift)
tensor_operator_registry.register('bitwise_right_shift', bitwise_right_shift)
tensor_operator_registry.register('ger', ger)
tensor_operator_registry.register('reduce_max', P.ReduceMax)
tensor_operator_registry.register('reduce_min', P.ReduceMin)
tensor_operator_registry.register('random_categorical', random_categorical)
tensor_operator_registry.register('mirror_pad', P.MirrorPad)
tensor_operator_registry.register('minimum', P.Minimum)
tensor_operator_registry.register('matrix_power', matrix_power)
tensor_operator_registry.register('det', det)
tensor_operator_registry.register('dot', dot)
tensor_operator_registry.register('log1p', log1p)
tensor_operator_registry.register('logdet', logdet)
tensor_operator_registry.register('ceil', P.Ceil)
tensor_operator_registry.register('fill', P.Fill)
tensor_operator_registry.register('tile', P.Tile)
tensor_operator_registry.register('logit', logit)
tensor_operator_registry.register('sum', P.ReduceSum)
tensor_operator_registry.register('split', split)
tensor_operator_registry.register('tensor_split', tensor_split)
tensor_operator_registry.register('vsplit', vsplit)
tensor_operator_registry.register('hsplit', hsplit)
tensor_operator_registry.register('dsplit', dsplit)
tensor_operator_registry.register('select', P.Select)
tensor_operator_registry.register('zeros_like', P.ZerosLike)
tensor_operator_registry.register('scalar_to_tensor', scalar_to_tensor)
tensor_operator_registry.register('stop_gradient', stop_gradient)
tensor_operator_registry.register('masked_fill', masked_fill)
tensor_operator_registry.register('masked_select', masked_select)
tensor_operator_registry.register('nonzero', nonzero)
tensor_operator_registry.register('i0', i0)
tensor_operator_registry.register('isclose', isclose)
tensor_operator_registry.register('isneginf', isneginf)
tensor_operator_registry.register('isposinf', isposinf)
tensor_operator_registry.register('isreal', isreal)
tensor_operator_registry.register('inv', inv)
tensor_operator_registry.register('digamma', digamma)
tensor_operator_registry.register('lgamma', lgamma)
tensor_operator_registry.register('logaddexp', logaddexp)
tensor_operator_registry.register('logaddexp2', logaddexp2)
tensor_operator_registry.register('logsumexp', logsumexp)
tensor_operator_registry.register('inverse', inverse)
tensor_operator_registry.register('invert', invert)
tensor_operator_registry.register('hardshrink', P.HShrink)
tensor_operator_registry.register('heaviside', heaviside)
tensor_operator_registry.register('hypot', hypot)
tensor_operator_registry.register('svd', linalg_ops.Svd)
tensor_operator_registry.register('diag', P.Diag)
tensor_operator_registry.register('diagflat', diagflat)
tensor_operator_registry.register('unique_consecutive', UniqueConsecutive)
tensor_operator_registry.register('unique_with_pad', P.UniqueWithPad)
tensor_operator_registry.register('inplace_update', P.InplaceUpdateV2)
tensor_operator_registry.register('col2im', col2im)
tensor_operator_registry.register('standard_laplace', P.StandardLaplace)
tensor_operator_registry.register('erf', P.Erf)
tensor_operator_registry.register('erfc', P.Erfc)
tensor_operator_registry.register('standard_normal', P.StandardNormal)
tensor_operator_registry.register('sigmoid', P.Sigmoid)
tensor_operator_registry.register('median', Median)
tensor_operator_registry.register('tanh', tanh)
tensor_operator_registry.register('exp', P.Exp)
tensor_operator_registry.register('addbmm', addbmm)
tensor_operator_registry.register('addmm', addmm)
tensor_operator_registry.register('addmv', addmv)
tensor_operator_registry.register('adjoint', adjoint)
tensor_operator_registry.register('asinh', asinh)
tensor_operator_registry.register('arcsinh', arcsinh)
tensor_operator_registry.register('atan', atan)
tensor_operator_registry.register('atanh', atanh)
tensor_operator_registry.register('arctanh', arctanh)
tensor_operator_registry.register('bmm', bmm)
tensor_operator_registry.register('conj', conj)
tensor_operator_registry.register('cross', cross)
tensor_operator_registry.register('erfinv', erfinv)
tensor_operator_registry.register('less_equal', less_equal)
tensor_operator_registry.register('lcm', lcm)
tensor_operator_registry.register('ldexp', ldexp)
tensor_operator_registry.register('clamp', clamp)
tensor_operator_registry.register('fold', fold)
tensor_operator_registry.register('unfold', unfold)
tensor_operator_registry.register('index_add', index_add)
tensor_operator_registry.register('greater', greater)
tensor_operator_registry.register('greater_equal', greater_equal)
tensor_operator_registry.register('igamma', igamma)
tensor_operator_registry.register('igammac', igammac)
# ms cannot support Tensor(True) compare
tensor_operator_registry.register('__eq__', equal)
tensor_operator_registry.register('__ne__', not_equal)
tensor_operator_registry.register('__neg__', neg_tensor)
tensor_operator_registry.register('__lt__', tensor_lt)
tensor_operator_registry.register('__le__', tensor_le)
tensor_operator_registry.register('__gt__', tensor_gt)
tensor_operator_registry.register('__ge__', tensor_ge)
tensor_operator_registry.register('__logical_not__', logical_not)
tensor_operator_registry.register('gt', P.Greater)
tensor_operator_registry.register('ge', P.GreaterEqual)
tensor_operator_registry.register('shape', shape)
tensor_operator_registry.register('squeeze', squeeze)
tensor_operator_registry.register('unsqueeze', unsqueeze)
tensor_operator_registry.register('expand_dims', expand_dims)
# support GE backend for no compare operators
tensor_operator_registry.register('cast', cast)
tensor_operator_registry.register('shape_mul', shape_mul)
tensor_operator_registry.register('concatenate', P.Concat)
tensor_operator_registry.register('fill', fill)
tensor_operator_registry.register('eye', eye)
tensor_operator_registry.register('reduce_sum', reduce_sum)
tensor_operator_registry.register('tensor_slice', tensor_slice)
tensor_operator_registry.register('select', select)
tensor_operator_registry.register('gather', gather)
tensor_operator_registry.register('gather_d', gather_d)
tensor_operator_registry.register('gather_elements', gather_elements)
tensor_operator_registry.register('gather_nd', gather_nd)
tensor_operator_registry.register('stack', stack)
tensor_operator_registry.register('unstack', unstack)
tensor_operator_registry.register('unbind', P.Unstack)
tensor_operator_registry.register('log', log)
tensor_operator_registry.register('log10', log10)
tensor_operator_registry.register('log2', log2)
tensor_operator_registry.register('lerp', lerp)
tensor_operator_registry.register('floor', floor)
# support sparse tensor operators
tensor_operator_registry.register('csr_add', csr_add)
tensor_operator_registry.register('csr_mul', csr_mul)
tensor_operator_registry.register('csr2coo', csr2coo)
tensor_operator_registry.register('coo2csr', coo2csr)
tensor_operator_registry.register('csr_div', csr_div)
tensor_operator_registry.register('csr_mv', csr_mv)
tensor_operator_registry.register('csr_mm_akg', _csr_ops.CSRMM)
tensor_operator_registry.register('csr_mm', csr_mm)
tensor_operator_registry.register('csr_reduce_sum', csr_reduce_sum)
tensor_operator_registry.register('dense_to_sparse_csr', dense_to_sparse_csr)
tensor_operator_registry.register('dense_to_sparse_coo', dense_to_sparse_coo)
tensor_operator_registry.register('csr_to_dense', csr_to_dense)
tensor_operator_registry.register('narrow', narrow)
tensor_operator_registry.register('sort', sort)
tensor_operator_registry.register('argsort', argsort)
tensor_operator_registry.register('msort', msort)
tensor_operator_registry.register('mm', mm)
tensor_operator_registry.register('nan_to_num', nan_to_num)
tensor_operator_registry.register('nansum', nansum)
tensor_operator_registry.register('csr_to_coo', csr_to_coo)
tensor_operator_registry.register('zeros', zeros)
tensor_operator_registry.register('ones', ones)
tensor_operator_registry.register('unsorted_segment_min', unsorted_segment_min)
tensor_operator_registry.register('unsorted_segment_max', unsorted_segment_max)
tensor_operator_registry.register('unsorted_segment_prod', unsorted_segment_prod)
tensor_operator_registry.register('scatter', scatter)
tensor_operator_registry.register('tensor_scatter_update', tensor_scatter_update)
tensor_operator_registry.register('tensor_scatter_mul', tensor_scatter_mul)
tensor_operator_registry.register('tensor_scatter_div', tensor_scatter_div)
tensor_operator_registry.register('tensor_scatter_min', P.TensorScatterMin)
tensor_operator_registry.register('tensor_scatter_max', P.TensorScatterMax)
tensor_operator_registry.register('tensor_scatter_sub', tensor_scatter_sub)
tensor_operator_registry.register('tensor_scatter_add', tensor_scatter_add)
tensor_operator_registry.register('bernoulli', bernoulli)
tensor_operator_registry.register('poisson', P.Poisson)
tensor_operator_registry.register('randperm', P.Randperm)
tensor_operator_registry.register('multinomial', P.Multinomial)
tensor_operator_registry.register('norm', norm)
tensor_operator_registry.register('renorm', renorm)
tensor_operator_registry.register('adaptive_max_pool2d', AdaptiveMaxPool2D)
tensor_operator_registry.register('coalesce', coalesce)
tensor_operator_registry.register('argmax_with_value', max)
tensor_operator_registry.register('argmin_with_value', min)
tensor_operator_registry.register('argwhere', argwhere)
tensor_operator_registry.register('coo_add', coo_add)
tensor_operator_registry.register('topk', topk)
tensor_operator_registry.register('isfinite', P.IsFinite)
tensor_operator_registry.register('to', P.Cast)
tensor_operator_registry.register('bool', P.Cast)
tensor_operator_registry.register('float', P.Cast)
tensor_operator_registry.register('half', P.Cast)
tensor_operator_registry.register('int', P.Cast)
tensor_operator_registry.register('long', P.Cast)
tensor_operator_registry.register('cholesky', P.Cholesky)
tensor_operator_registry.register('cholesky_inverse', P.CholeskyInverse)
tensor_operator_registry.register('expand', expand)
tensor_operator_registry.register('cumprod', cumprod)
tensor_operator_registry.register('diff', diff)
tensor_operator_registry.register('div', div)
tensor_operator_registry.register('equal', equal)
tensor_operator_registry.register('expm1', expm1)
tensor_operator_registry.register('frac', frac)
tensor_operator_registry.register('isinf', isinf)
tensor_operator_registry.register('isnan', isnan)
tensor_operator_registry.register('is_complex', is_complex)
tensor_operator_registry.register('le', le)
tensor_operator_registry.register('less', less)
tensor_operator_registry.register('logical_and', logical_and)
tensor_operator_registry.register('logical_not', logical_not)
tensor_operator_registry.register('logical_or', logical_or)
tensor_operator_registry.register('logical_xor', logical_xor)
tensor_operator_registry.register('lstsq', lstsq)
tensor_operator_registry.register('mvlgamma', mvlgamma)
tensor_operator_registry.register('maximum', maximum)
tensor_operator_registry.register('max', max)
tensor_operator_registry.register('min', min)
tensor_operator_registry.register('mul', mul)
tensor_operator_registry.register('multiply', multiply)
tensor_operator_registry.register('moveaxis', moveaxis)
tensor_operator_registry.register('movedim', movedim)
tensor_operator_registry.register('neg', neg)
tensor_operator_registry.register('ne', ne)
tensor_operator_registry.register('not_equal', not_equal)
tensor_operator_registry.register('sgn', sgn)
tensor_operator_registry.register('sign', sign)
tensor_operator_registry.register('signbit', signbit)
tensor_operator_registry.register('sinh', sinh)
tensor_operator_registry.register('trunc', trunc)
tensor_operator_registry.register('where', where)
tensor_operator_registry.register('imag', imag)
tensor_operator_registry.register('repeat_interleave', repeat_interleave)
tensor_operator_registry.register('rad2deg', rad2deg)
tensor_operator_registry.register('deg2rad', deg2rad)
tensor_operator_registry.register('copysign', copysign)
tensor_operator_registry.register('roll', Roll)
tensor_operator_registry.register('rot90', rot90)
tensor_operator_registry.register('swapaxes', swapaxes)
tensor_operator_registry.register('swapdims', swapdims)
tensor_operator_registry.register('repeat_elements', repeat_elements)

__all__ = [name for name in dir() if name[0] != "_"]
__all__.remove('Primitive')
