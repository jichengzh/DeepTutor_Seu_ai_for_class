#pragma once
#include <Eigen/Dense>
#include <vector>
#include <rclcpp/rclcpp.hpp>

namespace module_4_obstacle {

/**
 * @brief Extended Kalman Filter for tracking a single 3-D obstacle.
 *
 * State vector:  x = [x, y, z, vx, vy, vz]^T   (6 × 1)
 * Observation:   z = [x, y, z]^T                (3 × 1)
 *
 * The motion model is constant-velocity.  Because the model is linear, this
 * filter is mathematically equivalent to a plain KF, but the class is named
 * EKF to expose the interface expected by the rest of the system and to make
 * it trivial to extend with nonlinear models later.
 */
class ObstacleEKF {
public:
  /**
   * @brief Construct and initialise the filter.
   * @param initial_pos        Initial position measurement [m].
   * @param process_noise_std  Standard deviation for Q (acceleration noise).
   * @param measurement_noise_std  Standard deviation for R (position noise).
   */
  ObstacleEKF(const Eigen::Vector3d& initial_pos,
               double process_noise_std = 0.1,
               double measurement_noise_std = 0.2);

  /** @brief Predict state forward by dt seconds using the CV model. */
  void predict(double dt);

  /** @brief Update with a new position measurement. */
  void update(const Eigen::Vector3d& measurement);

  // ── Accessors ─────────────────────────────────────────────────────────────
  Eigen::VectorXd getState()      const { return x_; }
  Eigen::MatrixXd getCovariance() const { return P_; }
  Eigen::Vector3d getPosition()   const { return x_.head<3>(); }
  Eigen::Vector3d getVelocity()   const { return x_.tail<3>(); }
  double          getTimeSinceUpdate() const { return time_since_update_; }
  void            incrementTimeSinceUpdate(double dt) { time_since_update_ += dt; }

  /**
   * @brief Predict future positions without modifying internal state.
   * @param dt    Time step between predictions [s].
   * @param steps Number of future steps.
   * @return      Vector of predicted positions.
   */
  std::vector<Eigen::Vector3d> predictTrajectory(double dt, int steps) const;

private:
  Eigen::VectorXd x_;   ///< State [x, y, z, vx, vy, vz]
  Eigen::MatrixXd P_;   ///< Covariance 6×6
  Eigen::MatrixXd Q_;   ///< Process noise 6×6
  Eigen::MatrixXd R_;   ///< Measurement noise 3×3
  Eigen::MatrixXd F_;   ///< State transition 6×6 (updated each predict call)
  Eigen::MatrixXd H_;   ///< Observation matrix 3×6 (constant)
  double time_since_update_{0.0};
};

}  // namespace module_4_obstacle
