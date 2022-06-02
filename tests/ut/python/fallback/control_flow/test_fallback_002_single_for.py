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
""" test graph fallback control flow."""
import numpy as np
from mindspore import Tensor, ms_function, context

context.set_context(mode=context.GRAPH_MODE)


def test_single_for_numpy():
    """
    Feature: JIT Fallback
    Description: Test fallback with control flow.
    Expectation: No exception.
    """
    @ms_function
    def control_flow_for():
        x = np.array([1, 3, 5])
        y = np.array([0, 2, 4])
        for _ in range(3):
            x = x + y
        return Tensor(x)
    res = control_flow_for()
    assert (res.asnumpy() == [1, 9, 17]).all()


def test_single_for_builtin_function_sum():
    """
    Feature: JIT Fallback
    Description: Test fallback with control flow.
    Expectation: No exception.
    """
    @ms_function
    def control_flow_for():
        x = np.array([1, 3, 5, 7, 9])
        result = x
        for _ in range(3):
            result = sum(x)
        return Tensor(result)
    res = control_flow_for()
    assert res == 25


def test_single_for_builtin_function_numpy_sum():
    """
    Feature: JIT Fallback
    Description: Test fallback with control flow.
    Expectation: No exception.
    """
    @ms_function
    def control_flow_for():
        x = np.array([1, 3, 5, 7, 9])
        y = np.array([0, 2, 4, 6, 8])
        result = x
        for _ in range(3):
            x = x + y
            result = sum(x, 1)
        return Tensor(result)
    res = control_flow_for()
    assert res == 86


def test_single_for_builtin_function_tensor_sum():
    """
    Feature: JIT Fallback
    Description: Test fallback with control flow.
    Expectation: No exception.
    """
    @ms_function
    def control_flow_for():
        x = Tensor(np.array([1, 3, 5, 7, 9]))
        y = Tensor(np.array([0, 2, 4, 6, 8]))
        result = x
        for _ in range(3):
            x = x + y
            result = sum(x, 1)
        return result
    res = control_flow_for()
    assert res == 86


def test_single_for_builtin_function_list():
    """
    Feature: JIT Fallback
    Description: Test fallback with control flow.
    Expectation: No exception.
    """
    @ms_function
    def control_flow_for():
        x = np.array([1.1, 2.2])
        for _ in range(3):
            x = x + list(x)
        return Tensor(x)
    res = control_flow_for()
    assert (res.asnumpy() == [8.8, 17.6]).all()
