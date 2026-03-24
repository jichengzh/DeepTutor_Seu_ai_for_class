/**
 * @file sensor_synchronizer.cpp
 * @brief Implementation of SensorSynchronizer.
 *
 * The node subscribes to three sensor topics (LaserScan, IMU, Odometry),
 * uses message_filters::ApproximateTime to align them in time, validates
 * the inter-sensor timestamp gap, and republishes the synchronised triplet
 * on /synced/{scan,imu,odom}.
 */

#include "module_1_localization/sensor_synchronizer.hpp"

#include <chrono>
#include <cmath>
#include <string>

using namespace std::chrono_literals;

namespace module_1_localization {

// ============================================================
// Constructor
// ============================================================

SensorSynchronizer::SensorSynchronizer(const rclcpp::NodeOptions & options)
: rclcpp::Node("sensor_synchronizer", options)
{
  // 1. Declare and read parameters
  declareParameters();

  scan_topic_       = get_parameter("scan_topic").as_string();
  imu_topic_        = get_parameter("imu_topic").as_string();
  odom_topic_       = get_parameter("odom_topic").as_string();
  sync_tolerance_ms_ = get_parameter("sync_tolerance_ms").as_double();
  queue_size_       = static_cast<int>(get_parameter("queue_size").as_int());

  RCLCPP_INFO(get_logger(),
    "SensorSynchronizer: scan='%s' imu='%s' odom='%s' "
    "tolerance=%.1f ms queue=%d",
    scan_topic_.c_str(), imu_topic_.c_str(), odom_topic_.c_str(),
    sync_tolerance_ms_, queue_size_);

  // 2. Create publishers
  synced_scan_pub_ = create_publisher<sensor_msgs::msg::LaserScan>(
    "synced/scan", rclcpp::QoS(10));
  synced_imu_pub_  = create_publisher<sensor_msgs::msg::Imu>(
    "synced/imu",  rclcpp::QoS(10));
  synced_odom_pub_ = create_publisher<nav_msgs::msg::Odometry>(
    "synced/odom", rclcpp::QoS(10));

  // 3. Initialise message_filters subscribers.
  //    The node's shared_ptr is passed so that message_filters can
  //    use it for creating subscriptions internally.
  scan_sub_.subscribe(this, scan_topic_);
  imu_sub_.subscribe(this,  imu_topic_);
  odom_sub_.subscribe(this, odom_topic_);

  // 4. Create synchronizer and connect callback.
  sync_ = std::make_shared<message_filters::Synchronizer<SyncPolicy>>(
    SyncPolicy(static_cast<unsigned int>(queue_size_)),
    scan_sub_, imu_sub_, odom_sub_);

  // Allow up to sync_tolerance_ms_ between earliest and latest stamp.
  sync_->setMaxIntervalDuration(
    rclcpp::Duration::from_seconds(sync_tolerance_ms_ / 1000.0));

  sync_->registerCallback(
    std::bind(&SensorSynchronizer::syncCallback, this,
      std::placeholders::_1,
      std::placeholders::_2,
      std::placeholders::_3));

  RCLCPP_INFO(get_logger(), "SensorSynchronizer initialised.");
}

// ============================================================
// declareParameters
// ============================================================

void SensorSynchronizer::declareParameters()
{
  declare_parameter<std::string>("scan_topic",        "scan");
  declare_parameter<std::string>("imu_topic",         "imu/data");
  declare_parameter<std::string>("odom_topic",        "odom");
  declare_parameter<double>     ("sync_tolerance_ms", 10.0);
  declare_parameter<int>        ("queue_size",        50);
}

// ============================================================
// syncCallback
// ============================================================

void SensorSynchronizer::syncCallback(
  const sensor_msgs::msg::LaserScan::ConstSharedPtr & scan,
  const sensor_msgs::msg::Imu::ConstSharedPtr &       imu,
  const nav_msgs::msg::Odometry::ConstSharedPtr &     odom)
{
  // ---- Validate: ensure all stamps are non-zero ----
  if (scan->header.stamp.sec == 0 &&
      imu->header.stamp.sec  == 0 &&
      odom->header.stamp.sec == 0)
  {
    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
      "SensorSynchronizer: all three stamps are zero – possible simulation issue.");
  }

  // ---- Compute pairwise time differences ----
  auto to_sec = [](const rclcpp::Time & t) { return t.seconds(); };

  const rclcpp::Time t_scan(scan->header.stamp);
  const rclcpp::Time t_imu (imu->header.stamp);
  const rclcpp::Time t_odom(odom->header.stamp);

  const double diff_scan_imu  = std::abs(to_sec(t_scan) - to_sec(t_imu))  * 1000.0;
  const double diff_scan_odom = std::abs(to_sec(t_scan) - to_sec(t_odom)) * 1000.0;
  const double diff_imu_odom  = std::abs(to_sec(t_imu)  - to_sec(t_odom)) * 1000.0;
  const double max_diff_ms    = std::max({diff_scan_imu, diff_scan_odom, diff_imu_odom});

  if (max_diff_ms > sync_tolerance_ms_) {
    RCLCPP_WARN(get_logger(),
      "SensorSynchronizer: max timestamp gap = %.2f ms exceeds tolerance %.2f ms "
      "(scan-imu=%.2f ms, scan-odom=%.2f ms, imu-odom=%.2f ms)",
      max_diff_ms, sync_tolerance_ms_,
      diff_scan_imu, diff_scan_odom, diff_imu_odom);
  }

  // ---- Re-publish synchronised messages ----
  synced_scan_pub_->publish(*scan);
  synced_imu_pub_ ->publish(*imu);
  synced_odom_pub_->publish(*odom);

  // ---- Increment counter and log periodically ----
  const uint64_t count = ++sync_count_;
  if (count % 100 == 0) {
    RCLCPP_DEBUG(get_logger(),
      "SensorSynchronizer: synchronised %lu triplets (latest gap %.2f ms)",
      static_cast<unsigned long>(count), max_diff_ms);
  }
}

}  // namespace module_1_localization

// ============================================================
// main
// ============================================================
#include <rclcpp/rclcpp.hpp>

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(
    std::make_shared<module_1_localization::SensorSynchronizer>());
  rclcpp::shutdown();
  return 0;
}
