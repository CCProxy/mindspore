/**
 * Copyright 2021 Huawei Technologies Co., Ltd
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

#ifndef MINDSPORE_CCSRC_DISTRIBUTED_CLUSTER_CLUSTER_CONTEXT_H_
#define MINDSPORE_CCSRC_DISTRIBUTED_CLUSTER_CLUSTER_CONTEXT_H_

#include <map>
#include <set>
#include <mutex>
#include <string>
#include <memory>
#include <atomic>
#include "distributed/constants.h"
#include "utils/log_adapter.h"
#include "utils/ms_utils.h"

#include "ps/core/cluster_config.h"
#include "distributed/cluster/actor_route_table_proxy.h"
#include "distributed/cluster/topology/node_base.h"
#include "include/backend/visible.h"

namespace mindspore {
namespace distributed {
namespace cluster {
// The environment variable name represents the node id of a certain process(compute graph node).
constexpr char kNodeId[] = "MS_NODE_ID";

// The detailed reason of failing to run 'mindspore.communication.init()' with ClusterContext.
constexpr char kDetailedFailureReason[] =
  "Maybe you are trying to call 'mindspore.communication.init()' without using 'mpirun', which will make MindSpore "
  "load several environment variables and check their validation. Please use 'mpirun' to launch this process to fix "
  "this issue, or refer to this link if you want to run distributed training without using 'mpirun': "
  "https://www.mindspore.cn/tutorials/experts/zh-CN/r1.9/parallel/train_gpu.html.";

// Node role based cluster built by MindSpore communication framework.
class BACKEND_EXPORT ClusterContext {
 public:
  ~ClusterContext();
  DISABLE_COPY_AND_ASSIGN(ClusterContext)
  static std::shared_ptr<ClusterContext> instance();

  // Initialize the cluster configuration and build network.
  bool Initialize();

  // Finalize the cluster and process exits. If timeout is set to UINT32_MAX, this method will block without timeout.
  bool Finalize(uint32_t timeout = kDefaultFinishTimeout);

  // Return whether this node is the scheduler node.
  // In a cluster, the scheduler node is special because it's responsible for building network.
  bool IsScheduler();

  // Return node object of this process.
  const std::shared_ptr<topology::NodeBase> &node() const;

  // Return the shadow node.
  const std::shared_ptr<topology::NodeBase> &node_base() const;

  // Return node role in this cluster.
  const std::string &node_role() const;

  // Returns total number of the specified node role. This is used as the group size of this node role.
  uint32_t node_num(const std::string &node_role);

  // Returns the total number of various role nodes.
  uint32_t node_num() const;

  // Return cluster is initialized.
  bool initialized() const;

  // Return actor route proxy for AbstractNode.
  const ActorRouteTableProxyPtr &actor_route_table_proxy() const;

 private:
  ClusterContext();

  // This initializing cluster configurations. They can be exported by environment variables, set by python API or
  // configuration file.
  void InitClusterConfig();

  // Build the cluster with other processes. This method will not return until the networking is done.
  bool BuildCluster();

  // Load the cluster configuration like worker number, server number and etc.
  void InitNodeRole();
  void InitSchedulerIp();
  void InitSchedulerPort();

  // Use inline static instance pointer in case there are multiple static variables.
  inline static std::shared_ptr<ClusterContext> cluster_instance_{nullptr};

  // The flag that whether this cluster context instance is already initialized.
  std::atomic_bool inited_;

  // The flag that whether this cluster context instance is already finalized.
  std::atomic_bool finalized_;

  // The mutex about exiting status of this node.
  std::mutex finish_mutex_;

  // Node role to role number map.
  std::map<std::string, uint32_t> node_num_each_role_;

  // Scheduler information.
  std::string scheduler_host_;
  uint16_t scheduler_port_;

  // The compute graph node or meta server node according to the configuration of this process.
  std::shared_ptr<topology::NodeBase> node_base_;

  // The role of this process in the cluster.
  std::string node_role_;

  // The configuration of this cluster.
  std::unique_ptr<ps::core::ClusterConfig> cluster_config_;

  // The actor route table proxy. It only created in abstract nodes because scheduler does not use proxy.
  ActorRouteTableProxyPtr actor_route_table_proxy_;
};
}  // namespace cluster
}  // namespace distributed
}  // namespace mindspore
#endif  // MINDSPORE_CCSRC_DISTRIBUTED_CLUSTER_CLUSTER_CONTEXT_H_
