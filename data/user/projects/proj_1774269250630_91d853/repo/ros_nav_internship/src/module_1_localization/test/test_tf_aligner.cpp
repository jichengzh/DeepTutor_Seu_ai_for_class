/**
 * @file test_tf_aligner.cpp
 * @brief GTest unit tests for TFAligner.
 *
 * Tests:
 *  1. test_default_parameter_values     – Verifies default extrinsics are zero/default.
 *  2. test_custom_laser_extrinsics      – Verifies custom laser extrinsics are accepted.
 *  3. test_custom_imu_extrinsics        – Verifies custom IMU extrinsics are accepted.
 *  4. test_node_construction_no_throw   – Node should construct without throwing.
 *  5. test_dynamic_param_change         – Setting params marks the node dirty.
 *  6. test_quaternion_from_rpy          – Unit test of RPY -> quaternion math.
 *  7. test_frame_name_parameters        – Verifies frame names are stored correctly.
 */

#include <gtest/gtest.h>

#include <cmath>
#include <memory>

#include <rclcpp/rclcpp.hpp>
#include <tf2/LinearMath/Quaternion.h>

#include "module_1_localization/tf_aligner.hpp"

using module_1_localization::TFAligner;

// ============================================================
// Test fixture
// ============================================================

class TFAlignerTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    rclcpp::init(0, nullptr);
  }

  void TearDown() override
  {
    rclcpp::shutdown();
  }
};

// ============================================================
// Test 1: Node construction does not throw
// ============================================================

TEST_F(TFAlignerTest, test_node_construction_no_throw)
{
  EXPECT_NO_THROW({
    auto node = std::make_shared<TFAligner>(rclcpp::NodeOptions{});
    EXPECT_NE(node, nullptr);
  });
}

// ============================================================
// Test 2: Default parameter values
// ============================================================

TEST_F(TFAlignerTest, test_default_parameter_values)
{
  auto node = std::make_shared<TFAligner>(rclcpp::NodeOptions{});
  ASSERT_NE(node, nullptr);

  EXPECT_EQ  (node->get_parameter("parent_frame").as_string(), "base_link");
  EXPECT_EQ  (node->get_parameter("laser_frame").as_string(),  "laser");
  EXPECT_EQ  (node->get_parameter("imu_frame").as_string(),    "imu_link");

  EXPECT_DOUBLE_EQ(node->get_parameter("laser_x").as_double(),     0.0);
  EXPECT_DOUBLE_EQ(node->get_parameter("laser_y").as_double(),     0.0);
  EXPECT_DOUBLE_EQ(node->get_parameter("laser_z").as_double(),     0.18);
  EXPECT_DOUBLE_EQ(node->get_parameter("laser_roll").as_double(),  0.0);
  EXPECT_DOUBLE_EQ(node->get_parameter("laser_pitch").as_double(), 0.0);
  EXPECT_DOUBLE_EQ(node->get_parameter("laser_yaw").as_double(),   0.0);

  EXPECT_DOUBLE_EQ(node->get_parameter("imu_x").as_double(),     0.0);
  EXPECT_DOUBLE_EQ(node->get_parameter("imu_y").as_double(),     0.0);
  EXPECT_DOUBLE_EQ(node->get_parameter("imu_z").as_double(),     0.1);
  EXPECT_DOUBLE_EQ(node->get_parameter("imu_roll").as_double(),  0.0);
  EXPECT_DOUBLE_EQ(node->get_parameter("imu_pitch").as_double(), 0.0);
  EXPECT_DOUBLE_EQ(node->get_parameter("imu_yaw").as_double(),   0.0);

  EXPECT_DOUBLE_EQ(node->get_parameter("broadcast_rate").as_double(), 1.0);
}

// ============================================================
// Test 3: Custom laser extrinsics via parameter overrides
// ============================================================

TEST_F(TFAlignerTest, test_custom_laser_extrinsics)
{
  rclcpp::NodeOptions opts;
  opts.parameter_overrides({
    rclcpp::Parameter("laser_x",     0.15),
    rclcpp::Parameter("laser_y",    -0.03),
    rclcpp::Parameter("laser_z",     0.25),
    rclcpp::Parameter("laser_roll",  0.01),
    rclcpp::Parameter("laser_pitch", 0.02),
    rclcpp::Parameter("laser_yaw",   M_PI / 4.0),
  });

  auto node = std::make_shared<TFAligner>(opts);
  ASSERT_NE(node, nullptr);

  EXPECT_NEAR(node->get_parameter("laser_x").as_double(),      0.15,        1e-9);
  EXPECT_NEAR(node->get_parameter("laser_y").as_double(),     -0.03,        1e-9);
  EXPECT_NEAR(node->get_parameter("laser_z").as_double(),      0.25,        1e-9);
  EXPECT_NEAR(node->get_parameter("laser_roll").as_double(),   0.01,        1e-9);
  EXPECT_NEAR(node->get_parameter("laser_pitch").as_double(),  0.02,        1e-9);
  EXPECT_NEAR(node->get_parameter("laser_yaw").as_double(),    M_PI / 4.0,  1e-9);
}

// ============================================================
// Test 4: Custom IMU extrinsics via parameter overrides
// ============================================================

TEST_F(TFAlignerTest, test_custom_imu_extrinsics)
{
  rclcpp::NodeOptions opts;
  opts.parameter_overrides({
    rclcpp::Parameter("imu_x",      0.05),
    rclcpp::Parameter("imu_y",      0.0),
    rclcpp::Parameter("imu_z",      0.12),
    rclcpp::Parameter("imu_roll",   0.0),
    rclcpp::Parameter("imu_pitch",  0.0),
    rclcpp::Parameter("imu_yaw",    M_PI),
  });

  auto node = std::make_shared<TFAligner>(opts);
  ASSERT_NE(node, nullptr);

  EXPECT_NEAR(node->get_parameter("imu_x").as_double(),     0.05,  1e-9);
  EXPECT_NEAR(node->get_parameter("imu_z").as_double(),     0.12,  1e-9);
  EXPECT_NEAR(node->get_parameter("imu_yaw").as_double(),   M_PI,  1e-9);
}

// ============================================================
// Test 5: Custom frame names
// ============================================================

TEST_F(TFAlignerTest, test_frame_name_parameters)
{
  rclcpp::NodeOptions opts;
  opts.parameter_overrides({
    rclcpp::Parameter("parent_frame", "robot_base"),
    rclcpp::Parameter("laser_frame",  "velodyne"),
    rclcpp::Parameter("imu_frame",    "xsens_imu"),
  });

  auto node = std::make_shared<TFAligner>(opts);
  ASSERT_NE(node, nullptr);

  EXPECT_EQ(node->get_parameter("parent_frame").as_string(), "robot_base");
  EXPECT_EQ(node->get_parameter("laser_frame").as_string(),  "velodyne");
  EXPECT_EQ(node->get_parameter("imu_frame").as_string(),    "xsens_imu");
}

// ============================================================
// Test 6: Dynamic parameter change updates internal state
// ============================================================

TEST_F(TFAlignerTest, test_dynamic_param_change)
{
  auto node = std::make_shared<TFAligner>(rclcpp::NodeOptions{});
  ASSERT_NE(node, nullptr);

  // Change laser_yaw to 90 degrees
  const double new_yaw = M_PI / 2.0;
  auto result = node->set_parameter(rclcpp::Parameter("laser_yaw", new_yaw));
  EXPECT_TRUE(result.successful)
    << "Dynamic parameter change should succeed. Reason: " << result.reason;

  // The parameter server should reflect the new value
  EXPECT_NEAR(node->get_parameter("laser_yaw").as_double(), new_yaw, 1e-9);
}

// ============================================================
// Test 7: RPY to quaternion math
// ============================================================

TEST_F(TFAlignerTest, test_quaternion_from_rpy_identity)
{
  // Roll=0, Pitch=0, Yaw=0 should give identity quaternion
  tf2::Quaternion q;
  q.setRPY(0.0, 0.0, 0.0);
  q.normalize();

  EXPECT_NEAR(q.x(), 0.0, 1e-9);
  EXPECT_NEAR(q.y(), 0.0, 1e-9);
  EXPECT_NEAR(q.z(), 0.0, 1e-9);
  EXPECT_NEAR(q.w(), 1.0, 1e-9);
}

TEST_F(TFAlignerTest, test_quaternion_from_rpy_180_yaw)
{
  // Yaw = 180 degrees should give q = (0, 0, 1, 0)  (up to normalisation)
  tf2::Quaternion q;
  q.setRPY(0.0, 0.0, M_PI);
  q.normalize();

  EXPECT_NEAR(q.x(), 0.0, 1e-6);
  EXPECT_NEAR(q.y(), 0.0, 1e-6);
  EXPECT_NEAR(std::abs(q.z()), 1.0, 1e-6);
  EXPECT_NEAR(std::abs(q.w()), 0.0, 1e-6);
}

TEST_F(TFAlignerTest, test_quaternion_normalisation)
{
  // After setRPY, quaternion must be unit length
  tf2::Quaternion q;
  q.setRPY(0.1, 0.2, 0.3);
  q.normalize();

  const double norm = std::sqrt(
    q.x()*q.x() + q.y()*q.y() + q.z()*q.z() + q.w()*q.w());
  EXPECT_NEAR(norm, 1.0, 1e-9);
}

// ============================================================
// Test 8: Node spins without crashing
// ============================================================

TEST_F(TFAlignerTest, test_node_spins_without_crash)
{
  auto node = std::make_shared<TFAligner>(rclcpp::NodeOptions{});
  ASSERT_NE(node, nullptr);

  auto executor = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
  executor->add_node(node);

  // Spin for 200 ms to allow timer to fire at least once (rate=1 Hz, so
  // the timer won't fire, but the node should not crash).
  EXPECT_NO_THROW({
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(200);
    while (std::chrono::steady_clock::now() < deadline) {
      executor->spin_some(std::chrono::milliseconds(10));
    }
  });
}

// ============================================================
// main
// ============================================================

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
