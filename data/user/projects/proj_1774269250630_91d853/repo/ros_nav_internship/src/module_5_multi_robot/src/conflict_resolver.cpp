/**
 * conflict_resolver.cpp
 *
 * Online conflict detector and resolver using Time-To-Collision (TTC).
 *
 * Detection model
 * ───────────────
 * TTC is computed from the relative position r = pos_a - pos_b and relative
 * velocity v = vel_a - vel_b using the linear approach model:
 *
 *   TTC = -(r · v) / (v · v)   when r · v < 0  (robots approaching)
 *
 * If TTC < ttc_threshold_ AND current separation < conflict_detection_distance_
 * a conflict is declared.
 *
 * Resolution (right-hand traffic)
 * ────────────────────────────────
 * The robot with the lower id yields by following a waypoint offset
 * perpendicularly to the right of its current heading.  The offset command is
 * published on /conflict_resolved_waypoint and a JSON event on /conflict_events.
 */

#include "module_5_multi_robot/conflict_resolver.hpp"

#include <cmath>
#include <sstream>
#include <string>

namespace module_5_multi_robot {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

ConflictResolver::ConflictResolver(const rclcpp::NodeOptions & options)
: Node("conflict_resolver", options)
{
  this->declare_parameter("num_robots",                  num_robots_);
  this->declare_parameter("conflict_detection_distance", conflict_detection_distance_);
  this->declare_parameter("ttc_threshold",               ttc_threshold_);
  this->declare_parameter("lateral_offset",              lateral_offset_);

  num_robots_                  = this->get_parameter("num_robots").as_int();
  conflict_detection_distance_ = this->get_parameter("conflict_detection_distance").as_double();
  ttc_threshold_               = this->get_parameter("ttc_threshold").as_double();
  lateral_offset_              = this->get_parameter("lateral_offset").as_double();

  // Per-robot odometry subscribers
  for (int i = 0; i < num_robots_; ++i) {
    std::string topic = "/robot_" + std::to_string(i) + "/odom";
    robot_subs_.push_back(
        this->create_subscription<nav_msgs::msg::Odometry>(
            topic, rclcpp::QoS(10),
            [this, i](const nav_msgs::msg::Odometry::SharedPtr msg) {
              robotStatesCallback(msg, i);
            }));
  }

  offset_cmd_pub_ = this->create_publisher<geometry_msgs::msg::PoseStamped>(
      "/conflict_resolved_waypoint", rclcpp::QoS(10));

  conflict_events_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/conflict_events", rclcpp::QoS(10));

  // 10 Hz conflict check timer
  check_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(100),
      std::bind(&ConflictResolver::checkConflicts, this));

  RCLCPP_INFO(this->get_logger(),
              "ConflictResolver ready: %d robots, dist=%.2f m, TTC_thresh=%.2f s",
              num_robots_, conflict_detection_distance_, ttc_threshold_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Public: computeTTC
// ─────────────────────────────────────────────────────────────────────────────

std::optional<double> ConflictResolver::computeTTC(
    const RobotState & a, const RobotState & b) const
{
  // Relative position
  double rx = a.x - b.x;
  double ry = a.y - b.y;

  // Relative velocity
  double rvx = a.vx - b.vx;
  double rvy = a.vy - b.vy;

  double rv_sq = rvx * rvx + rvy * rvy;
  if (rv_sq < 1e-9) return std::nullopt;  // robots not moving relative to each other

  double r_dot_rv = rx * rvx + ry * rvy;
  if (r_dot_rv >= 0.0) return std::nullopt;  // robots diverging

  double ttc = -r_dot_rv / rv_sq;
  return ttc;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public: computeLateralOffset
// ─────────────────────────────────────────────────────────────────────────────

geometry_msgs::msg::PoseStamped ConflictResolver::computeLateralOffset(
    const RobotState & robot, double offset_distance) const
{
  // Right-perpendicular to heading: rotate heading by -90 degrees
  // heading direction: (cos(yaw), sin(yaw))
  // right perpendicular: (sin(yaw), -cos(yaw))
  double right_x = std::sin(robot.yaw);
  double right_y = -std::cos(robot.yaw);

  geometry_msgs::msg::PoseStamped waypoint;
  waypoint.header.frame_id      = "map";
  waypoint.header.stamp         = robot.stamp;
  waypoint.pose.position.x      = robot.x + right_x * offset_distance;
  waypoint.pose.position.y      = robot.y + right_y * offset_distance;
  waypoint.pose.position.z      = 0.0;

  // Keep the same heading
  waypoint.pose.orientation.z   = std::sin(robot.yaw / 2.0);
  waypoint.pose.orientation.w   = std::cos(robot.yaw / 2.0);

  return waypoint;
}

// ─────────────────────────────────────────────────────────────────────────────
// Private: robotStatesCallback
// ─────────────────────────────────────────────────────────────────────────────

void ConflictResolver::robotStatesCallback(
    const nav_msgs::msg::Odometry::SharedPtr msg, int robot_id)
{
  RobotState & rs = robot_states_[robot_id];
  rs.id    = robot_id;
  rs.x     = msg->pose.pose.position.x;
  rs.y     = msg->pose.pose.position.y;
  rs.vx    = msg->twist.twist.linear.x;
  rs.vy    = msg->twist.twist.linear.y;
  rs.stamp = msg->header.stamp;

  // Extract yaw from quaternion
  const auto & q = msg->pose.pose.orientation;
  rs.yaw = std::atan2(2.0 * (q.w * q.z + q.x * q.y),
                       1.0 - 2.0 * (q.y * q.y + q.z * q.z));
}

// ─────────────────────────────────────────────────────────────────────────────
// Private: checkConflicts
// ─────────────────────────────────────────────────────────────────────────────

void ConflictResolver::checkConflicts()
{
  if (static_cast<int>(robot_states_.size()) < 2) return;

  // Check all pairs
  for (auto it_a = robot_states_.begin(); it_a != robot_states_.end(); ++it_a) {
    for (auto it_b = std::next(it_a); it_b != robot_states_.end(); ++it_b) {
      const RobotState & a = it_a->second;
      const RobotState & b = it_b->second;

      double dist = robotDistance(a, b);
      if (dist > conflict_detection_distance_) continue;

      auto ttc_opt = computeTTC(a, b);
      if (!ttc_opt) continue;

      double ttc = *ttc_opt;
      if (ttc < ttc_threshold_) {
        resolveConflict(a.id, b.id, ttc);
      }
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Private: resolveConflict
// ─────────────────────────────────────────────────────────────────────────────

void ConflictResolver::resolveConflict(int robot_a_id, int robot_b_id, double ttc)
{
  // Right-hand traffic: lower id yields (moves right)
  int yield_id = std::min(robot_a_id, robot_b_id);

  auto it = robot_states_.find(yield_id);
  if (it == robot_states_.end()) return;

  const RobotState & yielder = it->second;
  auto waypoint = computeLateralOffset(yielder, lateral_offset_);

  offset_cmd_pub_->publish(waypoint);

  // Publish conflict event
  std::string event_json = buildConflictJson(
      robot_a_id, robot_b_id, ttc,
      "right_lateral_offset_robot_" + std::to_string(yield_id));
  std_msgs::msg::String event_msg;
  event_msg.data = event_json;
  conflict_events_pub_->publish(event_msg);

  RCLCPP_WARN(this->get_logger(),
              "Conflict: robots %d & %d, TTC=%.2f s, robot %d yields right",
              robot_a_id, robot_b_id, ttc, yield_id);
}

// ─────────────────────────────────────────────────────────────────────────────
// Private: robotDistance
// ─────────────────────────────────────────────────────────────────────────────

double ConflictResolver::robotDistance(
    const RobotState & a, const RobotState & b) const
{
  double dx = a.x - b.x;
  double dy = a.y - b.y;
  return std::sqrt(dx * dx + dy * dy);
}

// ─────────────────────────────────────────────────────────────────────────────
// Private: buildConflictJson
// ─────────────────────────────────────────────────────────────────────────────

std::string ConflictResolver::buildConflictJson(
    int robot_a, int robot_b, double ttc,
    const std::string & action) const
{
  std::ostringstream oss;
  oss << "{"
      << "\"type\":\"conflict\","
      << "\"robot_a\":" << robot_a << ","
      << "\"robot_b\":" << robot_b << ","
      << "\"ttc\":"     << ttc     << ","
      << "\"action\":\"" << action << "\""
      << "}";
  return oss.str();
}

}  // namespace module_5_multi_robot

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<module_5_multi_robot::ConflictResolver>());
  rclcpp::shutdown();
  return 0;
}
