#pragma once

/**
 * @file tf_aligner.hpp
 * @brief Broadcasts static TF transforms for sensor frames relative to
 *        base_link, with support for runtime calibration via dynamic
 *        parameters.
 */

#include <memory>
#include <string>

#include <rclcpp/rclcpp.hpp>
#include <tf2_ros/static_transform_broadcaster.h>
#include <tf2_ros/transform_broadcaster.h>
#include <geometry_msgs/msg/transform_stamped.hpp>

namespace module_1_localization {

/**
 * @class TFAligner
 * @brief Publishes static/dynamic TF transforms for laser and IMU frames.
 *
 * Parameters (all double, in metres / radians):
 *   - laser_x, laser_y, laser_z, laser_roll, laser_pitch, laser_yaw
 *   - imu_x,   imu_y,   imu_z,   imu_roll,   imu_pitch,   imu_yaw
 *   - parent_frame   (default: "base_link")
 *   - laser_frame    (default: "laser")
 *   - imu_frame      (default: "imu_link")
 *   - broadcast_rate (default: 1.0 Hz, used for re-broadcast on param change)
 */
class TFAligner : public rclcpp::Node {
public:
  /**
   * @brief Construct the TFAligner node, read parameters, and broadcast
   *        initial static transforms.
   */
  explicit TFAligner(const rclcpp::NodeOptions & options = rclcpp::NodeOptions{});

  /// Default destructor.
  ~TFAligner() override = default;

private:
  // ----------------------------------------------------------------
  // Initialisation helpers
  // ----------------------------------------------------------------
  /// Declare all parameters with default values.
  void declareParameters();

  /// Build and broadcast the laser frame transform.
  void broadcastLaserTransform();

  /// Build and broadcast the IMU frame transform.
  void broadcastImuTransform();

  /// Broadcast both transforms. Called on startup and periodically.
  void broadcastAll();

  // ----------------------------------------------------------------
  // Callbacks
  // ----------------------------------------------------------------
  /// Periodic timer callback – re-broadcasts if parameters changed.
  void timerCallback();

  /// Dynamic parameter change callback for runtime calibration.
  rcl_interfaces::msg::SetParametersResult onParamChange(
    const std::vector<rclcpp::Parameter> & params);

  // ----------------------------------------------------------------
  // Helper: build a TransformStamped from xyz + rpy
  // ----------------------------------------------------------------
  static geometry_msgs::msg::TransformStamped buildTransform(
    const std::string & parent_frame,
    const std::string & child_frame,
    double x, double y, double z,
    double roll, double pitch, double yaw,
    const rclcpp::Time & stamp);

  // ----------------------------------------------------------------
  // TF broadcasters
  // ----------------------------------------------------------------
  std::shared_ptr<tf2_ros::StaticTransformBroadcaster> static_broadcaster_;
  std::shared_ptr<tf2_ros::TransformBroadcaster>       dynamic_broadcaster_;

  // ----------------------------------------------------------------
  // Timer
  // ----------------------------------------------------------------
  rclcpp::TimerBase::SharedPtr broadcast_timer_;

  // ----------------------------------------------------------------
  // Parameter change handler
  // ----------------------------------------------------------------
  rclcpp::node_interfaces::OnSetParametersCallbackHandle::SharedPtr param_cb_handle_;

  // ----------------------------------------------------------------
  // Frame names
  // ----------------------------------------------------------------
  std::string parent_frame_{"base_link"};
  std::string laser_frame_{"laser"};
  std::string imu_frame_{"imu_link"};

  // ----------------------------------------------------------------
  // Laser extrinsics
  // ----------------------------------------------------------------
  double laser_x_{0.0}, laser_y_{0.0}, laser_z_{0.18};
  double laser_roll_{0.0}, laser_pitch_{0.0}, laser_yaw_{0.0};

  // ----------------------------------------------------------------
  // IMU extrinsics
  // ----------------------------------------------------------------
  double imu_x_{0.0}, imu_y_{0.0}, imu_z_{0.1};
  double imu_roll_{0.0}, imu_pitch_{0.0}, imu_yaw_{0.0};

  // ----------------------------------------------------------------
  // Broadcast rate
  // ----------------------------------------------------------------
  double broadcast_rate_{1.0};   ///< Hz

  /// Set to true by onParamChange so timerCallback re-broadcasts.
  bool params_dirty_{false};
};

}  // namespace module_1_localization
