#include "module_2_planning/velocity_clipper.hpp"

#include <algorithm>
#include <cmath>

namespace module_2_planning {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
VelocityClipper::VelocityClipper(const rclcpp::NodeOptions& options)
: rclcpp::Node("velocity_clipper", options)
{
  declare_parameter("v_max_linear",   rclcpp::ParameterValue(1.0));
  declare_parameter("v_max_angular",  rclcpp::ParameterValue(1.0));
  declare_parameter("a_max_linear",   rclcpp::ParameterValue(0.5));
  declare_parameter("a_max_angular",  rclcpp::ParameterValue(1.0));

  get_parameter("v_max_linear",  v_max_linear_);
  get_parameter("v_max_angular", v_max_angular_);
  get_parameter("a_max_linear",  a_max_linear_);
  get_parameter("a_max_angular", a_max_angular_);

  raw_sub_ = create_subscription<geometry_msgs::msg::Twist>(
    "/cmd_vel_raw", rclcpp::QoS(10),
    std::bind(&VelocityClipper::cmdVelRawCallback, this, std::placeholders::_1));

  clipped_pub_ = create_publisher<geometry_msgs::msg::Twist>(
    "/cmd_vel", rclcpp::QoS(10));

  prev_time_ = now();

  RCLCPP_INFO(get_logger(),
    "VelocityClipper ready: v_max=%.2f omega_max=%.2f a_lin=%.2f a_ang=%.2f",
    v_max_linear_, v_max_angular_, a_max_linear_, a_max_angular_);
}

// ─────────────────────────────────────────────────────────────────────────────
// clip — core velocity/acceleration limiting logic
// ─────────────────────────────────────────────────────────────────────────────
geometry_msgs::msg::Twist VelocityClipper::clip(
  const geometry_msgs::msg::Twist& raw,
  const geometry_msgs::msg::Twist& prev,
  double dt) const
{
  geometry_msgs::msg::Twist out;

  // ── Linear velocity limit ──────────────────────────────────────────────────
  double vx = raw.linear.x;
  double vy = raw.linear.y;

  // Clamp each axis independently
  vx = std::clamp(vx, -v_max_linear_, v_max_linear_);
  vy = std::clamp(vy, -v_max_linear_, v_max_linear_);

  // ── Angular velocity limit ─────────────────────────────────────────────────
  double wz = std::clamp(raw.angular.z, -v_max_angular_, v_max_angular_);

  // ── Acceleration limits ───────────────────────────────────────────────────
  if (dt > 1e-6) {
    // Linear x
    double max_delta_vx = a_max_linear_ * dt;
    double delta_vx = vx - prev.linear.x;
    if (std::fabs(delta_vx) > max_delta_vx) {
      vx = prev.linear.x + std::copysign(max_delta_vx, delta_vx);
    }

    // Linear y
    double max_delta_vy = a_max_linear_ * dt;
    double delta_vy = vy - prev.linear.y;
    if (std::fabs(delta_vy) > max_delta_vy) {
      vy = prev.linear.y + std::copysign(max_delta_vy, delta_vy);
    }

    // Angular z
    double max_delta_wz = a_max_angular_ * dt;
    double delta_wz = wz - prev.angular.z;
    if (std::fabs(delta_wz) > max_delta_wz) {
      wz = prev.angular.z + std::copysign(max_delta_wz, delta_wz);
    }
  }

  // ── Final clamp (in case accel-limited value went slightly over) ───────────
  out.linear.x  = std::clamp(vx, -v_max_linear_,  v_max_linear_);
  out.linear.y  = std::clamp(vy, -v_max_linear_,  v_max_linear_);
  out.linear.z  = 0.0;
  out.angular.x = 0.0;
  out.angular.y = 0.0;
  out.angular.z = std::clamp(wz, -v_max_angular_, v_max_angular_);

  return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// cmdVelRawCallback
// ─────────────────────────────────────────────────────────────────────────────
void VelocityClipper::cmdVelRawCallback(
  const geometry_msgs::msg::Twist::SharedPtr msg)
{
  rclcpp::Time cur_time = now();
  double dt = (cur_time - prev_time_).seconds();

  // Guard against large dt (e.g. after pause or first message)
  if (dt > 1.0 || dt < 0.0) dt = 0.0;

  geometry_msgs::msg::Twist prev_twist{};
  if (prev_cmd_.has_value()) {
    prev_twist = prev_cmd_.value();
  }

  auto clipped = clip(*msg, prev_twist, dt);

  clipped_pub_->publish(clipped);

  prev_cmd_  = clipped;
  prev_time_ = cur_time;
}

}  // namespace module_2_planning

// ── main ────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<module_2_planning::VelocityClipper>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
