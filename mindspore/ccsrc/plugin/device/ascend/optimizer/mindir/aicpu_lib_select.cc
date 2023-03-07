/**
 * Copyright 2022 Huawei Technologies Co., Ltd
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

#include "plugin/device/ascend/optimizer/mindir/aicpu_lib_select.h"
#include "plugin/device/ascend/kernel/aicpu/aicpu_util.h"
#include <set>
#include <string>
#include "include/common/utils/utils.h"
#include "include/backend/anf_runtime_algorithm.h"
#include "include/common/utils/anfalgo.h"

namespace mindspore {
namespace opt {
bool AICpuLibSelectPass::Process(const AnfNodePtr &node) const {
  static const std::set<std::string> kAICpuOpNames = {kDropoutGenMaskOpName,
                                                      kEnvironCreateOpName,
                                                      kEnvironSetOpName,
                                                      kEnvironGetOpName,
                                                      kEnvironDestroyAllOpName,
                                                      kPriorityReplayBufferCreate,
                                                      kPriorityReplayBufferPush,
                                                      kPriorityReplayBufferSample,
                                                      kPriorityReplayBufferUpdate,
                                                      kPriorityReplayBufferDestroy,
                                                      kReservoirReplayBufferCreate,
                                                      kReservoirReplayBufferPush,
                                                      kReservoirReplayBufferSample,
                                                      kReservoirReplayBufferDestroy,
                                                      kGatherDGradV2OpName,
                                                      kConcatOffsetOpName,
                                                      kSequenceAddOpName,
                                                      kSequenceAddOffsetOpName,
                                                      kSliceGradOpName,
                                                      kRandomShuffleOpName,
                                                      kRangeOpName,
                                                      kQuantDTypeCastOpName,
                                                      kFSEDecodeOpName,
                                                      kExpandDimsOpName};
  static const std::set<std::string> kMigrateAicpuKernelOps = {mindspore::kAdaptiveAvgPool2dOpName,
                                                               mindspore::kAdaptiveAvgPool2dGradOpName,
                                                               mindspore::kBucketizeOpName,
                                                               mindspore::kCauchyOpName,
                                                               mindspore::kChannelShuffleOpName,
                                                               mindspore::kCholeskyOpName,
                                                               mindspore::kCholeskyGradOpName,
                                                               mindspore::kCholeskyInverseOpName,
                                                               mindspore::kCholeskySolveOpName,
                                                               mindspore::kCol2ImOpName,
                                                               mindspore::kCombinedNonMaxSuppressionOpName,
                                                               mindspore::kComplexOpName,
                                                               mindspore::kComplexAbsOpName,
                                                               mindspore::kConcatOpName,
                                                               mindspore::kCosOpName,
                                                               mindspore::kCountNonZeroOpName,
                                                               mindspore::kCumulativeLogsumexpOpName,
                                                               mindspore::kCumProdOpName,
                                                               mindspore::kCSRSparseMatrixToDenseOpName,
                                                               mindspore::kCSRSparseMatrixToSparseTensorOpName,
                                                               mindspore::kDataFormatVecPermuteOpName,
                                                               mindspore::kFillV2OpName,
                                                               mindspore::kLogMatrixDeterminantOpName,
                                                               mindspore::kMatrixSolveLsOpName,
                                                               mindspore::kMedianOpName,
                                                               mindspore::kACosGradOpName,
                                                               mindspore::kAcoshGradOpName,
                                                               mindspore::kAdaptiveAvgPool3DOpName,
                                                               mindspore::kAdaptiveAvgPool3DGradOpName,
                                                               mindspore::kAdaptiveMaxPool2DGradOpName,
                                                               mindspore::kAdaptiveMaxPool3DOpName,
                                                               mindspore::kAdaptiveMaxPool3DGradOpName,
                                                               mindspore::kAddNOpName,
                                                               mindspore::kAddV2OpName,
                                                               mindspore::kAdjustContrastv2OpName,
                                                               mindspore::kAdjustHueOpName,
                                                               mindspore::kAdjustSaturationOpName,
                                                               mindspore::kAffineGridGradOpName,
                                                               mindspore::kAngleOpName,
                                                               mindspore::kArgmaxOpName,
                                                               mindspore::kArgMaxWithValueOpName,
                                                               mindspore::kArgminOpName,
                                                               mindspore::kArgMinWithValueOpName,
                                                               mindspore::KAsinGradOpName,
                                                               mindspore::KAsinhGradOpName,
                                                               mindspore::kAvgPoolV1OpName,
                                                               mindspore::kAvgPoolGradV1OpName,
                                                               mindspore::kNextAfterOpName,
                                                               mindspore::kBartlettWindowOpName,
                                                               mindspore::kBatchNormGradGradOpName,
                                                               mindspore::kBatchMatMulOpName,
                                                               mindspore::kCoalesceOpName,
                                                               mindspore::kDepthToSpaceOpName,
                                                               mindspore::kDenseToDenseSetOperation,
                                                               mindspore::kCropAndResizeGradBoxesOpName,
                                                               mindspore::kCropAndResizeGradImageOpName,
                                                               mindspore::kDivOpName,
                                                               mindspore::kReluGradOpName,
                                                               mindspore::kNonDeterministicIntsOpName,
                                                               mindspore::kMvlgammaOpName,
                                                               mindspore::kMvlgammaGradOpName,
                                                               mindspore::kFractionalMaxPoolWithFixedKsizeOpName,
                                                               mindspore::kFractionalMaxPool3DWithFixedKsizeOpName,
                                                               mindspore::kFractionalMaxPool3DGradWithFixedKsizeOpName,
                                                               mindspore::kPowOpName,
                                                               mindspore::kRandomPoissonOpName,
                                                               mindspore::kMatrixPowerOpName,
                                                               mindspore::kHammingWindowOpName,
                                                               mindspore::kMaxPool3DGradWithArgmaxOpName,
                                                               mindspore::kMaxPool3DWithArgmaxOpName,
                                                               mindspore::kMaxPoolGradV1OpName,
                                                               mindspore::kMaxUnpool2DOpName,
                                                               mindspore::kMaxUnpool2DGradOpName,
                                                               mindspore::kMaxUnpool3DOpName,
                                                               mindspore::kMaxUnpool3DGradOpName,
                                                               mindspore::kDivNoNanOpName,
                                                               mindspore::kRealOpName,
                                                               mindspore::kIgammaOpName,
                                                               mindspore::kIgammacOpName,
                                                               mindspore::kIgammaGradAOpName,
                                                               mindspore::kImagOpName,
                                                               mindspore::kSliceOpName,
                                                               mindspore::kInstanceNormV2OpName,
                                                               mindspore::kSparseSegmentMeanGradOpName,
                                                               mindspore::kSparseSegmentMeanWithNumSegmentsOpName,
                                                               mindspore::kListDiffOpName,
                                                               mindspore::kLogOpName,
                                                               mindspore::kTruncatedNormal,
                                                               mindspore::kTraceOpName,
                                                               mindspore::kTraceGradOpName,
                                                               mindspore::kTridiagonalSolveOpName,
                                                               mindspore::kSparseTensorToCSRSparseMatrixOpName,
                                                               mindspore::kSparseTensorDenseAddOpName,
                                                               mindspore::kSparseTensorDenseMatmulOpName,
                                                               mindspore::kSparseSoftmaxOpName,
                                                               mindspore::kSparseSliceOpName,
                                                               mindspore::kSparseSliceGradOpName,
                                                               mindspore::kSparseReorderOpName,
                                                               mindspore::kSparseCrossOpName,
                                                               mindspore::kSetSizeOpName,
                                                               mindspore::kLogSpaceOpName,
                                                               mindspore::kSegmentMeanOpName,
                                                               mindspore::kSegmentProdOpName,
                                                               mindspore::kSegmentSumOpName,
                                                               mindspore::kSegmentMinOpName,
                                                               mindspore::kInstanceNormV2GradOpName,
                                                               mindspore::kLayerNormGradGradOpName,
                                                               mindspore::kExpm1OpName,
                                                               mindspore::kBiasAddOpName,
                                                               mindspore::kBiasAddGradOpName,
                                                               mindspore::kBincountOpName,
                                                               mindspore::kBlackmanWindowOpName,
                                                               mindspore::kBroadcastToOpName,
                                                               mindspore::kMedianGradOpName,
                                                               mindspore::kNMSWithMaskOpName,
                                                               mindspore::kReduceSumOpName,
                                                               mindspore::kSpaceToDepthOpName,
                                                               mindspore::kSparseAddmmOpName,
                                                               mindspore::kSparseApplyAdagradDAOpName,
                                                               mindspore::kSparseApplyCenteredRMSPropOpName,
                                                               mindspore::kSparseApplyMomentumOpName,
                                                               mindspore::kSparseApplyProximalGradientDescentOpName,
                                                               mindspore::kSparseConcatOpName,
                                                               mindspore::kSparseDenseCwiseAddOpName,
                                                               mindspore::kSparseDenseCwiseDivOpName,
                                                               mindspore::kSparseDenseCwiseMulOpName,
                                                               mindspore::kSparseMatrixMatMulOpName,
                                                               mindspore::kSparseMatrixNNZOpName,
                                                               mindspore::kSparseMatrixTransposeOpName,
                                                               mindspore::kSparseFillEmptyRowsGradOpName,
                                                               mindspore::kSparseReshapeOpName,
                                                               mindspore::kSparseSegmentSqrtNGradOpName,
                                                               mindspore::kSparseSegmentSqrtNWithNumSegmentsOpName,
                                                               mindspore::kSparseSoftmaxCrossEntropyWithLogitsV2OpName,
                                                               mindspore::kSparseSparseMaximumOpName,
                                                               mindspore::kSparseSparseMinimumOpName,
                                                               mindspore::kSparseSegmentSumWithNumSegmentsOpName,
                                                               mindspore::kSplitOpName,
                                                               mindspore::kSqrtOpName,
                                                               mindspore::kSqrtGradOpName,
                                                               mindspore::kTanhOpName,
                                                               mindspore::kTileOpName,
                                                               mindspore::kTridiagonalMatMulOpName,
                                                               mindspore::kTripletMarginLossOpName,
                                                               mindspore::kTransposeOpName,
                                                               mindspore::kTriuIndicesOpName,
                                                               mindspore::kTrilIndicesOpName,
                                                               mindspore::kUnstackOpName,
                                                               mindspore::kUnravelIndexOpName,
                                                               mindspore::kUnsortedSegmentSumOpName,
                                                               mindspore::kUpperBoundOpName,
                                                               mindspore::kXlogyOpName,
                                                               mindspore::kXdivyOpName,
                                                               mindspore::kFFTWithSizeOpName,
                                                               mindspore::kHistogramOpName,
                                                               mindspore::kIm2ColOpName,
                                                               mindspore::kGatherNdOpName,
                                                               mindspore::kScatterNdOpName,
                                                               mindspore::kScatterNdUpdateOpName,
                                                               mindspore::kTensorScatterUpdateOpName,
                                                               mindspore::kIsNanOpName,
                                                               mindspore::kMatrixDeterminantOpName,
                                                               mindspore::kMatrixLogarithmOpName,
                                                               mindspore::kMatrixSetDiagV3OpName,
                                                               mindspore::kMultinomialOpName,
                                                               mindspore::kNanToNumOpName,
                                                               mindspore::kQrOpName,
                                                               mindspore::kResizeAreaOpName,
                                                               mindspore::kResizeBicubicOpName,
                                                               mindspore::kResizeBicubicGradOpName,
                                                               mindspore::kQuantileOpName,
                                                               mindspore::kSparseSegmentSqrtNOpName,
                                                               mindspore::kUnsortedSegmentProdOpName,
                                                               mindspore::kExpOpName,
                                                               mindspore::kMatrixTriangularSolveOpName,
                                                               mindspore::kMaximumGradGradOpName,
                                                               mindspore::kMaxPoolV1OpName,
                                                               mindspore::kMinimumGradGradOpName,
                                                               mindspore::kMulNoNanOpName,
                                                               mindspore::kMultilabelMarginLossGradOpName,
                                                               mindspore::kNthElementOpName,
                                                               mindspore::kResizeNearestNeighborV2OpName,
                                                               mindspore::kResizeNearestNeighborV2GradOpName,
                                                               mindspore::kNonMaxSuppressionWithOverlapsOpName,
                                                               mindspore::kOneHotOpName,
                                                               mindspore::kOrgqrOpName,
                                                               mindspore::kStackOpName,
                                                               mindspore::kParameterizedTruncatedNormalOpName,
                                                               mindspore::kPolarOpName,
                                                               mindspore::kPdistGradOpName,
                                                               mindspore::kRaggedRangeOpName,
                                                               mindspore::kRaggedTensorToSparseOpName,
                                                               mindspore::kRaggedTensorToTensorOpName,
                                                               mindspore::kReciprocalOpName,
                                                               mindspore::kReciprocalGradOpName,
                                                               mindspore::kReduceMeanOpName,
                                                               mindspore::kReduceProdOpName,
                                                               mindspore::kReLUV3OpName,
                                                               mindspore::kReverseV2OpName,
                                                               mindspore::kRGBToHSVOpName,
                                                               mindspore::kRsqrtGradOpName,
                                                               mindspore::kSampleDistortedBoundingBoxV2OpName,
                                                               mindspore::kScaleAndTranslateOpName,
                                                               mindspore::kScaleAndTranslateGradOpName,
                                                               mindspore::kScatterNdOpName,
                                                               mindspore::kScatterNdUpdateOpName,
                                                               mindspore::kSelectOpName,
                                                               mindspore::kSelfAdjointEigOpName,
                                                               mindspore::kSinOpName,
                                                               mindspore::kSincOpName,
                                                               mindspore::kSinhOpName,
                                                               mindspore::kSmoothL1LossGradOpName,
                                                               mindspore::kSmoothL1LossOpName,
                                                               mindspore::kSignOpName,
                                                               mindspore::kCheckNumericsOpName,
                                                               mindspore::kFloorDivOpName,
                                                               mindspore::kLog1pOpName,
                                                               mindspore::kMulOpName,
                                                               mindspore::kMaskedSelectOpName,
                                                               mindspore::kMaskedSelectGradOpName,
                                                               mindspore::kConjOpName,
                                                               mindspore::kZerosLikeOpName,
                                                               mindspore::kMatrixBandPartOpName,
                                                               mindspore::kDenseToCSRSparseMatrixOpName,
                                                               mindspore::kDenseToSparseSetOperation,
                                                               mindspore::kDiagOpName,
                                                               mindspore::kDiagonalOpName,
                                                               mindspore::kDiagPartOpName,
                                                               mindspore::kEigOpName,
                                                               mindspore::kEyeOpName,
                                                               mindspore::kFmaxOpName,
                                                               mindspore::kFminOpName,
                                                               mindspore::kFractionalAvgPoolOpName,
                                                               mindspore::kFractionalAvgPoolGradOpName,
                                                               mindspore::kFractionalMaxPoolOpName,
                                                               mindspore::kFractionalMaxPoolGradOpName,
                                                               mindspore::kFractionalMaxPoolGradWithFixedKsizeOpName,
                                                               mindspore::kGatherNdOpName,
                                                               mindspore::kGcdOpName,
                                                               mindspore::kGeqrfOpName,
                                                               mindspore::kHSigmoidOpName,
                                                               mindspore::kHSigmoidGradOpName,
                                                               mindspore::kHeavisideOpName,
                                                               mindspore::kHypotOpName,
                                                               mindspore::kIdentityNOpName,
                                                               mindspore::kIndexFillOpName,
                                                               mindspore::kKLDivLossOpName,
                                                               mindspore::kKLDivLossGradOpName,
                                                               mindspore::kLcmOpName,
                                                               mindspore::kLogitOpName,
                                                               mindspore::kLogitGradOpName,
                                                               mindspore::kLowerBoundOpName,
                                                               mindspore::kLstsqOpName,
                                                               mindspore::kLuUnpackOpName,
                                                               mindspore::kLuUnpackGradOpName,
                                                               mindspore::kMatMulOpName,
                                                               mindspore::kMatrixExpOpName,
                                                               mindspore::kPadV3GradOpName,
                                                               mindspore::kPadV3OpName,
                                                               mindspore::kLogicalXorOpName,
                                                               mindspore::kLogNormalReverseOpName,
                                                               mindspore::kBetaincOpName,
                                                               mindspore::kLessEqualOpName,
                                                               mindspore::kHSVToRGBOpName,
                                                               mindspore::kLuSolveOpName,
                                                               mindspore::kExtractGlimpseOpName,
                                                               mindspore::kMatrixSolveOpName,
                                                               mindspore::kIsInfOpName,
                                                               mindspore::kMaskedSelectOpName,
                                                               mindspore::kMaskedSelectGradOpName,
                                                               mindspore::kMultiMarginLossOpName,
                                                               mindspore::kMatrixInverseOpName,
                                                               mindspore::kMultiMarginLossGradOpName,
                                                               mindspore::kSspaddmmOpName,
                                                               mindspore::kDeformableOffsetsOpName,
                                                               mindspore::kDeformableOffsetsGradOpName,
                                                               mindspore::kBatchMatMulOpName,
                                                               mindspore::kSparseToDenseV2OpName,
                                                               mindspore::kTrilOpName,
                                                               mindspore::kBernoulliOpName,
                                                               mindspore::kGluOpName,
                                                               mindspore::kGluGradOpName};

  static const std::string kEnvOpSoNames = "mindspore_aicpu_kernels";
  static const std::string kCpuKernelSoName = "mindspore_cpu_kernels";

  if (!node->isa<CNode>()) {
    return false;
  }
  auto kernel_name = common::AnfAlgo::GetCNodeName(node);
  if (kAICpuOpNames.find(kernel_name) != kAICpuOpNames.end()) {
    common::AnfAlgo::SetNodeAttr(kAttrCustAicpu, MakeValue(kEnvOpSoNames), node);
  }
  if (kMigrateAicpuKernelOps.find(kernel_name) != kMigrateAicpuKernelOps.end()) {
    common::AnfAlgo::SetNodeAttr(kAttrCustAicpu, MakeValue(kCpuKernelSoName), node);
  }

  return true;
}
}  // namespace opt
}  // namespace mindspore
