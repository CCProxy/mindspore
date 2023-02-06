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

#ifndef MINDSPORE_CCSRC_DISTRIBUTED_RPC_RDMA_CONSTANTS_H_
#define MINDSPORE_CCSRC_DISTRIBUTED_RPC_RDMA_CONSTANTS_H_

#include <urpc.h>
#include <string>

#include "utils/dlopen_macro.h"
#include "distributed/constants.h"

namespace mindspore {
namespace distributed {
namespace rpc {
// Parse url string with format ip:port.
inline bool ParseURL(const std::string &url, std::string *ip, uint16_t *port) {
  if (ip == nullptr || port == nullptr) {
    MS_LOG(ERROR) << "Output ip or port is nullptr";
    return false;
  }

  size_t index1 = url.find(URL_PROTOCOL_IP_SEPARATOR);
  if (index1 == std::string::npos) {
    index1 = 0;
  } else {
    index1 = index1 + sizeof(URL_PROTOCOL_IP_SEPARATOR) - 1;
  }

  size_t index2 = url.rfind(':');
  if (index2 == std::string::npos) {
    MS_LOG(ERROR) << "Couldn't find the character colon.";
    return false;
  }

  *ip = url.substr(index1, index2 - index1);
  if (ip->empty()) {
    MS_LOG(ERROR) << "Couldn't find ip in url: " << url.c_str();
    return false;
  }

  size_t idx = index2 + sizeof(URL_IP_PORT_SEPARATOR) - 1;
  if (idx >= url.size()) {
    MS_LOG(ERROR) << "The size of url is invalid";
    return false;
  }
  try {
    *port = static_cast<uint16_t>(std::stoul(url.substr(idx)));
  } catch (const std::system_error &e) {
    MS_LOG(ERROR) << "Couldn't find port in url: " << url.c_str();
    return false;
  }

  MS_LOG(INFO) << "Parse URL for " << url << ". IP: " << *ip << ". Port: " << *port;
  return true;
}

inline void *LoadURPC() {
  static void *urpc_handle = nullptr;
  if (urpc_handle == nullptr) {
    urpc_handle = dlopen("liburpc.so", RTLD_LAZY | RTLD_LOCAL);
    if (urpc_handle == nullptr) {
      auto err = GetDlErrorMsg();
      MS_LOG(EXCEPTION) << "dlopen liburpc.so failed. Error message: " << err;
    }
  }
  return urpc_handle;
}
inline const void *kURPCHandle = LoadURPC();

#define REG_URPC_METHOD(name, return_type, ...)                 \
  constexpr const char *k##name##Name = #name;                  \
  using name##FunObj = std::function<return_type(__VA_ARGS__)>; \
  using name##FunPtr = return_type (*)(__VA_ARGS__);            \
  const name##FunPtr name##_func = DlsymFuncObj(name, const_cast<void *>(kURPCHandle));

// The symbols of liburpc.so to be dynamically loaded.
REG_URPC_METHOD(urpc_init, int, struct urpc_config *)
REG_URPC_METHOD(urpc_uninit, void)
REG_URPC_METHOD(urpc_connect, urpc_session_t *, const char *, uint16_t, urma_jfs_t *)
REG_URPC_METHOD(urpc_close, void, urpc_session_t *)
REG_URPC_METHOD(urpc_register_memory, int, void *, int)
REG_URPC_METHOD(urpc_register_serdes, int, const char *, const urpc_serdes_t *, urpc_tx_cb_t, void *)
REG_URPC_METHOD(urpc_register_handler, int, urpc_handler_info_t *, uint32_t *)
REG_URPC_METHOD(urpc_register_raw_handler_explicit, int, urpc_raw_handler_t, void *, urpc_tx_cb_t, void *, uint32_t)
REG_URPC_METHOD(urpc_unregister_handler, void, const char *, uint32_t)
REG_URPC_METHOD(urpc_query_capability, int, struct urpc_cap *)
REG_URPC_METHOD(urpc_send_request, int, urpc_session_t *, struct urpc_send_wr *, struct urpc_send_option *)
REG_URPC_METHOD(urpc_call, int, urpc_session_t *, const char *, void *, void **, struct urpc_send_option *)
REG_URPC_METHOD(urpc_call_sgl, int, urpc_session_t *, const char *, void *, void **, struct urpc_send_option *)
REG_URPC_METHOD(urpc_get_default_allocator, struct urpc_buffer_allocator *)

constexpr int kURPCSuccess = 0;
constexpr uint32_t kInterProcessDataHandleID = 0;

constexpr char kDefaultIP[] = "1.1.8.203";
constexpr char kDefaultIfName[] = "hrn0_2";
constexpr uint16_t kDefaultPort = 10969;

constexpr uint32_t kServerWorkingThreadNum = 4;
constexpr uint32_t kClientPollingThreadNum = 4;

struct req_cb_arg {
  int *rsp_received;
  struct urpc_buffer_allocator *allocator;
};
}  // namespace rpc
}  // namespace distributed
}  // namespace mindspore
#endif  // MINDSPORE_CCSRC_DISTRIBUTED_RPC_RDMA_CONSTANTS_H_
