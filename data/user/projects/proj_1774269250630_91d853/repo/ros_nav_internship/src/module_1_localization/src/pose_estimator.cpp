/**
 * @file pose_estimator.cpp
 * @brief EKF-based pose estimator combining AMCL output with IMU integration.
 *
 * Implements a simplified 3-DOF (x, y, yaw) Extended Kalman Filter that:
 *  - Predicts using IMU angular velocity and odometry linear velocity.
 *  - Updates (corrects) using the AMCL pose estimate when available.
 *
 * State vector:  X = [x, y, yaw]^T
 * Input vector:  u = [v (linear), omega (angular)]^T
 * Observation:   z = [x_amcl, y_amcl, yaw_amcl]^T
 *
 * This file compiles as part of the localization_lifecycle executable.
 */

#include <array>
#include <cmath>
#include <string>

#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/pose_with_covariance_stamped.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <sensor_msgs/msg/imu.hpp>

namespace module_1_localization {
namespace pose_estimator {

// ============================================================
// EKF state  (3-DOF: x, y, yaw)
// ============================================================

struct EkfState {
  double x   {0.0};   ///< World-frame x position (m).
  double y   {0.0};   ///< World-frame y position (m).
  double yaw {0.0};   ///< World-frame yaw (rad).

  /// Covariance matrix P (3x3, row-major).
  std::array<double, 9> P {{
    0.1, 0.0, 0.0,
    0.0, 0.1, 0.0,
    0.0, 0.0, 0.05
  }};
};

// ============================================================
// EkfParameters
// ============================================================

struct EkfParameters {
  double q_x   {0.01};   ///< Process noise – x (m^2)
  double q_y   {0.01};   ///< Process noise – y (m^2)
  double q_yaw {0.005};  ///< Process noise – yaw (rad^2)
  double r_x   {0.05};   ///< Observation noise – x (m^2)
  double r_y   {0.05};   ///< Observation noise – y (m^2)
  double r_yaw {0.02};   ///< Observation noise – yaw (rad^2)
};

// ============================================================
// Helper: normalise angle to [-pi, pi]
// ============================================================

static double normaliseAngle(double a)
{
  while (a >  M_PI) { a -= 2.0 * M_PI; }
  while (a < -M_PI) { a += 2.0 * M_PI; }
  return a;
}

// ============================================================
// ekfPredict
// ============================================================

/**
 * @brief EKF prediction step.
 *
 * Integrates the motion model:
 *   x'   = x   + v * cos(yaw) * dt
 *   y'   = y   + v * sin(yaw) * dt
 *   yaw' = yaw + omega * dt
 *
 * Propagates covariance through the linearised Jacobian F.
 *
 * @param state   EKF state (modified in-place).
 * @param params  Noise parameters.
 * @param v       Linear velocity (m/s) from odometry.
 * @param omega   Angular velocity (rad/s) from IMU.
 * @param dt      Time step (s).
 */
void ekfPredict(
  EkfState & state,
  const EkfParameters & params,
  double v,
  double omega,
  double dt)
{
  if (dt <= 0.0) { return; }

  const double cos_yaw = std::cos(state.yaw);
  const double sin_yaw = std::sin(state.yaw);

  // State update
  state.x   += v * cos_yaw * dt;
  state.y   += v * sin_yaw * dt;
  state.yaw  = normaliseAngle(state.yaw + omega * dt);

  // Jacobian of motion model w.r.t. state  F (3x3, row-major)
  //      dX'/dX = [1, 0, -v*sin(yaw)*dt]
  //               [0, 1,  v*cos(yaw)*dt ]
  //               [0, 0,  1             ]
  const double f02 = -v * sin_yaw * dt;
  const double f12 =  v * cos_yaw * dt;

  // F * P  (3x3 * 3x3)
  auto & P = state.P;

  // Row 0 of F*P
  const double fp00 = P[0] + f02 * P[6];
  const double fp01 = P[1] + f02 * P[7];
  const double fp02 = P[2] + f02 * P[8];
  // Row 1 of F*P
  const double fp10 = P[3] + f12 * P[6];
  const double fp11 = P[4] + f12 * P[7];
  const double fp12 = P[5] + f12 * P[8];
  // Row 2 of F*P (unchanged row of F)
  const double fp20 = P[6];
  const double fp21 = P[7];
  const double fp22 = P[8];

  // F*P*F^T  (3x3 * 3x3^T)
  // (F^T has columns of F as rows, but since F is nearly I we expand)
  // P_new = F*P*F^T + Q
  P[0] = fp00 + fp02 * f02 + params.q_x;
  P[1] = fp01 + fp02 * f12;
  P[2] = fp02;
  P[3] = fp10 + fp12 * f02;
  P[4] = fp11 + fp12 * f12 + params.q_y;
  P[5] = fp12;
  P[6] = fp20;
  P[7] = fp21;
  P[8] = fp22 + params.q_yaw;
}

// ============================================================
// ekfUpdate
// ============================================================

/**
 * @brief EKF update (correction) step using an AMCL pose observation.
 *
 * Observation model is linear (H = I_3x3).
 * Kalman gain: K = P * H^T * (H * P * H^T + R)^{-1}
 * For H=I: K = P * (P + R)^{-1}  (3x3 diagonal R simplifies this).
 *
 * @param state   EKF state (modified in-place).
 * @param params  Noise parameters.
 * @param z_x     Observed x from AMCL.
 * @param z_y     Observed y from AMCL.
 * @param z_yaw   Observed yaw from AMCL.
 */
void ekfUpdate(
  EkfState & state,
  const EkfParameters & params,
  double z_x,
  double z_y,
  double z_yaw)
{
  auto & P = state.P;

  // S = P + R  (innovation covariance, diagonal R)
  const double s00 = P[0] + params.r_x;
  const double s11 = P[4] + params.r_y;
  const double s22 = P[8] + params.r_yaw;

  // For this simplified case with diagonal R and full P,
  // compute K = P * inv(P + R).
  // Simplification: treat S as block-diagonal using diagonal of S.
  // This is an approximation that works well when off-diagonals of P are small.
  const double k00 = P[0] / s00;
  const double k01 = P[1] / s11;
  const double k02 = P[2] / s22;
  const double k10 = P[3] / s00;
  const double k11 = P[4] / s11;
  const double k12 = P[5] / s22;
  const double k20 = P[6] / s00;
  const double k21 = P[7] / s11;
  const double k22 = P[8] / s22;

  // Innovation
  const double inn_x   = z_x   - state.x;
  const double inn_y   = z_y   - state.y;
  const double inn_yaw = normaliseAngle(z_yaw - state.yaw);

  // State update
  state.x   += k00 * inn_x + k01 * inn_y + k02 * inn_yaw;
  state.y   += k10 * inn_x + k11 * inn_y + k12 * inn_yaw;
  state.yaw  = normaliseAngle(
    state.yaw + k20 * inn_x + k21 * inn_y + k22 * inn_yaw);

  // Covariance update: P = (I - K) * P
  const double new_p00 = (1.0 - k00) * P[0] - k01 * P[3] - k02 * P[6];
  const double new_p01 = (1.0 - k00) * P[1] - k01 * P[4] - k02 * P[7];
  const double new_p02 = (1.0 - k00) * P[2] - k01 * P[5] - k02 * P[8];
  const double new_p10 = -k10 * P[0] + (1.0 - k11) * P[3] - k12 * P[6];
  const double new_p11 = -k10 * P[1] + (1.0 - k11) * P[4] - k12 * P[7];
  const double new_p12 = -k10 * P[2] + (1.0 - k11) * P[5] - k12 * P[8];
  const double new_p20 = -k20 * P[0] - k21 * P[3] + (1.0 - k22) * P[6];
  const double new_p21 = -k20 * P[1] - k21 * P[4] + (1.0 - k22) * P[7];
  const double new_p22 = -k20 * P[2] - k21 * P[5] + (1.0 - k22) * P[8];

  P = {new_p00, new_p01, new_p02,
       new_p10, new_p11, new_p12,
       new_p20, new_p21, new_p22};
}

// ============================================================
// ekfToPoseMsg
// ============================================================

/**
 * @brief Convert EKF state to a PoseWithCovarianceStamped message.
 *
 * @param state       Current EKF state.
 * @param stamp       ROS timestamp.
 * @param frame_id    Frame (typically "map").
 * @param[out] msg    Filled pose message.
 */
void ekfToPoseMsg(
  const EkfState & state,
  const rclcpp::Time & stamp,
  const std::string & frame_id,
  geometry_msgs::msg::PoseWithCovarianceStamped & msg)
{
  msg.header.stamp    = stamp;
  msg.header.frame_id = frame_id;

  msg.pose.pose.position.x = state.x;
  msg.pose.pose.position.y = state.y;
  msg.pose.pose.position.z = 0.0;

  // Convert yaw to quaternion
  msg.pose.pose.orientation.x = 0.0;
  msg.pose.pose.orientation.y = 0.0;
  msg.pose.pose.orientation.z = std::sin(state.yaw / 2.0);
  msg.pose.pose.orientation.w = std::cos(state.yaw / 2.0);

  // Fill 6x6 covariance (x, y, z, roll, pitch, yaw)
  // We only have x, y, yaw covariance from the 3-DOF EKF.
  msg.pose.covariance.fill(0.0);
  msg.pose.covariance[0]  = state.P[0];   // xx
  msg.pose.covariance[1]  = state.P[1];   // xy
  msg.pose.covariance[5]  = state.P[2];   // x-yaw
  msg.pose.covariance[6]  = state.P[3];   // yx
  msg.pose.covariance[7]  = state.P[4];   // yy
  msg.pose.covariance[11] = state.P[5];   // y-yaw
  msg.pose.covariance[30] = state.P[6];   // yaw-x
  msg.pose.covariance[31] = state.P[7];   // yaw-y
  msg.pose.covariance[35] = state.P[8];   // yaw-yaw
}

}  // namespace pose_estimator
}  // namespace module_1_localization
