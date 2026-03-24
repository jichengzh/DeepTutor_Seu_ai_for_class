#include <gtest/gtest.h>
#include <cmath>
#include <vector>

// ── Inline reimplementation of the cone functions for unit testing ────────────
// These mirror the logic in ObstacleLayerExt and SafetyGuardian exactly.
// We test the formulas in isolation so the tests don't depend on ROS / nav2.

namespace {

/**
 * @brief Compute the velocity-dependent collision cone radius.
 *
 * Mirrors ObstacleLayerExt::computeConeRadius():
 *   r = v_relative * reaction_time + robot_radius
 */
double computeConeRadius(double v_relative, double reaction_time, double robot_radius)
{
  return v_relative * reaction_time + robot_radius;
}

/**
 * @brief Check whether a point is inside the safety cone.
 *
 * @param obs_x, obs_y   Obstacle centroid in robot frame.
 * @param cone_radius    Radius returned by computeConeRadius().
 * @return               true if the point is within the cone.
 */
bool isInsideCone(double obs_x, double obs_y, double cone_radius)
{
  return std::hypot(obs_x, obs_y) <= cone_radius;
}

/**
 * @brief Determine whether the minimum obstacle distance causes a safety
 *        violation given the configured safety_distance threshold.
 *
 * Mirrors SafetyGuardian's check logic.
 */
bool isSafetyViolated(double min_obstacle_dist, double safety_distance)
{
  return min_obstacle_dist < safety_distance;
}

}  // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// ── computeConeRadius tests ───────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────

TEST(CollisionConeTest, ConeRadiusZeroVelocity)
{
  // With zero relative velocity, cone radius equals the robot radius
  const double r = computeConeRadius(0.0, 0.5, 0.3);
  EXPECT_NEAR(r, 0.3, 1e-9);
}

TEST(CollisionConeTest, ConeRadiusDefaultParams)
{
  // v=1.0 m/s, t_r=0.5 s, r_robot=0.3 m → r = 1.0*0.5 + 0.3 = 0.8 m
  const double r = computeConeRadius(1.0, 0.5, 0.3);
  EXPECT_NEAR(r, 0.8, 1e-9);
}

TEST(CollisionConeTest, ConeRadiusHighSpeed)
{
  // v=4.0 m/s, t_r=1.0 s, r_robot=0.5 m → r = 4.0 + 0.5 = 4.5 m
  const double r = computeConeRadius(4.0, 1.0, 0.5);
  EXPECT_NEAR(r, 4.5, 1e-9);
}

TEST(CollisionConeTest, ConeRadiusScalesLinearlyWithSpeed)
{
  const double t_r    = 0.5;
  const double r_bot  = 0.3;
  const double r1     = computeConeRadius(1.0, t_r, r_bot);
  const double r2     = computeConeRadius(2.0, t_r, r_bot);

  // Difference should equal v_delta * t_r = 1.0 * 0.5 = 0.5
  EXPECT_NEAR(r2 - r1, 0.5, 1e-9);
}

TEST(CollisionConeTest, ConeRadiusScalesLinearlyWithReactionTime)
{
  const double v_rel  = 1.0;
  const double r_bot  = 0.3;
  const double r1     = computeConeRadius(v_rel, 0.5, r_bot);
  const double r2     = computeConeRadius(v_rel, 1.0, r_bot);

  // Difference should equal v_rel * dt_r = 1.0 * 0.5 = 0.5
  EXPECT_NEAR(r2 - r1, 0.5, 1e-9);
}

// ─────────────────────────────────────────────────────────────────────────────
// ── isInsideCone tests ────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────

TEST(CollisionConeTest, PointAtOriginAlwaysInsideCone)
{
  const double cone_r = computeConeRadius(1.0, 0.5, 0.3);
  EXPECT_TRUE(isInsideCone(0.0, 0.0, cone_r));
}

TEST(CollisionConeTest, PointJustInsideCone)
{
  const double cone_r = computeConeRadius(1.0, 0.5, 0.3);  // = 0.8
  EXPECT_TRUE(isInsideCone(0.0, 0.79, cone_r));
}

TEST(CollisionConeTest, PointOnConeBoundary)
{
  const double cone_r = computeConeRadius(1.0, 0.5, 0.3);  // = 0.8
  EXPECT_TRUE(isInsideCone(cone_r, 0.0, cone_r));
}

TEST(CollisionConeTest, PointJustOutsideCone)
{
  const double cone_r = computeConeRadius(1.0, 0.5, 0.3);  // = 0.8
  EXPECT_FALSE(isInsideCone(0.0, cone_r + 0.01, cone_r));
}

TEST(CollisionConeTest, DiagonalPointInsideCone)
{
  // Point at (0.5, 0.5): distance = sqrt(0.5) ≈ 0.707, cone_r = 0.8
  const double cone_r = computeConeRadius(1.0, 0.5, 0.3);
  EXPECT_TRUE(isInsideCone(0.5, 0.5, cone_r));
}

TEST(CollisionConeTest, DiagonalPointOutsideCone)
{
  // Point at (0.7, 0.7): distance = sqrt(0.98) ≈ 0.99, cone_r = 0.8
  const double cone_r = computeConeRadius(1.0, 0.5, 0.3);
  EXPECT_FALSE(isInsideCone(0.7, 0.7, cone_r));
}

// ─────────────────────────────────────────────────────────────────────────────
// ── isSafetyViolated tests ────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────

TEST(SafetyDistanceTest, NoViolationWhenFarAway)
{
  EXPECT_FALSE(isSafetyViolated(5.0, 1.5));
}

TEST(SafetyDistanceTest, NoViolationAtExactThreshold)
{
  // Violation is strict-less-than, so exactly equal is NOT a violation
  EXPECT_FALSE(isSafetyViolated(1.5, 1.5));
}

TEST(SafetyDistanceTest, ViolationJustInsideThreshold)
{
  EXPECT_TRUE(isSafetyViolated(1.49, 1.5));
}

TEST(SafetyDistanceTest, ViolationAtZeroDistance)
{
  EXPECT_TRUE(isSafetyViolated(0.0, 1.5));
}

TEST(SafetyDistanceTest, ViolationWithCustomThreshold)
{
  // safety_distance = 0.5 m
  EXPECT_TRUE( isSafetyViolated(0.3, 0.5));
  EXPECT_FALSE(isSafetyViolated(0.6, 0.5));
}

// ─────────────────────────────────────────────────────────────────────────────
// ── Combined: cone radius drives safety violation ────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────

TEST(CollisionConeTest, ObstacleInsideConeTriggersViolation)
{
  const double v_rel  = 1.0;
  const double t_r    = 0.5;
  const double r_bot  = 0.3;
  const double cone_r = computeConeRadius(v_rel, t_r, r_bot);  // = 0.8

  // Obstacle at (0.5, 0.0): dist = 0.5 < 0.8 → inside cone → violation
  const bool inside   = isInsideCone(0.5, 0.0, cone_r);
  EXPECT_TRUE(inside);
  EXPECT_TRUE(isSafetyViolated(0.5, cone_r));
}

TEST(CollisionConeTest, ObstacleOutsideConeNoViolation)
{
  const double cone_r = computeConeRadius(1.0, 0.5, 0.3);  // = 0.8

  // Obstacle at (1.0, 0.0): dist = 1.0 > 0.8 → outside cone → no violation
  const bool inside   = isInsideCone(1.0, 0.0, cone_r);
  EXPECT_FALSE(inside);
  EXPECT_FALSE(isSafetyViolated(1.0, cone_r));
}

// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
