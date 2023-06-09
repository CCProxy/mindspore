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
import os
import pytest
import tests.st.ge.ge_test_utils as utils


@pytest.mark.level1
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_avg_pool_grad():
    """
    Description: Auto-diff AvgPool in ge backend
    Expectation: success
    """
    utils.run_testcase('pass_avg_pool_grad')


@pytest.mark.level1
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_dropout():
    """
    Description: run dropout and dropoutgrad in ge backend
    Expectation: success
    """
    utils.run_testcase('pass_dropout')


@pytest.mark.level1
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_tensor_array():
    """
    Description: run TensorArray in ge backend
    Expectation: success
    """
    utils.run_testcase('pass_tensor_array')


@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_reduce_axis_update():
    """
    Description: test axis of reduce operator is empty
    Expectation: success
    """
    os.environ['MS_DEV_JIT_SYNTAX_LEVEL'] = '0'
    utils.run_testcase('pass_reduce_axis_update')
    os.environ['MS_DEV_JIT_SYNTAX_LEVEL'] = '2'


@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_convert_attr_to_input():
    """
    Description: test convert attr to input
    Expectation: success
    """
    os.environ['MS_DEV_JIT_SYNTAX_LEVEL'] = '0'
    utils.run_testcase('pass_convert_attr_to_input')
    os.environ['MS_DEV_JIT_SYNTAX_LEVEL'] = '2'


@pytest.mark.level1
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_convert_resize_nearest_neighbor_x_dtype():
    """
    Description: test convert ReszieNearestNeighborX dytpe
    Expectation: success
    """
    utils.run_testcase('pass_convert_resize_nearest_neighbor_x_dtype')
