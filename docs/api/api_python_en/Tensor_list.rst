.. role:: hidden
    :class: hidden-section

.. currentmodule:: {{ module }}

{% if objname in ["AdaSumByDeltaWeightWrapCell", "AdaSumByGradWrapCell", "DistributedGradReducer"] %}
{{ fullname | underline }}

.. autoclass:: {{ name }}
    :exclude-members: infer_value, infer_shape, infer_dtype, construct
    :members:

{% elif fullname=="mindspore.Tensor" %}
{{ fullname | underline }}

.. autoclass:: {{ name }}

Neural Network Layer Methods
----------------------------

Neural Network
^^^^^^^^^^^^^^

.. msplatformautosummary::
    :toctree: Tensor
    :nosignatures:

    mindspore.Tensor.flatten

Activation Function
^^^^^^^^^^^^^^^^^^^

.. msplatformautosummary::
    :toctree: Tensor
    :nosignatures:

    mindspore.Tensor.hardshrink
    mindspore.Tensor.soft_shrink

Mathematical Methods
--------------------

Element-wise Methods
^^^^^^^^^^^^^^^^^^^^

.. msplatformautosummary::
    :toctree: Tensor
    :nosignatures:
    
    mindspore.Tensor.abs
    mindspore.Tensor.addcdiv
    mindspore.Tensor.addcmul
    mindspore.Tensor.atan2
    mindspore.Tensor.bernoulli
    mindspore.Tensor.bitwise_and
    mindspore.Tensor.bitwise_or
    mindspore.Tensor.bitwise_xor
    mindspore.Tensor.ceil
    mindspore.Tensor.cosh
    mindspore.Tensor.erf
    mindspore.Tensor.erfc
    mindspore.Tensor.inv
    mindspore.Tensor.invert
    mindspore.Tensor.lerp
    mindspore.Tensor.log1p
    mindspore.Tensor.logit
    mindspore.Tensor.pow
    mindspore.Tensor.round
    mindspore.Tensor.std
    mindspore.Tensor.svd
    mindspore.Tensor.tan
    mindspore.Tensor.var
    mindspore.Tensor.xdivy
    mindspore.Tensor.xlogy

Reduction Methods
^^^^^^^^^^^^^^^^^

.. msplatformautosummary::
    :toctree: Tensor
    :nosignatures:

    mindspore.Tensor.argmax
    mindspore.Tensor.argmin
    mindspore.Tensor.argmin_with_value
    mindspore.Tensor.max
    mindspore.Tensor.mean
    mindspore.Tensor.median
    mindspore.Tensor.min
    mindspore.Tensor.norm
    mindspore.Tensor.prod
    mindspore.Tensor.renorm

Comparison Methods
^^^^^^^^^^^^^^^^^^

.. msplatformautosummary::
    :toctree: Tensor
    :nosignatures:

    mindspore.Tensor.all
    mindspore.Tensor.any
    mindspore.Tensor.approximate_equal
    mindspore.Tensor.has_init
    mindspore.Tensor.isclose
    mindspore.Tensor.top_k

Linear Algebraic Methods
^^^^^^^^^^^^^^^^^^^^^^^^

.. msplatformautosummary::
    :toctree: Tensor
    :nosignatures:

    mindspore.Tensor.ger
    mindspore.Tensor.log_matrix_determinant
    mindspore.Tensor.matrix_determinant

Tensor Operation Methods
------------------------

Tensor Construction
^^^^^^^^^^^^^^^^^^^

.. msplatformautosummary::
    :toctree: Tensor
    :nosignatures:

    mindspore.Tensor.choose
    mindspore.Tensor.fill
    mindspore.Tensor.fills
    mindspore.Tensor.view

Random Generation Methods
^^^^^^^^^^^^^^^^^^^^^^^^^^

.. msplatformautosummary::
    :toctree: Tensor
    :nosignatures:

    mindspore.Tensor.random_categorical

Array Methods
^^^^^^^^^^^^^

.. msplatformautosummary::
    :toctree: Tensor
    :nosignatures:

    mindspore.Tensor.broadcast_to
    mindspore.Tensor.col2im
    mindspore.Tensor.copy
    mindspore.Tensor.cummax
    mindspore.Tensor.cummin
    mindspore.Tensor.cumsum
    mindspore.Tensor.diag
    mindspore.Tensor.diagonal
    mindspore.Tensor.dtype
    mindspore.Tensor.expand_as
    mindspore.Tensor.expand_dims
    mindspore.Tensor.gather
    mindspore.Tensor.gather_elements
    mindspore.Tensor.gather_nd
    mindspore.Tensor.index_fill
    mindspore.Tensor.init_data
    mindspore.Tensor.inplace_update
    mindspore.Tensor.item
    mindspore.Tensor.itemset
    mindspore.Tensor.itemsize
    mindspore.Tensor.masked_fill
    mindspore.Tensor.masked_select
    mindspore.Tensor.nbytes
    mindspore.Tensor.ndim
    mindspore.Tensor.nonzero
    mindspore.Tensor.narrow
    mindspore.Tensor.ptp
    mindspore.Tensor.ravel
    mindspore.Tensor.repeat
    mindspore.Tensor.reshape
    mindspore.Tensor.resize
    mindspore.Tensor.scatter_add
    mindspore.Tensor.scatter_div
    mindspore.Tensor.scatter_max
    mindspore.Tensor.scatter_min
    mindspore.Tensor.scatter_mul
    mindspore.Tensor.scatter_sub
    mindspore.Tensor.searchsorted
    mindspore.Tensor.select
    mindspore.Tensor.shape
    mindspore.Tensor.size
    mindspore.Tensor.split
    mindspore.Tensor.squeeze
    mindspore.Tensor.strides
    mindspore.Tensor.sum
    mindspore.Tensor.swapaxes
    mindspore.Tensor.T
    mindspore.Tensor.take
    mindspore.Tensor.to_tensor
    mindspore.Tensor.trace
    mindspore.Tensor.transpose
    mindspore.Tensor.unique_consecutive
    mindspore.Tensor.unique_with_pad
    mindspore.Tensor.unsorted_segment_max
    mindspore.Tensor.unsorted_segment_min
    mindspore.Tensor.unsorted_segment_prod

Type Conversion
^^^^^^^^^^^^^^^

.. msplatformautosummary::
    :toctree: Tensor
    :nosignatures:

    mindspore.Tensor.asnumpy
    mindspore.Tensor.astype
    mindspore.Tensor.from_numpy
    mindspore.Tensor.to_coo
    mindspore.Tensor.to_csr

Gradient Clipping
^^^^^^^^^^^^^^^^^

.. msplatformautosummary::
    :toctree: Tensor
    :nosignatures:

    mindspore.Tensor.clip

Parameter Operation Methods
--------------------

.. msplatformautosummary::
    :toctree: Tensor
    :nosignatures:

    mindspore.Tensor.assign_value

Other Methods
--------------------

.. msplatformautosummary::
    :toctree: Tensor
    :nosignatures:

    mindspore.Tensor.flush_from_cache

{% elif objname[0].istitle() %}
{{ fullname | underline }}

.. autoclass:: {{ name }}
    :exclude-members: infer_value, infer_shape, infer_dtype
    :members:

{% else %}
{{ fullname | underline }}

.. autofunction:: {{ fullname }}
{% endif %}

..
  autogenerated from _templates/classtemplate.rst
  note it does not have :inherited-members:
