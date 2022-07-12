mindspore.ops.smooth_l1_loss
============================

.. py:function:: mindspore.ops.smooth_l1_loss(logits, labels, beta=1.0)

    计算平滑L1损失，该L1损失函数有稳健性。

    平滑L1损失是一种类似于MSELoss的损失函数，但对异常值相对不敏感。参阅论文 `Fast R-CNN <https://arxiv.org/abs/1504.08083>`_ 。

    给定长度为 :math:`N` 的两个输入 `x` 和 `y` ，平滑L1损失的计算如下：

    .. math::
        L_{i} =
        \begin{cases}
        \frac{0.5 (x_i - y_i)^{2}}{\beta}, & \text{if } |x_i - y_i| < \beta \\
        |x_i - y_i| - 0.5 * \beta, & \text{otherwise. }
        \end{cases}

    其中， :math:`\beta` 代表阈值 `beta` 。 :math:`N` 为batch size。

    **参数：**

    - **logits** (Tensor) - shape： :math:`(N, *)` ，其中 :math:`*` 表示任意数量的附加维度。数据类型支持float16或float32。
    - **labels** (Tensor) - shape： :math:`(N, *)` ，与 `logits` 的shape和数据类型相同。
    - **beta** (float) - 控制损失函数在L1Loss和L2Loss间变换的阈值。默认值：1.0。

    **返回：**

    Tensor。输出Tensor的shape和数据类型同 `logits` 的一样。

    **异常：**

    - **TypeError** - `beta` 不是float类型。
    - **TypeError** - `logits` 或 `labels` 的数据类型非float16或float32。
    - **ValueError** - `beta` 小于或等于0。
    - **ValueError** - `logits` 与 `labels` 的shape不同。
