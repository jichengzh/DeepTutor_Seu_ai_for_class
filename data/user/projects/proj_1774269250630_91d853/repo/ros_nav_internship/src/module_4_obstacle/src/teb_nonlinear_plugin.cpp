#include "module_4_obstacle/teb_nonlinear_plugin.hpp"
#include <stdexcept>

namespace module_4_obstacle {

// ─────────────────────────────────────────────────────────────────────────────
TebNonlinearPlugin::TebNonlinearPlugin(rclcpp::Node::SharedPtr node)
: node_(node)
{
  if (!node_) {
    throw std::invalid_argument("TebNonlinearPlugin: node pointer is null");
  }

  // ── Declare parameters (with defaults already set in the header) ──────────
  if (!node_->has_parameter("teb.v_max")) {
    node_->declare_parameter("teb.v_max",              v_max_);
  }
  if (!node_->has_parameter("teb.alpha_ddot_max")) {
    node_->declare_parameter("teb.alpha_ddot_max",     alpha_ddot_max_);
  }
  if (!node_->has_parameter("teb.wheel_base")) {
    node_->declare_parameter("teb.wheel_base",         wheel_base_);
  }
  if (!node_->has_parameter("teb.max_steering_angle")) {
    node_->declare_parameter("teb.max_steering_angle", max_steering_angle_);
  }
  if (!node_->has_parameter("teb.max_linear_accel")) {
    node_->declare_parameter("teb.max_linear_accel",   max_linear_accel_);
  }

  // ── Load parameter values ─────────────────────────────────────────────────
  node_->get_parameter("teb.v_max",              v_max_);
  node_->get_parameter("teb.alpha_ddot_max",     alpha_ddot_max_);
  node_->get_parameter("teb.wheel_base",         wheel_base_);
  node_->get_parameter("teb.max_steering_angle", max_steering_angle_);
  node_->get_parameter("teb.max_linear_accel",   max_linear_accel_);

  // ── Sanity checks ─────────────────────────────────────────────────────────
  if (v_max_ <= 0.0) {
    RCLCPP_WARN(node_->get_logger(),
      "TebNonlinearPlugin: v_max = %.3f is non-positive, clamping to 0.1", v_max_);
    v_max_ = 0.1;
  }
  if (wheel_base_ <= 0.0) {
    RCLCPP_WARN(node_->get_logger(),
      "TebNonlinearPlugin: wheel_base = %.3f is non-positive, clamping to 0.1",
      wheel_base_);
    wheel_base_ = 0.1;
  }
  if (max_steering_angle_ <= 0.0) {
    RCLCPP_WARN(node_->get_logger(),
      "TebNonlinearPlugin: max_steering_angle = %.3f is non-positive, clamping to 0.1",
      max_steering_angle_);
    max_steering_angle_ = 0.1;
  }

  RCLCPP_INFO(node_->get_logger(),
    "TebNonlinearPlugin loaded: v_max=%.2f  alpha_ddot_max=%.2f  "
    "wheel_base=%.2f  max_steer=%.3f  max_lin_accel=%.2f",
    v_max_, alpha_ddot_max_, wheel_base_, max_steering_angle_, max_linear_accel_);
}

// ─────────────────────────────────────────────────────────────────────────────
double TebNonlinearPlugin::maxLinearVelocity(double steering_angle) const
{
  // Nonlinear constraint: reduce v_max as steering increases.
  // Formula: v_max * cos(steering_angle / 2)
  // This models the effective forward speed of the vehicle when turning.
  const double half_steer = steering_angle / 2.0;
  const double factor = std::cos(half_steer);

  // Clamp to [0, v_max_] – cos can be slightly > 1 due to floating-point
  return std::max(0.0, std::min(v_max_, v_max_ * factor));
}

// ─────────────────────────────────────────────────────────────────────────────
double TebNonlinearPlugin::maxAngularAcceleration() const
{
  return alpha_ddot_max_;
}

// ─────────────────────────────────────────────────────────────────────────────
double TebNonlinearPlugin::minTurningRadius() const
{
  // R_min = wheel_base / tan(max_steering_angle)
  const double tan_steer = std::tan(max_steering_angle_);
  if (std::abs(tan_steer) < 1e-9) {
    return std::numeric_limits<double>::infinity();
  }
  return wheel_base_ / tan_steer;
}

}  // namespace module_4_obstacle
