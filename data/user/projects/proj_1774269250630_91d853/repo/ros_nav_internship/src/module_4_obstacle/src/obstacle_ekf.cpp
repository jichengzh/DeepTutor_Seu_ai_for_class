#include "module_4_obstacle/obstacle_ekf.hpp"
#include <stdexcept>

namespace module_4_obstacle {

// ─────────────────────────────────────────────────────────────────────────────
ObstacleEKF::ObstacleEKF(const Eigen::Vector3d& initial_pos,
                          double process_noise_std,
                          double measurement_noise_std)
{
  // ── State: [x, y, z, vx, vy, vz] ─────────────────────────────────────────
  x_ = Eigen::VectorXd::Zero(6);
  x_.head<3>() = initial_pos;

  // ── Initial covariance: moderate position certainty, high velocity uncertainty
  P_ = Eigen::MatrixXd::Identity(6, 6);
  P_.topLeftCorner<3, 3>()     *= 1.0;   // position uncertainty [m²]
  P_.bottomRightCorner<3, 3>() *= 10.0;  // velocity uncertainty [m²/s²]

  // ── Process noise Q (constant-velocity + white-noise acceleration model)
  // Will be rebuilt in predict() with the actual dt; initialise to identity here.
  Q_ = Eigen::MatrixXd::Identity(6, 6);
  Q_ *= process_noise_std * process_noise_std;

  // ── Measurement noise R ────────────────────────────────────────────────────
  R_ = Eigen::MatrixXd::Identity(3, 3);
  R_ *= measurement_noise_std * measurement_noise_std;

  // ── Observation matrix H: extracts position from state ────────────────────
  H_ = Eigen::MatrixXd::Zero(3, 6);
  H_.block<3, 3>(0, 0) = Eigen::Matrix3d::Identity();

  // ── State transition F: built in predict() per dt; initialise as identity ─
  F_ = Eigen::MatrixXd::Identity(6, 6);
}

// ─────────────────────────────────────────────────────────────────────────────
void ObstacleEKF::predict(double dt)
{
  if (dt <= 0.0) return;

  // ── Rebuild state transition matrix for this dt ───────────────────────────
  // F = I + dt * A   where A couples position to velocity
  //   [ I   dt*I ]
  //   [ 0     I  ]
  F_ = Eigen::MatrixXd::Identity(6, 6);
  F_.block<3, 3>(0, 3) = Eigen::Matrix3d::Identity() * dt;

  // ── Rebuild process noise Q (discrete-time white-noise acceleration model)
  // Q = G * G^T * sigma_a²   where G = [dt²/2, dt]^T per axis
  const double dt2   = dt * dt;
  const double dt3   = dt2 * dt;
  const double dt4   = dt2 * dt2;
  const double sigma2 = Q_(0, 0);  // reuse stored variance

  Q_ = Eigen::MatrixXd::Zero(6, 6);
  // top-left  [pos-pos block]
  Q_.block<3, 3>(0, 0) = Eigen::Matrix3d::Identity() * (dt4 / 4.0) * sigma2;
  // top-right [pos-vel block]
  Q_.block<3, 3>(0, 3) = Eigen::Matrix3d::Identity() * (dt3 / 2.0) * sigma2;
  // bottom-left [vel-pos block]
  Q_.block<3, 3>(3, 0) = Eigen::Matrix3d::Identity() * (dt3 / 2.0) * sigma2;
  // bottom-right [vel-vel block]
  Q_.block<3, 3>(3, 3) = Eigen::Matrix3d::Identity() * dt2 * sigma2;

  // ── Predict: x = F * x ────────────────────────────────────────────────────
  x_ = F_ * x_;

  // ── Predict: P = F * P * F^T + Q ─────────────────────────────────────────
  P_ = F_ * P_ * F_.transpose() + Q_;

  // Accumulate time since last measurement
  time_since_update_ += dt;
}

// ─────────────────────────────────────────────────────────────────────────────
void ObstacleEKF::update(const Eigen::Vector3d& measurement)
{
  // ── Innovation: y = z - H * x ─────────────────────────────────────────────
  const Eigen::Vector3d y = measurement - H_ * x_;

  // ── Innovation covariance: S = H * P * H^T + R ───────────────────────────
  const Eigen::Matrix3d S = H_ * P_ * H_.transpose() + R_;

  // ── Kalman gain: K = P * H^T * S^{-1} ────────────────────────────────────
  const Eigen::MatrixXd K = P_ * H_.transpose() * S.inverse();

  // ── State update: x = x + K * y ───────────────────────────────────────────
  x_ = x_ + K * y;

  // ── Covariance update (Joseph form for numerical stability):
  // P = (I - K*H) * P * (I - K*H)^T + K * R * K^T
  const Eigen::MatrixXd I_KH =
      Eigen::MatrixXd::Identity(6, 6) - K * H_;
  P_ = I_KH * P_ * I_KH.transpose() + K * R_ * K.transpose();

  // Reset time since last update
  time_since_update_ = 0.0;
}

// ─────────────────────────────────────────────────────────────────────────────
std::vector<Eigen::Vector3d> ObstacleEKF::predictTrajectory(
    double dt, int steps) const
{
  if (dt <= 0.0 || steps <= 0) return {};

  std::vector<Eigen::Vector3d> trajectory;
  trajectory.reserve(steps);

  // Work on copies of the internal state
  Eigen::VectorXd x_sim = x_;

  // Build constant F for this dt (same formula as predict())
  Eigen::MatrixXd F_sim = Eigen::MatrixXd::Identity(6, 6);
  F_sim.block<3, 3>(0, 3) = Eigen::Matrix3d::Identity() * dt;

  for (int i = 0; i < steps; ++i) {
    x_sim = F_sim * x_sim;
    trajectory.push_back(x_sim.head<3>());
  }

  return trajectory;
}

}  // namespace module_4_obstacle
