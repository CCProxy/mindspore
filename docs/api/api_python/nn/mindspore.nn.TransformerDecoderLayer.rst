mindspore.nn.TransformerDecoderLayer
========================================

.. py:class:: mindspore.nn.TransformerDecoderLayer(d_model: int, nhead: int, dim_feedforward: int = 2048, dropout: float = 0.1, activation: Union[str, Cell] = 'relu', layer_norm_eps: float = 1e-5, batch_first: bool = False, norm_first: bool = False)

    Transformer的解码器层。Transformer解码器的单层实现，包括Self Attention层、MultiheadAttention层和FeedForward层。

    参数：
        - **d_model** (int) - 输入的特征数。
        - **nhead** (int) - 注意力头的数量。
        - **dim_feedforward** (int) - FeedForward层的维数。默认：2048。
        - **dropout** (float) - 随机丢弃比例。默认：0.1。
        - **activation** (str, Cell) - 中间层的激活函数，可以输入字符串、函数接口或激活函数层实例。支持"relu"、"gelu"。默认："relu"。
        - **layer_norm_eps** (float) - LayerNorm层的eps值，默认：1e-5。
        - **batch_first** (bool) - 如果为 ``True`` 则输入输出Shape为(batch, seq, feature)，反之，Shape为(seq, batch, feature)。默认： ``False``。
        - **norm_first** (bool) - 如果为 ``True``， 则LayerNorm层位于Self Attention层、MultiheadAttention层和FeedForward层之前，反之，位于其后。默认： ``False``。

    输入：
        - **tgt** (Tensor) - 目标序列。
        - **memory** (Tensor) - TransformerEncoder的最后一层输出序列。
        - **tgt_mask** (Tensor) - 目标序列的掩码矩阵 (可选)。默认：None。
        - **memory_mask** (Tensor) - memory序列的掩码矩阵 (可选)。默认：None。
        - **tgt_key_padding_mask** (Tensor) - 目标序列Key矩阵的掩码矩阵 (可选)。默认：None。
        - **memory_key_padding_mask** (Tensor) - memory序列Key矩阵的掩码矩阵 (可选)。默认：None。

    输出：
        Tensor。
