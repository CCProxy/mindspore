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
from mindspore import Tensor, ms_function, context

context.set_context(mode=context.GRAPH_MODE)


def test_for_after_if_1():
    """
    Feature: JIT Fallback
    Description: Test fallback with control flow.
    Expectation: No exception.
    """

    @ms_function
    def func():
        x = Tensor([0])
        y = Tensor([0])
        if x == y:
            x = x + 3
        for _ in range(3):
            y = y + 1
        return x + y

    res = func()
    assert res == 6


def test_for_after_if_4():
    """
    Feature: JIT Fallback
    Description: Test fallback with control flow.
    Expectation: No exception.
    """

    @ms_function
    def func():
        x = Tensor([1])
        y = Tensor([2])
        if max(x, y) == Tensor([2]):
            x = x + min(x, y)

        z = (Tensor(1), Tensor(2), Tensor(3))
        for i in zip(z):
            x = x * i
        return x

    res = func()
    assert res == 12
