#include <gtest/gtest.h>
#include "module_4_obstacle/obstacle_ekf.hpp"
#include <cmath>

using module_4_obstacle::ObstacleEKF;

// ─────────────────────────────────────────────────────────────────────────────
// Helper: absolute tolerance for Eigen vectors
// ─────────────────────────────────────────────────────────────────────────────
static bool vec3_near(const Eigen::Vector3d& a, const Eigen::Vector3d& b, double tol)
{
  return (a - b).cwiseAbs().maxCoeff() < tol;
}

// ─────────────────────────────────────────────────────────────────────────────
// 1. Construction: state initialised correctly
// ─────────────────────────────────────────────────────────────────────────────
TEST(ObstacleEKFTest, ConstructionInitialisesPosition)
{
  const Eigen::Vector3d init_pos(1.0, 2.0, 0.5);
  ObstacleEKF ekf(init_pos);

  EXPECT_NEAR(ekf.getPosition().x(), 1.0, 1e-9);
  EXPECT_NEAR(ekf.getPosition().y(), 2.0, 1e-9);
  EXPECT_NEAR(ekf.getPosition().z(), 0.5, 1e-9);

  // Initial velocity should be zero
  EXPECT_NEAR(ekf.getVelocity().norm(), 0.0, 1e-9);

  // time_since_update should be 0
  EXPECT_NEAR(ekf.getTimeSinceUpdate(), 0.0, 1e-9);
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. Predict: position advances proportionally to velocity
// ─────────────────────────────────────────────────────────────────────────────
TEST(ObstacleEKFTest, PredictAdvancesPosition)
{
  const Eigen::Vector3d init_pos(0.0, 0.0, 0.0);
  ObstacleEKF ekf(init_pos);

  // Manually seed the velocity through repeated updates at known positions
  // First update at origin sets velocity to ~0
  ekf.update(Eigen::Vector3d(0.0, 0.0, 0.0));

  // Predict forward by 1 s
  ekf.predict(1.0);

  // Covariance should grow during prediction (uncertainty increases)
  const Eigen::MatrixXd P = ekf.getCovariance();
  EXPECT_GT(P(0, 0), 0.0);
  EXPECT_GT(P(1, 1), 0.0);

  // time_since_update should have incremented
  EXPECT_GT(ekf.getTimeSinceUpdate(), 0.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. Predict + Update cycle: converges toward truth for constant-velocity object
// ─────────────────────────────────────────────────────────────────────────────
TEST(ObstacleEKFTest, PredictUpdateCycleConverges)
{
  // True object: starts at (0,0,0), moves at (1.0, 0.5, 0.0) m/s
  const Eigen::Vector3d init_pos(0.0, 0.0, 0.0);
  const Eigen::Vector3d true_vel(1.0, 0.5, 0.0);

  ObstacleEKF ekf(init_pos, 0.05, 0.1);

  const double dt = 0.1;
  const int    N  = 50;

  Eigen::Vector3d true_pos = init_pos;
  for (int i = 0; i < N; ++i) {
    ekf.predict(dt);
    true_pos += true_vel * dt;
    ekf.update(true_pos);  // noise-free measurement
  }

  // After convergence, estimated position should be close to truth
  const Eigen::Vector3d est_pos = ekf.getPosition();
  EXPECT_NEAR(est_pos.x(), true_pos.x(), 0.05);
  EXPECT_NEAR(est_pos.y(), true_pos.y(), 0.05);

  // Estimated velocity should be close to true velocity
  const Eigen::Vector3d est_vel = ekf.getVelocity();
  EXPECT_NEAR(est_vel.x(), true_vel.x(), 0.1);
  EXPECT_NEAR(est_vel.y(), true_vel.y(), 0.1);

  // time_since_update should be 0 (updated in last iteration)
  EXPECT_NEAR(ekf.getTimeSinceUpdate(), 0.0, 1e-9);
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. Covariance: update reduces covariance relative to predict-only
// ─────────────────────────────────────────────────────────────────────────────
TEST(ObstacleEKFTest, UpdateReducesCovariance)
{
  ObstacleEKF ekf(Eigen::Vector3d(0.0, 0.0, 0.0));

  // Record covariance after one predict step
  ekf.predict(0.1);
  const double P_before = ekf.getCovariance()(0, 0);

  // After update, uncertainty should decrease
  ekf.update(Eigen::Vector3d(0.1, 0.0, 0.0));
  const double P_after = ekf.getCovariance()(0, 0);

  EXPECT_LT(P_after, P_before);
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. predictTrajectory: correct number of steps, positions advance
// ─────────────────────────────────────────────────────────────────────────────
TEST(ObstacleEKFTest, PredictTrajectoryReturnsCorrectCount)
{
  ObstacleEKF ekf(Eigen::Vector3d(0.0, 0.0, 0.0));

  const int steps = 10;
  const auto traj = ekf.predictTrajectory(0.1, steps);

  ASSERT_EQ(static_cast<int>(traj.size()), steps);
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. predictTrajectory: accuracy for a constant-velocity object
// ─────────────────────────────────────────────────────────────────────────────
TEST(ObstacleEKFTest, PredictTrajectoryAccuracy)
{
  // Converge the filter to a known velocity first
  const Eigen::Vector3d true_vel(1.0, 0.0, 0.0);
  ObstacleEKF ekf(Eigen::Vector3d(0.0, 0.0, 0.0), 0.01, 0.01);

  // Run many predict+update cycles to lock in the velocity
  const double dt = 0.1;
  Eigen::Vector3d pos(0.0, 0.0, 0.0);
  for (int i = 0; i < 100; ++i) {
    ekf.predict(dt);
    pos += true_vel * dt;
    ekf.update(pos);
  }

  // Now check trajectory prediction
  const int   steps      = 5;
  const auto  traj       = ekf.predictTrajectory(dt, steps);
  ASSERT_EQ(static_cast<int>(traj.size()), steps);

  const Eigen::Vector3d start = ekf.getPosition();
  for (int i = 0; i < steps; ++i) {
    const double expected_x = start.x() + true_vel.x() * dt * (i + 1);
    // Allow generous tolerance because state is noisy
    EXPECT_NEAR(traj[i].x(), expected_x, 0.1)
        << "Trajectory step " << i << " x deviation too large";
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. predictTrajectory: does NOT modify internal state
// ─────────────────────────────────────────────────────────────────────────────
TEST(ObstacleEKFTest, PredictTrajectoryDoesNotMutateState)
{
  ObstacleEKF ekf(Eigen::Vector3d(3.0, 1.0, 0.0));
  ekf.update(Eigen::Vector3d(3.0, 1.0, 0.0));

  const Eigen::VectorXd state_before = ekf.getState();

  ekf.predictTrajectory(0.1, 20);  // should not change internal state

  const Eigen::VectorXd state_after = ekf.getState();
  EXPECT_TRUE(state_before.isApprox(state_after, 1e-12));
}

// ─────────────────────────────────────────────────────────────────────────────
// 8. Negative or zero dt: predict() is a no-op
// ─────────────────────────────────────────────────────────────────────────────
TEST(ObstacleEKFTest, PredictWithZeroDtIsNoop)
{
  ObstacleEKF ekf(Eigen::Vector3d(1.0, 2.0, 3.0));
  const Eigen::VectorXd state_before = ekf.getState();

  ekf.predict(0.0);

  EXPECT_TRUE(ekf.getState().isApprox(state_before, 1e-12));
}

// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
