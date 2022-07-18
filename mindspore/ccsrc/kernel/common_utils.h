/**
 * Copyright 2019-2022 Huawei Technologies Co., Ltd
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
#ifndef MINDSPORE_CCSRC_KERNEL_COMMON_UTILS_H_
#define MINDSPORE_CCSRC_KERNEL_COMMON_UTILS_H_

#include <dirent.h>
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <string>
#include <sstream>
#include <algorithm>
#include <vector>
#include <utility>
#include <tuple>
#include <nlohmann/json.hpp>
#include "include/common/utils/utils.h"
#include "kernel/kernel.h"
#include "kernel/oplib/opinfo.h"
#include "kernel/kernel_build_info.h"
#include "ops/base_operator.h"

namespace mindspore {
namespace kernel {
constexpr auto kAkgKernelMeta = "kernel_meta/";
constexpr auto kProcessorAiCore = "aicore";
constexpr auto kProcessorAiCpu = "aicpu";
constexpr auto kProcessorCuda = "cuda";
constexpr auto kProcessorCpu = "cpu";
constexpr auto kProcessorUnknown = "unknown";
constexpr auto kJsonSuffix = ".json";
constexpr auto kInfoSuffix = ".info";
constexpr unsigned int AUTODIFF_COMPILE_OVERTIME = 600;

// an enum to indicate a vector or matrix alignment direction.
// real_data: [1,2,3] left_align: [1,2,3,0] right_align:[0,1,2,3]
namespace MatrixDiag {
enum Alignment { RIGHT = 0, LEFT = 1 };
}  // namespace MatrixDiag

struct KernelMetaInfo {
  uintptr_t func_stub_;
  uint32_t block_dim_;
};
using KernelMetaPtr = std::shared_ptr<KernelMetaInfo>;

class KernelMeta {
 public:
  KernelMeta() = default;
  void Initialize();
  std::string Search(const std::string &kernel_name) const;
  bool Insert(const std::string &kernel_name, const std::string &kernel_json);
  std::string kernel_meta_path() const { return kernel_meta_path_; }
  bool initialized() const { return initialized_; }
  static KernelMeta *GetInstance() {
    static KernelMeta kernel_meta;
    return &kernel_meta;
  }
  ~KernelMeta() = default;

 private:
  bool initialized_ = false;
  std::string kernel_meta_path_;
  std::unordered_map<std::string, std::string> kernel_meta_map_;
};

class MatrixInfo {
 public:
  explicit MatrixInfo(size_t max_index, const ShapeVector &matrix_shapes)
      : max_index_(max_index), shapes_(matrix_shapes) {
    current_indexes_.resize(shapes_.size(), 0);
  }
  ~MatrixInfo() = default;
  bool SetIndex(size_t start, size_t end) {
    // Check data from start to end whether valid.
    if (start < min_index || end > max_index_ || start >= end) {
      return false;
    }
    // Initial current indexes.
    int last_rank = SizeToInt(current_indexes_.size()) - 1;
    for (int i = last_rank; i >= 0; --i) {
      size_t position = IntToSize(i);
      current_indexes_[position] = start % LongToSize(shapes_.at(position));
      start = start / LongToSize(shapes_.at(position));
      if (start == 0) {
        break;
      }
    }
    return true;
  }
  std::vector<size_t> IndexIterator() {
    if (is_first_iterator_) {
      is_first_iterator_ = false;
      return current_indexes_;
    }
    size_t last_rank = current_indexes_.size() - 1;
    current_indexes_[last_rank]++;
    for (size_t i = last_rank; current_indexes_.at(i) >= LongToSize(shapes_.at(i)) && i > 0; --i) {
      current_indexes_[i] = 0;
      current_indexes_[i - 1] += 1;
    }
    is_first_iterator_ = false;
    return current_indexes_;
  }

 private:
  bool is_first_iterator_{true};
  size_t min_index{0};
  size_t max_index_{1};
  ShapeVector shapes_;
  std::vector<size_t> current_indexes_;
};
using MatrixInfoPtr = std::shared_ptr<MatrixInfo>;

std::pair<MatrixDiag::Alignment, MatrixDiag::Alignment> GetAlignments(const std::string &alignment);
int CalDiagOffset(int diag_index, int max_diag_len, int inner_rows, int inner_cols,
                  const std::pair<MatrixDiag::Alignment, MatrixDiag::Alignment> &alignment);
std::string GetCompilerCachePath();
bool CheckCache(const std::string &kernel_name);
KernelPackPtr SearchCache(const std::string &kernel_name, const std::string &processor);
KernelPackPtr InsertCache(const std::string &kernel_name, const std::string &processor);
TypeId DtypeToTypeId(const std::string &dtypes);
std::string Dtype2ShortType(const std::string &dtype);
size_t GetDtypeNbyte(const std::string &dtype);
bool GetShapeSize(const ShapeVector &shape, const TypePtr &type_ptr, int64_t *size_i);
bool ParseMetadata(const CNodePtr &kernel_node, const std::shared_ptr<const OpInfo> &op_info_ptr, Processor processor,
                   std::vector<std::shared_ptr<KernelBuildInfo>> *const kernel_info_list);
void SaveJsonInfo(const std::string &json_name, const std::string &info, const std::string &base_path);
std::string GetProcessor(const AnfNodePtr &anf_node);
Processor GetProcessor(const string &processor);
bool IsSameShape(const ShapeVector &shape_a, const ShapeVector &shape_b);
std::vector<std::pair<AnfNodePtr, size_t>> GetOutputIndex(const std::vector<AnfNodePtr> &node_list,
                                                          const std::vector<AnfNodePtr> &input_list,
                                                          const std::vector<AnfNodePtr> &output_list);
void GetValidKernelNodes(const FuncGraphPtr &func_graph, std::vector<AnfNodePtr> *node_list);
void GetValidKernelNodes(const FuncGraphPtr &func_graph, std::vector<AnfNodePtr> *node_list,
                         std::vector<AnfNodePtr> *input_list, std::vector<AnfNodePtr> *output_list);
void GetFuncGraphOutputNodes(const FuncGraphPtr &func_graph, std::vector<AnfNodePtr> *output_list);
void GetGraphRealOutput(const FuncGraphPtr &func_graph, std::vector<std::pair<AnfNodePtr, size_t>> *node_list);
bool IsWeightBoundary(const AnfNodePtr &node);
std::vector<int64_t> GetReduceAttrAxis(const CNodePtr &cnode);
std::string GetProcessorStr(const AnfNodePtr &anf_node);
Processor GetProcessorFromContext();
std::string GetStrProcessorFromContext();
float Scaling(size_t in_size, size_t out_size, bool align_corners);
inline float Scaler(const size_t x, const float scale, bool half_pixel_centers) {
  if (half_pixel_centers) {
    /**
     * function with a std::floor(), so instead of subtracting the 0.5 as we
     * do in HalfPixelScale, we leave it as is, as the std::floor does the
     * correct thing.
     * */
    return (static_cast<float>(x) + 0.5f) * scale;
  } else {
    /**
     * Older incorrect scaling method that causes all resizes to have a slight
     * translation leading to inconsistent results. For example, a flip then a
     * resize gives different results then a resize then a flip.
     * */
    return static_cast<float>(x) * scale;
  }
}
float ScaleGrid(const int x, const float scale);
FusionType GetFusionTypeByName(const std::string &name);
std::string GetFusionNameByType(const kernel::FusionType &type);
std::vector<bool> Dec2Bin(const int64_t &mask);
void FillEmptyDims(const CNodePtr &kernel_node, std::vector<int64_t> *begin, std::vector<int64_t> *end,
                   std::vector<int64_t> *stride, ShapeVector *input_shape);
void ParseStrideSliceMasks(const CNodePtr &kernel_node, std::vector<int64_t> *begin, std::vector<int64_t> *end,
                           std::vector<int64_t> *stride, const ShapeVector &input_shape);
struct CachedInterpolation {
  size_t lower;
  size_t upper;
  float lerp;
};

template <typename T>
struct AlignCornersFunc {
  T operator()(const T &new_x, const int &old_length, const int &new_length) const {
    return new_length != 1 ? new_x * (old_length - 1) / (new_length - 1) : 0;
  }
};

template <typename T>
struct AsymmetricFunc {
  T operator()(const T &new_x, const int &old_length, const int &new_length) const {
    return new_length != 0 ? new_x * old_length / new_length : 0;
  }
};

template <typename T>
struct HalfPixelFunc {
  T operator()(const T &new_x, const int &old_length, const int &new_length) const {
    return new_length > 1 ? (new_x + 0.5) * old_length / new_length - 0.5 : 0;
  }
};

void ComputeInterpolationWeights(const size_t out_size, const size_t in_size, const float scale,
                                 CachedInterpolation *interpolation);

template <typename T>
inline std::string Vector2Str(const std::vector<T> &inputs) {
  if (!inputs.empty()) {
    std::ostringstream oss;
    (void)std::copy(inputs.begin(), inputs.end() - 1, std::ostream_iterator<T>(oss, ", "));
    oss << inputs.back();
    return oss.str();
  }
  return "";
}

template <template <typename, typename, typename...> typename M, typename T>
inline std::string Map2Str(const M<std::string, T> value) {
  std::stringstream ss;
  ss << "(";
  for (auto it = value.begin(); it != value.end(); it++) {
    if (it == value.begin()) {
      ss << it->first;
    } else {
      ss << ", " << it->first;
    }
  }
  ss << ")";
  return ss.str();
}

template <typename T>
inline T ComputeLerp(T top_left, T top_right, T bottom_left, T bottom_right, T x_lerp, T y_lerp) {
  T top = top_left + (top_right - top_left) * x_lerp;
  T bottom = bottom_left + (bottom_right - bottom_left) * x_lerp;
  return top + (bottom - top) * y_lerp;
}

void CheckSliceValid(const std::vector<int64_t> &start, const std::vector<int64_t> &stop,
                     const std::vector<int64_t> &step, const std::vector<int64_t> &input_shape);
size_t CalOffset(const std::vector<int64_t> &start, const std::vector<int64_t> &stop,
                 const std::vector<int64_t> &dim_offset);
std::vector<int64_t> CalDimOffset(const std::vector<int64_t> &input_shape);
size_t GetCopySize(const std::vector<int64_t> &dim_offset, const std::vector<int64_t> &start,
                   const std::vector<int64_t> &stop);
size_t UnitSizeInBytes(const mindspore::TypeId &t);

class KernelAttr {
 public:
  using DataType = std::pair<TypeId, std::string>;
  KernelAttr() = default;
  ~KernelAttr() = default;

  KernelAttr &AddInputAttr(const TypeId &ms_type, const std::string &format = kOpFormat_DEFAULT);
  KernelAttr &AddOutputAttr(const TypeId &ms_type, const std::string &format = kOpFormat_DEFAULT);
  KernelAttr &AddAllSameAttr(const bool &all_same);
  KernelAttr &AddSkipCheckAttr(const bool &skip_check);
  KernelAttr &AddOutInRef(size_t output_index, size_t input_index);
  KernelAttr &AddAllOutInRef(const bool &all_out_in_ref);

  const DataType &GetInputAttr(const size_t index) const { return input_type_[index]; }
  const DataType &GetOutputAttr(const size_t index) const { return output_type_[index]; }
  const bool &GetAllSame() const { return all_same_; }
  const bool &GetSkipCheck() const { return skip_check_; }

  size_t GetInputSize() const { return input_type_.size(); }
  size_t GetOutputSize() const { return output_type_.size(); }
  const OutputInputRefMap &GetOutInRefMap() const { return out_in_ref_map_; }
  const bool &GetAllOutInRef() const { return all_out_in_ref_; }

  void SetInputAttr(const size_t index, const TypeId &ms_type, const std::string &format);
  void SetOutputAttr(const size_t index, const TypeId &ms_type, const std::string &format);
  void SetInputAttrList(const std::vector<DataType> &addr_list);

 private:
  std::vector<DataType> input_type_;
  std::vector<DataType> output_type_;
  bool all_same_{false};
  bool skip_check_{false};

  // The map between kernel's output and input ref relationship.
  OutputInputRefMap out_in_ref_map_;

  // The reference for all outputs and inputs of the same index.
  bool all_out_in_ref_{false};
};
std::ostream &operator<<(std::ostream &os, KernelAttr kernel_attr);

std::pair<bool, size_t> MatchKernelAttr(const KernelAttr &kernel_attr, const std::vector<KernelAttr> &kernel_attr_list);
KernelAttr GetKernelAttrFromBuildInfo(const KernelBuildInfoPtr &build_info);
KernelAttr GetKernelAttrFromNode(const AnfNodePtr &kernel_node);

struct KernelArgs {
  BaseOperatorPtr op;
  std::vector<KernelTensorPtr> inputs;
  std::vector<KernelTensorPtr> outputs;
  std::map<uint32_t, tensor::TensorPtr> depend_tensor_map;  // dynamic shape kernel may need this map
  // cppcheck-suppress unusedStructMember
  constexpr static char key[] = "KernelArgs";
};
KernelArgs AbstractArgsFromCNode(const CNodePtr &cnode);

KernelAttr GetKernelAttrFromTensors(const std::vector<KernelTensorPtr> &inputs,
                                    const std::vector<KernelTensorPtr> &outputs);

void SetCpuRefMapToKernelInfo(const CNodePtr &apply_kernel, const std::vector<KernelAttr> &apply_kernel_attrs);
Format GetFormatFromStrToEnum(const std::string &format_str);
std::string GetFormatFromEnumToStr(Format format);
void UpdateNodeShape(const CNodePtr &cnode);
// Synchronize the output and input reference map between two kernel attrs.
void SyncOutInRef(const KernelAttr &from_kernel_attr, KernelAttr *to_kernel_attr);
std::shared_ptr<KernelArgs> GetArgsFromCNode(const CNodePtr &cnode);
void SetArgsToCNode(const CNodePtr &cnode, const KernelArgs &args);
inline std::map<uint32_t, tensor::TensorPtr> GetKernelDepends(const CNodePtr &cnode) {
  auto args = GetArgsFromCNode(cnode);
  if (args) {
    return args->depend_tensor_map;
  }
  return std::map<uint32_t, tensor::TensorPtr>();
}

template <typename Derived>
class MatchKernelHelper {
 public:
  MatchKernelHelper() = default;
  virtual ~MatchKernelHelper() = default;

  using KernelRunFunc = std::function<bool(Derived *, const std::vector<AddressPtr> &, const std::vector<AddressPtr> &,
                                           const std::vector<AddressPtr> &)>;
  virtual const std::vector<std::pair<KernelAttr, KernelRunFunc>> &GetFuncList() const = 0;

 protected:
  std::vector<KernelAttr> OpSupport() const {
    auto &func_list = static_cast<const Derived *>(this)->GetFuncList();
    std::vector<KernelAttr> support_list;
    (void)std::transform(func_list.begin(), func_list.end(), std::back_inserter(support_list),
                         [](const std::pair<KernelAttr, KernelRunFunc> &pair) { return pair.first; });
    return support_list;
  }
  bool MatchKernelFunc(const BaseOperatorPtr &base_operator, const std::vector<KernelTensorPtr> &inputs,
                       const std::vector<KernelTensorPtr> &outputs) {
    auto kernel_name = base_operator->name();
    auto kernel_attr = GetKernelAttrFromTensors(inputs, outputs);
    auto &func_list = static_cast<Derived *>(this)->GetFuncList();
    auto [is_match, index] = MatchKernelAttr(kernel_attr, OpSupport());
    if (!is_match) {
      MS_LOG(ERROR) << "The kernel '" << kernel_name << "' does not support this kernel data type: " << kernel_attr;
      return false;
    }
    kernel_func_ = func_list[index].second;
    return true;
  }
  KernelRunFunc kernel_func_;
};

namespace broadcast_utils {
bool IsBroadcast(const std::vector<size_t> &lhs, const std::vector<size_t> &rhs);
bool AlignedBroadCastShape(size_t align_rank, std::vector<size_t> *broadcast, std::vector<size_t> *lhs,
                           std::vector<size_t> *rhs);
}  // namespace broadcast_utils

#define CHECK_KERNEL_INPUTS_NUM(actual_inputs_num, expect_inputs_num, kernel_name)                     \
  do {                                                                                                 \
    if ((actual_inputs_num) != (expect_inputs_num)) {                                                  \
      MS_LOG(EXCEPTION) << (kernel_name) << " requires " << (expect_inputs_num) << " inputs, but got " \
                        << (actual_inputs_num) << ".";                                                 \
    }                                                                                                  \
  } while (0)

#define CHECK_KERNEL_OUTPUTS_NUM(actual_outputs_num, expect_outputs_num, kernel_name)                       \
  do {                                                                                                      \
    if ((actual_outputs_num) != (expect_outputs_num)) {                                                     \
      MS_LOG(EXCEPTION) << (kernel_name) << " should have " << (expect_outputs_num) << " outputs, but got " \
                        << (actual_outputs_num) << ".";                                                     \
    }                                                                                                       \
  } while (0)

#define CHECK_KERNEL_WORKSPACE_SIZE(actual_size, expect_size, kernel_name)                                           \
  do {                                                                                                               \
    if ((actual_size) != (expect_size)) {                                                                            \
      MS_LOG(EXCEPTION) << (kernel_name) << " requires " << (expect_size) << " workspace, but got " << (actual_size) \
                        << ".";                                                                                      \
    }                                                                                                                \
  } while (0)
}  // namespace kernel
}  // namespace mindspore

#endif  // MINDSPORE_CCSRC_KERNEL_COMMON_UTILS_H_
