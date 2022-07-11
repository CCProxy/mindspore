mindspore.ops.gamma
====================

.. py:function:: mindspore.ops.gamma(shape, alpha, beta, seed=None)

    ����٤��ֲ��������������

    **������**

    - **shape** (tuple) - ָ�������������shape������ά�ȵ�Tensor��
    - **alpha** (Tensor) - :math:`\alpha` �ֲ��Ĳ�����Ӧ�ô���0����������Ϊfloat32��
    - **beta** (Tensor) - :math:`\beta` �ֲ��Ĳ�����Ӧ�ô���0����������Ϊfloat32��
    - **seed** (int) - ����������������ӣ������ǷǸ�����Ĭ��ΪNone������Ϊ0��

    **���أ�**

    Tensor��shape������ `shape` �� `alpha` �� `beta` �㲥���shape����������Ϊfloat32��

    **�쳣��**

    - **TypeError** �C `shape` ����tuple��
    - **TypeError** �C `alpha` �� `beta` ����Tensor��
    - **TypeError** �C `seed` ���������Ͳ���int��
    - **TypeError** �C `alpha` �� `beta` ���������Ͳ���float32��