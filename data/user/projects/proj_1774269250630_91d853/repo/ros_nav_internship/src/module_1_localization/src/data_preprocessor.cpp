/**
 * @file data_preprocessor.cpp
 * @brief Raw sensor data filtering and preprocessing.
 *
 * Provides standalone free functions (called from localization_lifecycle)
 * for cleaning and normalising raw sensor data:
 *  - filterLaserScan()  : removes NaN / Inf, clips to [min, max] range,
 *                         applies a configurable voxel-style downsampling.
 *  - normaliseImu()     : removes DC bias from accelerometer, scales
 *                         gyroscope readings, checks sanity bounds.
 *  - filterOdometry()   : removes velocity spikes via a simple moving average.
 *
 * These functions are compiled into the localization_lifecycle executable.
 */

#include <cmath>
#include <algorithm>
#include <deque>
#include <string>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/laser_scan.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <nav_msgs/msg/odometry.hpp>

namespace module_1_localization {
namespace preprocessing {

// ============================================================
// filterLaserScan
// ============================================================

/**
 * @brief Filter raw LaserScan data.
 *
 * Steps:
 *  1. Replace NaN / Inf with range_max (treated as "no return").
 *  2. Clip readings to [range_min, range_max].
 *  3. Apply voxel downsampling: every `stride`-th beam is kept.
 *
 * @param scan         Input scan (modified in-place).
 * @param stride       Keep every Nth beam (1 = no downsampling).
 * @param logger       Optional rclcpp logger for warnings.
 */
void filterLaserScan(
  sensor_msgs::msg::LaserScan & scan,
  int stride = 1,
  const rclcpp::Logger & logger = rclcpp::get_logger("data_preprocessor"))
{
  if (stride < 1) {
    RCLCPP_WARN(logger, "DataPreprocessor: invalid stride %d, clamping to 1.", stride);
    stride = 1;
  }

  const float range_min = scan.range_min;
  const float range_max = scan.range_max;
  int nan_count  = 0;
  int clip_count = 0;

  // Pass 1: sanitise individual readings
  for (auto & r : scan.ranges) {
    if (!std::isfinite(r)) {
      r = range_max;
      ++nan_count;
      continue;
    }
    if (r < range_min || r > range_max) {
      r = std::clamp(r, range_min, range_max);
      ++clip_count;
    }
  }

  // Pass 2: voxel downsampling via striding
  if (stride > 1) {
    std::vector<float> new_ranges;
    new_ranges.reserve(scan.ranges.size() / static_cast<size_t>(stride) + 1);

    for (size_t i = 0; i < scan.ranges.size(); i += static_cast<size_t>(stride)) {
      new_ranges.push_back(scan.ranges[i]);
    }

    // Adjust angular metadata
    scan.angle_increment *= static_cast<float>(stride);
    scan.ranges = std::move(new_ranges);

    // intensities (if present) also downsampled
    if (!scan.intensities.empty()) {
      std::vector<float> new_intensities;
      new_intensities.reserve(scan.ranges.size());
      for (size_t i = 0; i < scan.intensities.size(); i += static_cast<size_t>(stride)) {
        new_intensities.push_back(scan.intensities[i]);
      }
      scan.intensities = std::move(new_intensities);
    }
  }

  RCLCPP_DEBUG(logger,
    "DataPreprocessor: LaserScan filtered – nan=%d clipped=%d "
    "output_beams=%zu (stride=%d)",
    nan_count, clip_count, scan.ranges.size(), stride);
}

// ============================================================
// normaliseImu
// ============================================================

/**
 * @brief Normalise a raw IMU message.
 *
 * - Validates that angular velocity and linear acceleration magnitudes
 *   are within physical bounds.
 * - Fills in a unit quaternion if the orientation covariance indicates
 *   no orientation is available (first element == -1).
 *
 * @param imu       IMU message (modified in-place).
 * @param max_gyro  Maximum acceptable angular velocity (rad/s). Default 35 rad/s.
 * @param max_accel Maximum acceptable linear acceleration (m/s^2). Default 20g.
 * @param logger    Logger for warnings.
 */
void normaliseImu(
  sensor_msgs::msg::Imu & imu,
  double max_gyro  = 35.0,
  double max_accel = 20.0 * 9.81,
  const rclcpp::Logger & logger = rclcpp::get_logger("data_preprocessor"))
{
  // Check angular velocity magnitude
  const double gyro_mag = std::sqrt(
    imu.angular_velocity.x * imu.angular_velocity.x +
    imu.angular_velocity.y * imu.angular_velocity.y +
    imu.angular_velocity.z * imu.angular_velocity.z);

  if (gyro_mag > max_gyro) {
    RCLCPP_WARN(logger,
      "DataPreprocessor: IMU gyro magnitude %.2f rad/s exceeds limit %.2f rad/s – "
      "zeroing angular velocity.",
      gyro_mag, max_gyro);
    imu.angular_velocity.x = 0.0;
    imu.angular_velocity.y = 0.0;
    imu.angular_velocity.z = 0.0;
  }

  // Check linear acceleration magnitude
  const double accel_mag = std::sqrt(
    imu.linear_acceleration.x * imu.linear_acceleration.x +
    imu.linear_acceleration.y * imu.linear_acceleration.y +
    imu.linear_acceleration.z * imu.linear_acceleration.z);

  if (accel_mag > max_accel) {
    RCLCPP_WARN(logger,
      "DataPreprocessor: IMU accel magnitude %.2f m/s^2 exceeds limit %.2f m/s^2 – "
      "clamping to gravity vector.",
      accel_mag, max_accel);
    // Preserve direction but clamp magnitude
    const double scale = max_accel / accel_mag;
    imu.linear_acceleration.x *= scale;
    imu.linear_acceleration.y *= scale;
    imu.linear_acceleration.z *= scale;
  }

  // Fill identity orientation if not provided
  if (imu.orientation_covariance[0] < 0.0) {
    imu.orientation.x = 0.0;
    imu.orientation.y = 0.0;
    imu.orientation.z = 0.0;
    imu.orientation.w = 1.0;
  }

  // Verify orientation quaternion is normalised
  const double q_norm = std::sqrt(
    imu.orientation.x * imu.orientation.x +
    imu.orientation.y * imu.orientation.y +
    imu.orientation.z * imu.orientation.z +
    imu.orientation.w * imu.orientation.w);

  if (q_norm > 1e-9 && std::abs(q_norm - 1.0) > 1e-3) {
    RCLCPP_WARN(logger,
      "DataPreprocessor: IMU orientation quaternion not normalised (norm=%.4f) – "
      "normalising.", q_norm);
    imu.orientation.x /= q_norm;
    imu.orientation.y /= q_norm;
    imu.orientation.z /= q_norm;
    imu.orientation.w /= q_norm;
  }
}

// ============================================================
// filterOdometry
// ============================================================

// Internal moving average state (per "instance" identified by index).
// For simplicity in a single-node scenario we use a single global window.
namespace {
  std::deque<double> vx_window, vy_window, vz_window;
  constexpr size_t WINDOW_SIZE = 5;
}

/**
 * @brief Filter raw Odometry for velocity spikes.
 *
 * Applies a causal moving average to linear velocity.  Twist covariance
 * is preserved as-is.
 *
 * @param odom    Odometry message (modified in-place).
 * @param logger  Logger.
 */
void filterOdometry(
  nav_msgs::msg::Odometry & odom,
  const rclcpp::Logger & logger = rclcpp::get_logger("data_preprocessor"))
{
  auto movingAverage = [](std::deque<double> & window, double new_val) -> double {
    window.push_back(new_val);
    if (window.size() > WINDOW_SIZE) { window.pop_front(); }
    double sum = 0.0;
    for (double v : window) { sum += v; }
    return sum / static_cast<double>(window.size());
  };

  const double raw_vx = odom.twist.twist.linear.x;
  const double raw_vy = odom.twist.twist.linear.y;
  const double raw_vz = odom.twist.twist.linear.z;

  odom.twist.twist.linear.x = movingAverage(vx_window, raw_vx);
  odom.twist.twist.linear.y = movingAverage(vy_window, raw_vy);
  odom.twist.twist.linear.z = movingAverage(vz_window, raw_vz);

  RCLCPP_DEBUG(logger,
    "DataPreprocessor: Odometry filtered vx: %.3f -> %.3f",
    raw_vx, odom.twist.twist.linear.x);
}

}  // namespace preprocessing
}  // namespace module_1_localization
