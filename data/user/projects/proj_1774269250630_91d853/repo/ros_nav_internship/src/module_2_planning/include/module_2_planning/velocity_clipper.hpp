#pragma once

#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/twist.hpp>

#include <optional>

namespace module_2_planning {

// ──────────────────────────────────────────────────────────────────────────────
// VelocityClipper
//
//  Subscribes to /cmd_vel_raw (geometry_msgs/Twist) and enforces:
//    • Linear velocity limits   [ -v_max, +v_max ]
//    • Angular velocity limits  [ -omega_max, +omega_max ]
//    • Linear acceleration cap  |Δv|  <= a_max_linear  * dt
//    • Angular acceleration cap |Δω|  <= a_max_angular * dt
//
//  The previous command is stored to compute per-cycle acceleration.
//  The clipped command is published on /cmd_vel.
// ──────────────────────────────────────────────────────────────────────────────
class VelocityClipper : public rclcpp::Node {
public:
  explicit VelocityClipper(
    const rclcpp::NodeOptions& options = rclcpp::NodeOptions());

  ~VelocityClipper() override = default;

  /// Apply velocity and acceleration limits.
  /// @param raw      Desired twist (may exceed limits).
  /// @param prev     Previous published twist (used for accel limiting).
  /// @param dt       Time since previous command (seconds).
  /// @return         Clipped twist that respects all limits.
  geometry_msgs::msg::Twist clip(
    const geometry_msgs::msg::Twist& raw,
    const geometry_msgs::msg::Twist& prev,
    double dt) const;

private:
  void cmdVelRawCallback(const geometry_msgs::msg::Twist::SharedPtr msg);

  // ── Publishers / Subscribers ─────────────────────────────────────────────────
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr raw_sub_;
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr    clipped_pub_;

  // ── State ─────────────────────────────────────────────────────────────────────
  std::optional<geometry_msgs::msg::Twist> prev_cmd_;
  rclcpp::Time                             prev_time_;

  // ── Parameters ────────────────────────────────────────────────────────────────
  double v_max_linear_{1.0};      ///< maximum linear speed  (m/s)
  double v_max_angular_{1.0};     ///< maximum angular speed (rad/s)
  double a_max_linear_{0.5};      ///< maximum linear  acceleration (m/s²)
  double a_max_angular_{1.0};     ///< maximum angular acceleration (rad/s²)
};

}  // namespace module_2_planning
