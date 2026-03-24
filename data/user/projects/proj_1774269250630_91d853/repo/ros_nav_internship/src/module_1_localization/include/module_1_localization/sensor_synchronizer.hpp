#pragma once

/**
 * @file sensor_synchronizer.hpp
 * @brief Synchronizes LaserScan, IMU, and Odometry messages using
 *        ApproximateTime policy from message_filters.
 *
 * The SensorSynchronizer node subscribes to three sensor topics and
 * republishes temporally-aligned message triplets on dedicated output
 * topics. If the time gap between any pair of messages exceeds the
 * configured tolerance, a warning is emitted.
 */

#include <atomic>
#include <memory>
#include <string>

#include <rclcpp/rclcpp.hpp>
#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>
#include <sensor_msgs/msg/laser_scan.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <geometry_msgs/msg/pose_with_covariance_stamped.hpp>

namespace module_1_localization {

/**
 * @class SensorSynchronizer
 * @brief ROS2 node that time-aligns LaserScan, IMU, and Odometry messages.
 *
 * Internally uses message_filters::ApproximateTime policy. Aligned
 * messages are forwarded to /synced/scan, /synced/imu, /synced/odom.
 */
class SensorSynchronizer : public rclcpp::Node {
public:
  /**
   * @brief Construct and initialise all subscriptions and publishers.
   * @param options  Standard ROS2 node options (passed through for composition).
   */
  explicit SensorSynchronizer(const rclcpp::NodeOptions & options = rclcpp::NodeOptions{});

  /// Default destructor – resources managed by shared_ptr / RAII.
  ~SensorSynchronizer() override = default;

private:
  /**
   * @brief Callback invoked when a synchronised triplet is available.
   * @param scan  Laser scan message.
   * @param imu   IMU message.
   * @param odom  Odometry message.
   */
  void syncCallback(
    const sensor_msgs::msg::LaserScan::ConstSharedPtr & scan,
    const sensor_msgs::msg::Imu::ConstSharedPtr & imu,
    const nav_msgs::msg::Odometry::ConstSharedPtr & odom);

  /// Declare and read all ROS2 parameters from the parameter server.
  void declareParameters();

  // ----------------------------------------------------------------
  // Type aliases
  // ----------------------------------------------------------------
  using SyncPolicy = message_filters::sync_policies::ApproximateTime<
    sensor_msgs::msg::LaserScan,
    sensor_msgs::msg::Imu,
    nav_msgs::msg::Odometry>;

  // ----------------------------------------------------------------
  // Subscribers (message_filters)
  // ----------------------------------------------------------------
  message_filters::Subscriber<sensor_msgs::msg::LaserScan>  scan_sub_;
  message_filters::Subscriber<sensor_msgs::msg::Imu>        imu_sub_;
  message_filters::Subscriber<nav_msgs::msg::Odometry>      odom_sub_;

  /// Synchronizer instance – created after parameters are read.
  std::shared_ptr<message_filters::Synchronizer<SyncPolicy>> sync_;

  // ----------------------------------------------------------------
  // Publishers
  // ----------------------------------------------------------------
  rclcpp::Publisher<sensor_msgs::msg::LaserScan>::SharedPtr  synced_scan_pub_;
  rclcpp::Publisher<sensor_msgs::msg::Imu>::SharedPtr        synced_imu_pub_;
  rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr      synced_odom_pub_;

  // ----------------------------------------------------------------
  // Parameters
  // ----------------------------------------------------------------
  std::string scan_topic_{"scan"};
  std::string imu_topic_{"imu/data"};
  std::string odom_topic_{"odom"};
  double      sync_tolerance_ms_{10.0};  ///< Warning threshold in milliseconds.
  int         queue_size_{50};           ///< message_filters queue depth.

  // ----------------------------------------------------------------
  // Diagnostics
  // ----------------------------------------------------------------
  /// Number of successfully synchronised message triplets received.
  std::atomic<uint64_t> sync_count_{0};
};

}  // namespace module_1_localization
