#pragma once
#include <rclcpp/rclcpp.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <geometry_msgs/msg/transform_stamped.hpp>
#include <tf2_ros/transform_broadcaster.h>
#include <tf2_ros/static_transform_broadcaster.h>
#include <vector>
#include <map>
#include <string>

namespace module_5_multi_robot {

/**
 * @class CrossRobotTF
 * @brief Publishes the full TF chain for a fleet of N robots.
 *
 * For each robot i the following transforms are maintained:
 *   map -> robot_i/odom          (dynamic, updated from odom message)
 *   robot_i/odom -> robot_i/base_link  (dynamic, from odometry twist integration)
 *
 * Static transforms published once at startup:
 *   robot_i/base_link -> robot_i/base_footprint
 *   robot_i/base_link -> robot_i/laser
 *
 * Subscribes to:
 *   /robot_N/odom  (nav_msgs/Odometry)
 *
 * Parameters:
 *   num_robots      (int,    default 3)    — how many robots to handle
 *   robot_prefix    (string, default "robot_")
 *   map_frame       (string, default "map")
 *   publish_rate_hz (double, default 50.0)
 */
class CrossRobotTF : public rclcpp::Node {
public:
  explicit CrossRobotTF(const rclcpp::NodeOptions & options = rclcpp::NodeOptions{});

private:
  /// Subscribe to /robot_N/odom for each robot and store latest odometry.
  void setupSubscriptions();

  /// Publish static base_link -> base_footprint and base_link -> laser for each robot.
  void publishStaticTransforms();

  /// Timer callback: broadcast dynamic transforms for all robots.
  void broadcastTimerCallback();

  /// Build and send the map->robot_i/odom and odom->robot_i/base_link transforms.
  void broadcastRobotTransforms(int robot_id);

  /// Odometry callback: cache latest odom for robot `robot_id`.
  void odomCallback(const nav_msgs::msg::Odometry::SharedPtr msg, int robot_id);

  // ── TF infrastructure ────────────────────────────────────────────────────────

  std::unique_ptr<tf2_ros::TransformBroadcaster>       dynamic_broadcaster_;
  std::unique_ptr<tf2_ros::StaticTransformBroadcaster> static_broadcaster_;

  // ── State ────────────────────────────────────────────────────────────────────

  std::map<int, nav_msgs::msg::Odometry>                             latest_odom_;
  std::map<int, bool>                                                odom_received_;
  std::vector<rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr> odom_subs_;

  rclcpp::TimerBase::SharedPtr broadcast_timer_;

  // ── Parameters ───────────────────────────────────────────────────────────────

  int         num_robots_{3};
  std::string robot_prefix_{"robot_"};
  std::string map_frame_{"map"};
  double      publish_rate_hz_{50.0};
};

}  // namespace module_5_multi_robot
