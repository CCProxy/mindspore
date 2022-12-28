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

#ifndef MINDSPORE_CCSRC_DISTRIBUTED_EMBEDDING_CACHE_DENSE_EMBEDDING_STORAGE_EMBEDDING_STORAGE_H_
#define MINDSPORE_CCSRC_DISTRIBUTED_EMBEDDING_CACHE_DENSE_EMBEDDING_STORAGE_EMBEDDING_STORAGE_H_

#include <vector>
#include "distributed/embedding_cache/embedding_storage/embedding_storage.h"

namespace mindspore {
namespace distributed {
namespace storage {
// A derived class for Dense implementation to manage lookup and update of a huge Embedding Table for Tensor type.
template <typename KeyType, typename ValueType, typename Allocator = Allocator<uint8_t>>
class BACKEND_EXPORT DenseEmbeddingStorage : public EmbeddingStorage<KeyType, ValueType, Allocator> {
 public:
  // The cache element type, a key-value pair, key is same the key of this dense embedding storage, value is the
  // index(line number in embedding table tensor) of the key.
  using CacheElement = typename EmbeddingStorage<KeyType, ValueType, Allocator>::CacheType::Element;

  DenseEmbeddingStorage(int32_t embedding_key, size_t embedding_dim, size_t capacity,
                        const Allocator &alloc = Allocator())
      : EmbeddingStorage<KeyType, ValueType>(embedding_key, embedding_dim, capacity, alloc) {}
  ~DenseEmbeddingStorage() override = default;

  // Initialize the EmbeddingStorage, such as recording the device address of the Embedding Table corresponding to the
  // DenseEmbeddingStorage.
  // Parameter[in] `device_address`: The device address of the Embedding Table tensor parameter
  // corresponding to the DenseEmbeddingStorage.
  void Initialize(const DeviceAddress *device_address) override;

  // Finalize the EmbeddingStorage, release allocated resource.
  void Finalize() override;

  // Batch embeddings lookup operation.
  // Query Embeddings in the host cache first, if the corresponding element cannot be found in the host cache, then read
  // the element from the SSD and insert host cache.
  // Access an element of the cache generally affects the location or order of the elements in the cache, depending
  // on different cache strategies.
  bool Get(const KeyType *keys, size_t key_num, ValueType *values) override;

  // Batch embeddings update/insert operation.
  // Update/Insert Embeddings in the host cache first, if the host cache has insufficient space, the expired elements
  // will automatically be evicted the to the SSD.
  // Update or Insert an element of the cache generally affects the location or order of the elements in the cache,
  // depending on different cache strategies.
  bool Put(const KeyType *keys, size_t key_num, const ValueType *values) override;

 private:
  // The base pointer to embedding table parameter, all embeddings in host cache is recorded in embedding_param_ptr_.
  ValueType *embedding_param_ptr_{nullptr};

  // Record all empty slot(idle slot or index) in embedding table tensor.
  std::vector<int> empty_slots_;
};
}  // namespace storage
}  // namespace distributed
}  // namespace mindspore
#endif  // MINDSPORE_CCSRC_DISTRIBUTED_EMBEDDING_CACHE_DENSE_EMBEDDING_STORAGE_EMBEDDING_STORAGE_H_