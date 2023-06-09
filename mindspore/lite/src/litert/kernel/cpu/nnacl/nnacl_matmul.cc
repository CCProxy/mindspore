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

#include "nnacl/nnacl_matmul.h"
#include "nnacl/nnacl_manager.h"
#include "include/errorcode.h"
#include "nnacl/kernel/matmul_f32_base.h"
#include "nnacl/cxx_utils.h"
#include "src/litert/pack_weight_manager.h"
#if defined(PARALLEL_INFERENCE) && defined(ENABLE_MINDRT)
#include "thread/parallel_thread_pool_manager.h"
#endif

using mindspore::lite::RET_ERROR;
using mindspore::lite::RET_OK;
using mindspore::schema::PrimitiveType_FullConnection;
using mindspore::schema::PrimitiveType_MatMulFusion;

namespace mindspore::nnacl {
int MatmulKernel::Prepare() {
  if (kernel_ == nullptr) {
    return RET_ERROR;
  }

  MatmulFp32Struct *matmul = reinterpret_cast<MatmulFp32Struct *>(kernel_);
  matmul->pack_weight_manager_ = lite::PackWeightManager::GetInstance();
  matmul->get_pack_data_by_sharing_weight_ = nnacl::DefaultGetSharingPackData;
  matmul->free_by_sharing_weight_ = nnacl::DefaultFreeSharingPackData;
  matmul->infer_shape_ = InferShapeDone();
  matmul->a_const_ = in_tensors_[FIRST_INPUT]->IsConst() && !op_parameter_->is_train_session_;
  matmul->b_const_ = in_tensors_[SECOND_INPUT]->IsConst() && !op_parameter_->is_train_session_;
  int ret = kernel_->prepare(kernel_);
  if (ret != RET_OK) {
    return ret;
  }

  if (!InferShapeDone()) {
    return RET_OK;
  }
  return ReSize();
}

int MatmulKernel::ReSize() {
  CHECK_NULL_RETURN(kernel_);
  MatmulFp32Struct *matmul = reinterpret_cast<MatmulFp32Struct *>(kernel_);

  matmul->model_thread_nr_ = kernel_->thread_nr_;
#if defined(PARALLEL_INFERENCE) && defined(ENABLE_MINDRT)
  auto num = ParallelThreadPoolManager::GetInstance()->GetThreadPoolSize(ms_context_->thread_pool_);
  matmul->model_thread_nr_ = (num != -1) ? (num) : (kernel_->thread_nr_);
#endif

  UpdateTensorC();
  auto ret = kernel_->resize(kernel_);
  set_workspace_size(kernel_->work_size_);
  return ret;
}

int MatmulKernel::PreparePackedWeight(const lite::Tensor *tensor) {
  CHECK_NULL_RETURN(kernel_);
  MatmulFp32Struct *matmul = reinterpret_cast<MatmulFp32Struct *>(kernel_);
  matmul->weight_is_packed_ = true;
  return RET_OK;
}

NNACL_KERNEL(PrimitiveType_MatMulFusion, kNumberTypeFloat32, NnaclOpt<MatmulKernel>)
NNACL_KERNEL(PrimitiveType_FullConnection, kNumberTypeFloat32, NnaclOpt<MatmulKernel>)
}  // namespace mindspore::nnacl
