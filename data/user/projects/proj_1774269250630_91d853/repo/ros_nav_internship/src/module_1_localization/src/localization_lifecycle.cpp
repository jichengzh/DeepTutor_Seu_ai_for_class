/**
 * @file localization_lifecycle.cpp
 * @brief Implementation of LocalizationLifecycleNode.
 *
 * Full Nav2 LifecycleNode state machine:
 *   Unconfigured -> configure -> Inactive
 *   Inactive     -> activate  -> Active
 *   Active       -> deactivate-> Inactive
 *   Inactive     -> cleanup   -> Unconfigured
 *   Any          -> shutdown  -> Finalized
 */

#include "module_1_localization/localization_lifecycle.hpp"

#include <chrono>

using namespace std::chrono_literals;
using nav2_util::CallbackReturn;

namespace module_1_localization {

// ============================================================
// Constructor / Destructor
// ============================================================

LocalizationLifecycleNode::LocalizationLifecycleNode(
  const rclcpp::NodeOptions & options)
: nav2_util::LifecycleNode("localization_lifecycle", "", options)
{
  RCLCPP_INFO(get_logger(), "LocalizationLifecycleNode: constructing.");
  declareParameters();
}

LocalizationLifecycleNode::~LocalizationLifecycleNode()
{
  RCLCPP_INFO(get_logger(), "LocalizationLifecycleNode: destructing.");
  releaseResources();
}

// ============================================================
// declareParameters / loadParameters
// ============================================================

void LocalizationLifecycleNode::declareParameters()
{
  declare_parameter<std::string>("global_frame",  "map");
  declare_parameter<std::string>("odom_frame",    "odom");
  declare_parameter<std::string>("base_frame",    "base_footprint");
  declare_parameter<std::string>("scan_topic",    "scan");
  declare_parameter<std::string>("imu_topic",     "imu/data");
  declare_parameter<std::string>("odom_topic",    "odom");
  declare_parameter<double>     ("update_rate",   10.0);
  declare_parameter<bool>       ("use_sim_time",  false);
}

void LocalizationLifecycleNode::loadParameters()
{
  global_frame_ = get_parameter("global_frame").as_string();
  odom_frame_   = get_parameter("odom_frame").as_string();
  base_frame_   = get_parameter("base_frame").as_string();
  scan_topic_   = get_parameter("scan_topic").as_string();
  imu_topic_    = get_parameter("imu_topic").as_string();
  odom_topic_   = get_parameter("odom_topic").as_string();
  update_rate_  = get_parameter("update_rate").as_double();
  use_sim_time_ = get_parameter("use_sim_time").as_bool();

  RCLCPP_INFO(get_logger(),
    "LocalizationLifecycleNode: params loaded – global_frame='%s' "
    "odom_frame='%s' base_frame='%s' rate=%.1f Hz",
    global_frame_.c_str(), odom_frame_.c_str(),
    base_frame_.c_str(), update_rate_);
}

// ============================================================
// createPublishers
// ============================================================

void LocalizationLifecycleNode::createPublishers()
{
  pose_pub_ = create_publisher<geometry_msgs::msg::PoseWithCovarianceStamped>(
    "localization_pose", rclcpp::QoS(10));

  filtered_odom_pub_ = create_publisher<nav_msgs::msg::Odometry>(
    "filtered_odom", rclcpp::QoS(10));
}

// ============================================================
// createSubscriptions
// ============================================================

void LocalizationLifecycleNode::createSubscriptions()
{
  scan_sub_ = create_subscription<sensor_msgs::msg::LaserScan>(
    scan_topic_, rclcpp::SensorDataQoS(),
    std::bind(&LocalizationLifecycleNode::scanCallback, this,
              std::placeholders::_1));

  imu_sub_ = create_subscription<sensor_msgs::msg::Imu>(
    imu_topic_, rclcpp::SensorDataQoS(),
    std::bind(&LocalizationLifecycleNode::imuCallback, this,
              std::placeholders::_1));

  odom_sub_ = create_subscription<nav_msgs::msg::Odometry>(
    odom_topic_, rclcpp::SensorDataQoS(),
    std::bind(&LocalizationLifecycleNode::odomCallback, this,
              std::placeholders::_1));
}

// ============================================================
// releaseResources
// ============================================================

void LocalizationLifecycleNode::releaseResources()
{
  processing_timer_.reset();
  scan_sub_.reset();
  imu_sub_.reset();
  odom_sub_.reset();
  pose_pub_.reset();
  filtered_odom_pub_.reset();
  diagnostic_updater_.reset();

  {
    std::lock_guard<std::mutex> lock(data_mutex_);
    latest_scan_.reset();
    latest_imu_.reset();
    latest_odom_.reset();
  }
}

// ============================================================
// on_configure
// ============================================================

CallbackReturn LocalizationLifecycleNode::on_configure(
  const rclcpp_lifecycle::State & /*state*/)
{
  RCLCPP_INFO(get_logger(), "LocalizationLifecycleNode: configuring.");

  try {
    loadParameters();
    createPublishers();
    createSubscriptions();

    // Initialise diagnostics updater
    diagnostic_updater_ = std::make_shared<diagnostic_updater::Updater>(this);
    diagnostic_updater_->setHardwareID("localization_lifecycle");
    diagnostic_updater_->add(
      "Localization Status",
      std::bind(&LocalizationLifecycleNode::diagnosticsCallback, this,
                std::placeholders::_1));

    // Initialise current_pose_ with identity
    current_pose_.header.frame_id = global_frame_;
    current_pose_.pose.pose.orientation.w = 1.0;

  } catch (const std::exception & e) {
    RCLCPP_ERROR(get_logger(),
      "LocalizationLifecycleNode: configure failed: %s", e.what());
    return CallbackReturn::FAILURE;
  }

  RCLCPP_INFO(get_logger(), "LocalizationLifecycleNode: configured successfully.");
  return CallbackReturn::SUCCESS;
}

// ============================================================
// on_activate
// ============================================================

CallbackReturn LocalizationLifecycleNode::on_activate(
  const rclcpp_lifecycle::State & /*state*/)
{
  RCLCPP_INFO(get_logger(), "LocalizationLifecycleNode: activating.");

  // Activate lifecycle publishers
  pose_pub_->on_activate();
  filtered_odom_pub_->on_activate();

  // Start processing timer
  const auto period_ms = static_cast<int>(1000.0 / update_rate_);
  processing_timer_ = create_wall_timer(
    std::chrono::milliseconds(period_ms),
    std::bind(&LocalizationLifecycleNode::processingTimerCallback, this));

  is_active_ = true;
  update_count_ = 0;

  RCLCPP_INFO(get_logger(),
    "LocalizationLifecycleNode: active at %.1f Hz.", update_rate_);

  // Activate bond with lifecycle manager if available
  createBond();

  return CallbackReturn::SUCCESS;
}

// ============================================================
// on_deactivate
// ============================================================

CallbackReturn LocalizationLifecycleNode::on_deactivate(
  const rclcpp_lifecycle::State & /*state*/)
{
  RCLCPP_INFO(get_logger(), "LocalizationLifecycleNode: deactivating.");

  is_active_ = false;

  // Stop processing timer
  if (processing_timer_) {
    processing_timer_->cancel();
    processing_timer_.reset();
  }

  // Deactivate lifecycle publishers
  pose_pub_->on_deactivate();
  filtered_odom_pub_->on_deactivate();

  // Destroy bond
  destroyBond();

  RCLCPP_INFO(get_logger(), "LocalizationLifecycleNode: deactivated.");
  return CallbackReturn::SUCCESS;
}

// ============================================================
// on_cleanup
// ============================================================

CallbackReturn LocalizationLifecycleNode::on_cleanup(
  const rclcpp_lifecycle::State & /*state*/)
{
  RCLCPP_INFO(get_logger(), "LocalizationLifecycleNode: cleaning up.");

  releaseResources();

  RCLCPP_INFO(get_logger(), "LocalizationLifecycleNode: cleaned up.");
  return CallbackReturn::SUCCESS;
}

// ============================================================
// on_shutdown
// ============================================================

CallbackReturn LocalizationLifecycleNode::on_shutdown(
  const rclcpp_lifecycle::State & /*state*/)
{
  RCLCPP_INFO(get_logger(), "LocalizationLifecycleNode: shutting down.");

  is_active_ = false;
  releaseResources();

  return CallbackReturn::SUCCESS;
}

// ============================================================
// Sensor callbacks
// ============================================================

void LocalizationLifecycleNode::scanCallback(
  const sensor_msgs::msg::LaserScan::ConstSharedPtr & msg)
{
  std::lock_guard<std::mutex> lock(data_mutex_);
  latest_scan_ = msg;
}

void LocalizationLifecycleNode::imuCallback(
  const sensor_msgs::msg::Imu::ConstSharedPtr & msg)
{
  std::lock_guard<std::mutex> lock(data_mutex_);
  latest_imu_ = msg;
}

void LocalizationLifecycleNode::odomCallback(
  const nav_msgs::msg::Odometry::ConstSharedPtr & msg)
{
  std::lock_guard<std::mutex> lock(data_mutex_);
  latest_odom_ = msg;
}

// ============================================================
// processingTimerCallback
// ============================================================

void LocalizationLifecycleNode::processingTimerCallback()
{
  if (!is_active_) { return; }

  sensor_msgs::msg::LaserScan::ConstSharedPtr  scan;
  sensor_msgs::msg::Imu::ConstSharedPtr        imu;
  nav_msgs::msg::Odometry::ConstSharedPtr      odom;

  {
    std::lock_guard<std::mutex> lock(data_mutex_);
    scan = latest_scan_;
    imu  = latest_imu_;
    odom = latest_odom_;
  }

  if (!scan || !imu || !odom) {
    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
      "LocalizationLifecycleNode: waiting for sensor data "
      "(scan=%s, imu=%s, odom=%s).",
      scan ? "OK" : "MISSING",
      imu  ? "OK" : "MISSING",
      odom ? "OK" : "MISSING");
    return;
  }

  // Update pose timestamp and publish
  current_pose_.header.stamp    = now();
  current_pose_.header.frame_id = global_frame_;
  publishPose();

  ++update_count_;
}

// ============================================================
// publishPose
// ============================================================

void LocalizationLifecycleNode::publishPose()
{
  if (pose_pub_ && pose_pub_->is_activated()) {
    pose_pub_->publish(current_pose_);
  }
}

// ============================================================
// diagnosticsCallback
// ============================================================

void LocalizationLifecycleNode::diagnosticsCallback(
  diagnostic_updater::DiagnosticStatusWrapper & stat)
{
  if (is_active_) {
    stat.summary(diagnostic_msgs::msg::DiagnosticStatus::OK,
      "Localization running");
  } else {
    stat.summary(diagnostic_msgs::msg::DiagnosticStatus::WARN,
      "Localization not active");
  }

  stat.add("Update count",  update_count_);
  stat.add("Update rate Hz", update_rate_);
  stat.add("Global frame",  global_frame_);
  stat.add("Odom frame",    odom_frame_);
  stat.add("Base frame",    base_frame_);
}

}  // namespace module_1_localization

// ============================================================
// main
// ============================================================
#include <rclcpp/rclcpp.hpp>

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<module_1_localization::LocalizationLifecycleNode>();
  rclcpp::spin(node->get_node_base_interface());
  rclcpp::shutdown();
  return 0;
}
