/**
 * cross_robot_tf.cpp
 *
 * Broadcasts the full TF chain for a configurable fleet of N robots.
 *
 * Transform chain per robot i
 * ───────────────────────────
 *   map  →  robot_i/odom  →  robot_i/base_link
 *
 * The map→odom transform is published as the identity (assuming each robot's
 * nav stack handles its own localisation and publishes odom→base_link).  When
 * a fresh odometry message arrives the node caches it and re-publishes on the
 * next timer tick.
 *
 * Static transforms (published once at startup)
 * ──────────────────────────────────────────────
 *   robot_i/base_link → robot_i/base_footprint  (z offset 0)
 *   robot_i/base_link → robot_i/laser            (x offset 0.15 m, z 0.2 m)
 */

#include "module_5_multi_robot/cross_robot_tf.hpp"

#include <tf2/LinearMath/Quaternion.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.hpp>
#include <functional>

namespace module_5_multi_robot {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

CrossRobotTF::CrossRobotTF(const rclcpp::NodeOptions & options)
: Node("cross_robot_tf", options)
{
  this->declare_parameter("num_robots",      num_robots_);
  this->declare_parameter("robot_prefix",    robot_prefix_);
  this->declare_parameter("map_frame",       map_frame_);
  this->declare_parameter("publish_rate_hz", publish_rate_hz_);

  num_robots_      = this->get_parameter("num_robots").as_int();
  robot_prefix_    = this->get_parameter("robot_prefix").as_string();
  map_frame_       = this->get_parameter("map_frame").as_string();
  publish_rate_hz_ = this->get_parameter("publish_rate_hz").as_double();

  // Initialise per-robot state
  for (int i = 0; i < num_robots_; ++i) {
    odom_received_[i] = false;
  }

  // TF broadcasters
  dynamic_broadcaster_ = std::make_unique<tf2_ros::TransformBroadcaster>(this);
  static_broadcaster_  = std::make_unique<tf2_ros::StaticTransformBroadcaster>(this);

  setupSubscriptions();
  publishStaticTransforms();

  // Periodic dynamic broadcast
  auto period_ms = std::chrono::milliseconds(
      static_cast<int>(1000.0 / publish_rate_hz_));
  broadcast_timer_ = this->create_wall_timer(
      period_ms,
      std::bind(&CrossRobotTF::broadcastTimerCallback, this));

  RCLCPP_INFO(this->get_logger(),
              "CrossRobotTF: %d robots, prefix='%s', map='%s', rate=%.1f Hz",
              num_robots_, robot_prefix_.c_str(), map_frame_.c_str(),
              publish_rate_hz_);
}

// ─────────────────────────────────────────────────────────────────────────────
// setupSubscriptions
// ─────────────────────────────────────────────────────────────────────────────

void CrossRobotTF::setupSubscriptions()
{
  for (int i = 0; i < num_robots_; ++i) {
    std::string topic = "/" + robot_prefix_ + std::to_string(i) + "/odom";
    odom_subs_.push_back(
        this->create_subscription<nav_msgs::msg::Odometry>(
            topic, rclcpp::QoS(10),
            [this, i](const nav_msgs::msg::Odometry::SharedPtr msg) {
              odomCallback(msg, i);
            }));
    RCLCPP_DEBUG(this->get_logger(), "Subscribed to %s", topic.c_str());
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// publishStaticTransforms
// ─────────────────────────────────────────────────────────────────────────────

void CrossRobotTF::publishStaticTransforms()
{
  std::vector<geometry_msgs::msg::TransformStamped> static_tfs;

  for (int i = 0; i < num_robots_; ++i) {
    std::string prefix = robot_prefix_ + std::to_string(i);

    // base_link → base_footprint
    {
      geometry_msgs::msg::TransformStamped tf;
      tf.header.stamp            = this->now();
      tf.header.frame_id         = prefix + "/base_link";
      tf.child_frame_id          = prefix + "/base_footprint";
      tf.transform.translation.x = 0.0;
      tf.transform.translation.y = 0.0;
      tf.transform.translation.z = 0.0;
      tf.transform.rotation.w    = 1.0;
      static_tfs.push_back(tf);
    }

    // base_link → laser (front-mounted lidar)
    {
      geometry_msgs::msg::TransformStamped tf;
      tf.header.stamp            = this->now();
      tf.header.frame_id         = prefix + "/base_link";
      tf.child_frame_id          = prefix + "/laser";
      tf.transform.translation.x = 0.15;
      tf.transform.translation.y = 0.0;
      tf.transform.translation.z = 0.20;
      tf.transform.rotation.w    = 1.0;
      static_tfs.push_back(tf);
    }
  }

  static_broadcaster_->sendTransform(static_tfs);
  RCLCPP_INFO(this->get_logger(),
              "Published %zu static transforms", static_tfs.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// odomCallback
// ─────────────────────────────────────────────────────────────────────────────

void CrossRobotTF::odomCallback(
    const nav_msgs::msg::Odometry::SharedPtr msg, int robot_id)
{
  latest_odom_[robot_id]    = *msg;
  odom_received_[robot_id]  = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// broadcastTimerCallback
// ─────────────────────────────────────────────────────────────────────────────

void CrossRobotTF::broadcastTimerCallback()
{
  for (int i = 0; i < num_robots_; ++i) {
    broadcastRobotTransforms(i);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// broadcastRobotTransforms
// ─────────────────────────────────────────────────────────────────────────────

void CrossRobotTF::broadcastRobotTransforms(int robot_id)
{
  std::string prefix    = robot_prefix_ + std::to_string(robot_id);
  rclcpp::Time now      = this->now();
  std::vector<geometry_msgs::msg::TransformStamped> tfs;

  // ── map → robot_i/odom ──────────────────────────────────────────────────────
  // We publish identity here; actual localisation drift is handled by nav2.
  {
    geometry_msgs::msg::TransformStamped tf;
    tf.header.stamp            = now;
    tf.header.frame_id         = map_frame_;
    tf.child_frame_id          = prefix + "/odom";
    tf.transform.translation.x = 0.0;
    tf.transform.translation.y = 0.0;
    tf.transform.translation.z = 0.0;
    tf.transform.rotation.w    = 1.0;
    tfs.push_back(tf);
  }

  // ── robot_i/odom → robot_i/base_link ────────────────────────────────────────
  if (odom_received_.at(robot_id)) {
    const auto & odom = latest_odom_.at(robot_id);

    geometry_msgs::msg::TransformStamped tf;
    tf.header.stamp            = odom.header.stamp;
    tf.header.frame_id         = prefix + "/odom";
    tf.child_frame_id          = prefix + "/base_link";
    tf.transform.translation.x = odom.pose.pose.position.x;
    tf.transform.translation.y = odom.pose.pose.position.y;
    tf.transform.translation.z = odom.pose.pose.position.z;
    tf.transform.rotation      = odom.pose.pose.orientation;
    tfs.push_back(tf);
  } else {
    // Publish identity until first odom is received
    geometry_msgs::msg::TransformStamped tf;
    tf.header.stamp            = now;
    tf.header.frame_id         = prefix + "/odom";
    tf.child_frame_id          = prefix + "/base_link";
    tf.transform.rotation.w    = 1.0;
    tfs.push_back(tf);
  }

  dynamic_broadcaster_->sendTransform(tfs);
}

}  // namespace module_5_multi_robot

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<module_5_multi_robot::CrossRobotTF>());
  rclcpp::shutdown();
  return 0;
}
