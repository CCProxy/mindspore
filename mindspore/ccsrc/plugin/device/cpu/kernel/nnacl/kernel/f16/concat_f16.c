/**
 * Copyright 2023 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "nnacl/kernel/f16/concat_f16.h"
#include "nnacl/kernel/concat.h"
#include "nnacl/fp16/cast_fp16.h"

typedef struct ConcatF16Struct {
  ConcatStruct concat_;
  void **tmp_buffer_; /* in_size + out_size */
} ConcatF16Struct;

int ConcatEnsureFp16InputsAndOutput(ConcatF16Struct *concat_f16) {
  ConcatStruct *concat = &concat_f16->concat_;

  int tmp_buffer_size = (concat->base_.in_size_ + concat->base_.out_size_) * sizeof(float16_t *);
  concat_f16->tmp_buffer_ = concat->base_.env_->alloc(concat->base_.env_->allocator_, tmp_buffer_size);
  NNACL_CHECK_NULL_RETURN_ERR(concat_f16->tmp_buffer_);
  memset(concat_f16->tmp_buffer_, 0, tmp_buffer_size);

  for (size_t i = 0; i < concat->base_.in_size_; ++i) {
    if (!concat->is_with_data_[i]) {
      continue;
    }

    TensorC *t = concat->base_.in_[i];
    NNACL_CHECK_NULL_RETURN_ERR(t);
    NNACL_CHECK_NULL_RETURN_ERR(t->data_);

    if (concat->base_.in_[i]->data_type_ == kNumberTypeFloat16) {
      concat->inputs_ptr_[i] = t->data_;
      continue;
    }

    if (t->data_type_ == kNumberTypeFloat32 || t->data_type_ == kNumberTypeFloat) {
      void *tmp = concat->base_.env_->alloc(concat->base_.env_->allocator_, GetElementNum(t) * sizeof(float16_t));
      NNACL_CHECK_NULL_RETURN_ERR(t);

      concat->inputs_ptr_[i] = tmp;
      concat_f16->tmp_buffer_[i] = tmp;
      Float32ToFloat16((float *)(t->data_), (float16_t *)(tmp), GetElementNum(t));
      continue;
    }
    return NNACL_CONCAT_F16_INVALID_DATA_TYPE;
  }

  TensorC *t = concat->base_.out_[0];
  if (t->data_type_ == kNumberTypeFloat16) {
    concat->output_ = t->data_;
    return NNACL_OK;
  }

  if (t->data_type_ == kNumberTypeFloat32 || t->data_type_ == kNumberTypeFloat) {
    concat->output_ = concat->base_.env_->alloc(concat->base_.env_->allocator_, GetElementNum(t) * sizeof(float16_t));
    NNACL_CHECK_NULL_RETURN_ERR(concat->output_);
    concat_f16->tmp_buffer_[concat->base_.in_size_] = concat->output_;
    return NNACL_OK;
  }
  return NNACL_CONCAT_F16_INVALID_DATA_TYPE;
}

int ConcatFp16Run(void *cdata, int task_id, float l, float r) {
  ConcatF16Struct *concat_f16 = (ConcatF16Struct *)cdata;
  NNACL_CHECK_NULL_RETURN_ERR(concat_f16);
  ConcatStruct *concat = &concat_f16->concat_;
  return DoConcat(concat, task_id);
}

void ConcatF16FreeTmpBuffer(ConcatF16Struct *concat_f16) {
  for (int i = 0; i < (concat_f16->concat_.base_.in_size_ + concat_f16->concat_.base_.out_size_); i++) {
    if (concat_f16->tmp_buffer_[i] != NULL) {
      concat_f16->concat_.base_.env_->free(concat_f16->concat_.base_.env_->allocator_, concat_f16->tmp_buffer_[i]);
    }
    concat_f16->tmp_buffer_[i] = NULL;
  }

  if (concat_f16->tmp_buffer_ != NULL) {
    concat_f16->concat_.base_.env_->free(concat_f16->concat_.base_.env_->allocator_, concat_f16->tmp_buffer_);
  }
  concat_f16->tmp_buffer_ = NULL;
}

int concat_f16_compute(KernelBase *self) {
  ConcatF16Struct *concat_f16 = (ConcatF16Struct *)self;
  NNACL_CHECK_NULL_RETURN_ERR(concat_f16);
  ConcatStruct *concat = &concat_f16->concat_;

  if (concat->outer_size_ == 0 || concat->inner_sizes_[self->in_size_] == 0) {
    return NNACL_OK;
  }

  int ret = ConcatEnsureFp16InputsAndOutput(concat_f16);
  if (ret != NNACL_OK) {
    ConcatF16FreeTmpBuffer(concat_f16);
    return ret;
  }

  NNACL_CHECK_NULL_RETURN_ERR(concat->output_);
  ret = self->env_->parallel_launch(self->env_->thread_pool_, ConcatFp16Run, self, self->thread_nr_);
  if (ret == NNACL_OK) {
    TensorC *output_tensor = concat->base_.out_[FIRST_INPUT];
    if (output_tensor->data_type_ == kNumberTypeFloat32 || output_tensor->data_type_ == kNumberTypeFloat) {
      float *output = concat->base_.out_[FIRST_INPUT]->data_;
      if (output == NULL) {
        ret = NNACL_CONCAT_F16_OUTPUT_DATA_INVALID;
      } else {
        Float16ToFloat32((float16_t *)concat->output_, output, GetElementNum(output_tensor));
      }
    }
  }

  ConcatF16FreeTmpBuffer(concat_f16);
  return ret;
}

KernelBase *CreateConcatF16(OpParameter *param, int data_type) {
  ConcatF16Struct *concat_f16 = (ConcatF16Struct *)malloc(sizeof(ConcatF16Struct));
  NNACL_CHECK_NULL_RETURN_NULL(concat_f16);
  memset(concat_f16, 0, sizeof(ConcatF16Struct));

  ConcatStruct *concat = &concat_f16->concat_;
  concat->data_type_ = kNumberTypeFloat16;
  concat->inner_sizes_ = NULL;
  concat->inputs_ptr_ = NULL;
  concat->is_with_data_ = NULL;
  concat->base_.prepare = concat_prepare;
  concat->base_.resize = concat_resize;
  concat->base_.release = concat_release;
  concat->base_.compute = concat_f16_compute;
  concat_f16->tmp_buffer_ = NULL;
  return (KernelBase *)concat;
}

REG_KERNEL_CREATOR(PrimType_Concat, kNumberTypeFloat16, CreateConcatF16)
