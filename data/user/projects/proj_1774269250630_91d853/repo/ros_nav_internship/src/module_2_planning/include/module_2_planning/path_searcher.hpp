#pragma once

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <nav_msgs/msg/path.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <nav2_costmap_2d/costmap_2d_ros.hpp>
#include <tf2_ros/buffer.h>
#include <tf2_ros/transform_listener.h>

#include "module_2_planning/hybrid_astar_planner.hpp"

#include <memory>
#include <string>

namespace module_2_planning {

// ──────────────────────────────────────────────────────────────────────────────
// PathSearcher
//
//  Standalone ROS 2 lifecycle component that:
//    • Subscribes to /start_pose  (geometry_msgs/PoseStamped)
//    • Subscribes to /goal_pose   (geometry_msgs/PoseStamped — or /goal from
//                                  Nav2 which sends the same type)
//    • Invokes HybridAStarPlanner::createPlan()
//    • Publishes result on /raw_path (nav_msgs/Path)
// ──────────────────────────────────────────────────────────────────────────────
class PathSearcher : public rclcpp_lifecycle::LifecycleNode {
public:
  explicit PathSearcher(
    const rclcpp::NodeOptions& options = rclcpp::NodeOptions());

  ~PathSearcher() override = default;

  // ── Lifecycle callbacks ─────────────────────────────────────────────────────
  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_configure(const rclcpp_lifecycle::State& state) override;

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_activate(const rclcpp_lifecycle::State& state) override;

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_deactivate(const rclcpp_lifecycle::State& state) override;

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_cleanup(const rclcpp_lifecycle::State& state) override;

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_shutdown(const rclcpp_lifecycle::State& state) override;

private:
  /// Attempt to compute and publish a path when both start and goal are known.
  void tryPlan();

  void startCallback(const geometry_msgs::msg::PoseStamped::SharedPtr msg);
  void goalCallback (const geometry_msgs::msg::PoseStamped::SharedPtr msg);

  // ── Publishers / Subscribers ────────────────────────────────────────────────
  rclcpp::Subscription<geometry_msgs::msg::PoseStamped>::SharedPtr start_sub_;
  rclcpp::Subscription<geometry_msgs::msg::PoseStamped>::SharedPtr goal_sub_;
  rclcpp_lifecycle::LifecyclePublisher<nav_msgs::msg::Path>::SharedPtr path_pub_;

  // ── Internal state ───────────────────────────────────────────────────────────
  std::shared_ptr<HybridAStarPlanner>            planner_;
  std::shared_ptr<nav2_costmap_2d::Costmap2DROS> costmap_ros_;
  std::shared_ptr<tf2_ros::Buffer>               tf_buffer_;
  std::shared_ptr<tf2_ros::TransformListener>    tf_listener_;

  geometry_msgs::msg::PoseStamped::SharedPtr current_start_;
  geometry_msgs::msg::PoseStamped::SharedPtr current_goal_;

  std::string planner_name_{"HybridAStarPlanner"};
  std::string global_frame_{"map"};
};

}  // namespace module_2_planning
