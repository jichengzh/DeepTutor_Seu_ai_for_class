#pragma once

/**
 * @file localization_lifecycle.hpp
 * @brief Full LifecycleNode managing the localization stack.
 *
 * Extends nav2_util::LifecycleNode to manage the lifecycle of the
 * localization subsystem: sensor synchronization, TF alignment,
 * EKF pose estimation, and diagnostic monitoring.
 *
 * State machine:
 *   Unconfigured -> (on_configure) -> Inactive
 *   Inactive     -> (on_activate)  -> Active
 *   Active       -> (on_deactivate)-> Inactive
 *   Inactive     -> (on_cleanup)   -> Unconfigured
 *   Any          -> (on_shutdown)  -> Finalized
 */

#include <memory>
#include <string>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <nav2_util/lifecycle_node.hpp>
#include <geometry_msgs/msg/pose_with_covariance_stamped.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <sensor_msgs/msg/laser_scan.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <diagnostic_updater/diagnostic_updater.hpp>

namespace module_1_localization {

/**
 * @class LocalizationLifecycleNode
 * @brief Manages the full localization pipeline as a Nav2 LifecycleNode.
 *
 * Responsibilities:
 *  - Load and validate all parameters on configure.
 *  - Start/stop data flow on activate/deactivate.
 *  - Release all resources on cleanup.
 *  - Publish /localization_pose and diagnostics.
 */
class LocalizationLifecycleNode : public nav2_util::LifecycleNode {
public:
  /**
   * @brief Construct the lifecycle node.
   * @param options  ROS2 node options (used for composition).
   */
  explicit LocalizationLifecycleNode(
    const rclcpp::NodeOptions & options = rclcpp::NodeOptions{});

  /// Destructor – ensures cleanup even on abnormal shutdown.
  ~LocalizationLifecycleNode() override;

protected:
  // ----------------------------------------------------------------
  // LifecycleNode state-machine callbacks
  // ----------------------------------------------------------------

  /**
   * @brief Configure: load params, create publishers/subscribers, allocate
   *        internal data structures.  Does NOT start processing data.
   */
  nav2_util::CallbackReturn on_configure(
    const rclcpp_lifecycle::State & state) override;

  /**
   * @brief Activate: activate lifecycle publishers, start the processing
   *        timer, begin accepting sensor data.
   */
  nav2_util::CallbackReturn on_activate(
    const rclcpp_lifecycle::State & state) override;

  /**
   * @brief Deactivate: stop processing timer, deactivate publishers.
   *        Subscriptions remain so state is preserved.
   */
  nav2_util::CallbackReturn on_deactivate(
    const rclcpp_lifecycle::State & state) override;

  /**
   * @brief Cleanup: release all resources (publishers, subscriptions,
   *        timers, allocated memory).  Returns to Unconfigured state.
   */
  nav2_util::CallbackReturn on_cleanup(
    const rclcpp_lifecycle::State & state) override;

  /**
   * @brief Shutdown: perform a best-effort cleanup of all resources.
   *        Called on node destruction or external shutdown request.
   */
  nav2_util::CallbackReturn on_shutdown(
    const rclcpp_lifecycle::State & state) override;

private:
  // ----------------------------------------------------------------
  // Internal helpers
  // ----------------------------------------------------------------
  void declareParameters();
  void loadParameters();
  void createPublishers();
  void createSubscriptions();
  void releaseResources();

  /// Main processing timer callback (runs at update_rate_ Hz).
  void processingTimerCallback();

  /// Sensor callbacks (store latest data, thread-safe).
  void scanCallback(const sensor_msgs::msg::LaserScan::ConstSharedPtr & msg);
  void imuCallback(const sensor_msgs::msg::Imu::ConstSharedPtr & msg);
  void odomCallback(const nav_msgs::msg::Odometry::ConstSharedPtr & msg);

  /// Publish current pose estimate.
  void publishPose();

  /// Diagnostics updater callback.
  void diagnosticsCallback(diagnostic_updater::DiagnosticStatusWrapper & stat);

  // ----------------------------------------------------------------
  // Publishers (lifecycle-managed)
  // ----------------------------------------------------------------
  rclcpp_lifecycle::LifecyclePublisher<
    geometry_msgs::msg::PoseWithCovarianceStamped>::SharedPtr pose_pub_;
  rclcpp_lifecycle::LifecyclePublisher<
    nav_msgs::msg::Odometry>::SharedPtr filtered_odom_pub_;

  // ----------------------------------------------------------------
  // Subscriptions
  // ----------------------------------------------------------------
  rclcpp::Subscription<sensor_msgs::msg::LaserScan>::SharedPtr scan_sub_;
  rclcpp::Subscription<sensor_msgs::msg::Imu>::SharedPtr       imu_sub_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr     odom_sub_;

  // ----------------------------------------------------------------
  // Timer
  // ----------------------------------------------------------------
  rclcpp::TimerBase::SharedPtr processing_timer_;

  // ----------------------------------------------------------------
  // Diagnostics
  // ----------------------------------------------------------------
  std::shared_ptr<diagnostic_updater::Updater> diagnostic_updater_;

  // ----------------------------------------------------------------
  // Latest sensor data (protected by mutex)
  // ----------------------------------------------------------------
  mutable std::mutex data_mutex_;
  sensor_msgs::msg::LaserScan::ConstSharedPtr latest_scan_;
  sensor_msgs::msg::Imu::ConstSharedPtr       latest_imu_;
  nav_msgs::msg::Odometry::ConstSharedPtr     latest_odom_;

  // ----------------------------------------------------------------
  // Current pose estimate
  // ----------------------------------------------------------------
  geometry_msgs::msg::PoseWithCovarianceStamped current_pose_;

  // ----------------------------------------------------------------
  // Parameters
  // ----------------------------------------------------------------
  std::string global_frame_{"map"};
  std::string odom_frame_{"odom"};
  std::string base_frame_{"base_footprint"};
  std::string scan_topic_{"scan"};
  std::string imu_topic_{"imu/data"};
  std::string odom_topic_{"odom"};
  double      update_rate_{10.0};   ///< Hz
  bool        use_sim_time_{false};

  // ----------------------------------------------------------------
  // State flags
  // ----------------------------------------------------------------
  bool is_active_{false};
  uint64_t update_count_{0};
};

}  // namespace module_1_localization
