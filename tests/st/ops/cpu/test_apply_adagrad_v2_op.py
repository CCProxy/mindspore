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

import numpy as np
import pytest

import mindspore.context as context
import mindspore.nn as nn
from mindspore import Tensor, Parameter
from mindspore.ops import operations as P
import mindspore.common.dtype as mstype

context.set_context(mode=context.GRAPH_MODE, device_target="CPU")

var_np = np.random.rand(3, 3).astype(np.float32)
accum_np = np.random.rand(3, 3).astype(np.float32)


class Net(nn.Cell):
    def __init__(self, user_eps):
        super(Net, self).__init__()
        self.apply_adagrad_v2 = P.ApplyAdagradV2(epsilon=user_eps)
        self.var = Parameter(Tensor(var_np), name="var")
        self.accum = Parameter(Tensor(accum_np), name="accum")

    def construct(self, lr, grad):
        return self.apply_adagrad_v2(self.var, self.accum, lr, grad)


@pytest.mark.skip(reason='platform not support')
@pytest.mark.level0
@pytest.mark.platform_x86_cpu
@pytest.mark.env_onecard
def test_apply_adagrad_v2():
    """
    Feature: Test the ApplyAdagradV2 CPU operation
    Description: Recreate the operation in numpy, and compare the result with the mindspore version.
    Expectation: Both the numpy and mindspore version of the operation should produce the same result.
    """
    # numpy op
    v2_eps = 1e-6
    gradient_np = np.random.rand(3, 3).astype(np.float32)
    expect_accum_np = accum_np + gradient_np * gradient_np
    np_dividend = (np.sqrt(expect_accum_np) + v2_eps)
    #update zero values to avoid division-by-zero
    np_dividend[np_dividend == 0] = 1e-6
    expect_var_np = var_np - (0.001 * gradient_np * (1 / np_dividend))

    net = Net(user_eps=v2_eps)
    lr = Tensor(0.001, mstype.float32)
    grad = Tensor(gradient_np)
    out = net(lr, grad)
    res_var_mindspore = out[0].asnumpy()
    res_accum_mindspore = out[1].asnumpy()
    eps = np.array([1e-6 for i in range(9)]).reshape(3, 3)

    assert np.all(np.abs(expect_var_np - res_var_mindspore) < eps)
    assert np.all(np.abs(expect_accum_np - res_accum_mindspore) < eps)
