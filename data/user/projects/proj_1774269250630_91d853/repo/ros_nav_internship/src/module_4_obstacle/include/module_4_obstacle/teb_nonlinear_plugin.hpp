#pragma once
#include <rclcpp/rclcpp.hpp>
#include <cmath>
#include <string>

namespace module_4_obstacle {

/**
 * @brief TEB nonlinear kinematic constraint plugin.
 *
 * Provides velocity and acceleration constraints that depend on the current
 * steering angle (Ackermann-like model).  Intended to be loaded by a
 * nav2_regulated_pure_pursuit or TEB-compatible planner as a shared library.
 *
 * Constraints implemented
 * ──────────────────────
 * - maxLinearVelocity(steering_angle):
 *     v_max * cos(steering_angle / 2)
 *   Reduces allowed speed as steering angle increases.
 *
 * - maxAngularAcceleration():
 *     Returns alpha_ddot_max (configured parameter).
 *
 * - minTurningRadius():
 *     wheel_base / tan(max_steering_angle)
 */
class TebNonlinearPlugin {
public:
  /**
   * @brief Construct and load parameters from a ROS2 node.
   * @param node  Shared pointer to the owning node (for parameter access).
   */
  explicit TebNonlinearPlugin(rclcpp::Node::SharedPtr node);

  /**
   * @brief Maximum linear velocity permitted at the given steering angle.
   * @param steering_angle  Current steering angle [rad].
   * @return                v_max * cos(steering_angle / 2)  [m/s].
   */
  double maxLinearVelocity(double steering_angle) const;

  /**
   * @brief Maximum angular acceleration limit.
   * @return  alpha_ddot_max [rad/s²].
   */
  double maxAngularAcceleration() const;

  /**
   * @brief Minimum turning radius for the configured wheel base and max steer.
   * @return  wheel_base / tan(max_steering_angle)  [m].
   */
  double minTurningRadius() const;

  // ── Parameter accessors ──────────────────────────────────────────────────
  double getVMax()              const { return v_max_; }
  double getAlphaDdotMax()      const { return alpha_ddot_max_; }
  double getWheelBase()         const { return wheel_base_; }
  double getMaxSteeringAngle()  const { return max_steering_angle_; }
  double getMaxLinearAccel()    const { return max_linear_accel_; }

private:
  // ── Parameters ──────────────────────────────────────────────────────────
  double v_max_{1.5};               ///< Maximum linear speed [m/s]
  double alpha_ddot_max_{1.0};      ///< Maximum angular acceleration [rad/s²]
  double wheel_base_{0.5};          ///< Distance between axles [m]
  double max_steering_angle_{0.7};  ///< Maximum steering angle [rad] (~40°)
  double max_linear_accel_{0.5};    ///< Maximum linear acceleration [m/s²]

  rclcpp::Node::SharedPtr node_;    ///< Owning node (kept for dynamic reconfigure)
};

}  // namespace module_4_obstacle
