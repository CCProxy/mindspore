mindspore.ops.bessel_y1
=======================

.. py:function:: mindspore.ops.bessel_y1(x)

    逐元素计算并返回输入Tensor的Bessel y1函数值。

    参数：
        - **x** (Tensor) - 输入Tensor。数据类型应为float16，float32或float64。

    返回：
        Tensor，shape和数据类型与 `x` 相同。

    异常：
        - **TypeError** - `x` 不是Tensor。
        - **TypeError** - `x` 的数据类型不是float16，float32或float64。
