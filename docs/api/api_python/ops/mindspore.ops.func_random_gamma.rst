mindspore.ops.gamma
====================

.. py:function:: mindspore.ops.gamma(shape, alpha, seed=None)

    ����٤��ֲ��������������

    **������**

    - **shape** (Tensor) - ָ�������������shape������ά�ȵ�Tensor��
    - **alpha** (Tensor) - :math:`\alpha` �ֲ��Ĳ�����Ӧ�ô���0����������Ϊhalf��float32����float64��
    - **seed** (int) - ����������������ӣ������ǷǸ�����Ĭ��ΪNone������Ϊ0��

    **���أ�**

    Tensor��shape������ `shape` �� `alpha` ƴ�Ӻ��shape���������ͺ�alphaһ�¡�

    **�쳣��**
    
    - **TypeError** �C `shape` ����Tensor��
    - **TypeError** �C `alpha` ����Tensor��
    - **TypeError** �C `seed` ���������Ͳ���int��
    - **TypeError** �C `alpha` ���������Ͳ���half��float32����float64��
