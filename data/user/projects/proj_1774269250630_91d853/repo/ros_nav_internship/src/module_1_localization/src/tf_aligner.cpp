/**
 * @file tf_aligner.cpp
 * @brief Implementation of TFAligner.
 *
 * Broadcasts static TF transforms for the laser and IMU frames relative
 * to base_link.  Supports runtime re-calibration via dynamic parameters:
 * any parameter change marks the transforms as dirty, and the periodic
 * timer re-broadcasts them.
 */

#include "module_1_localization/tf_aligner.hpp"

#include <tf2/LinearMath/Quaternion.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.hpp>

#include <chrono>
#include <string>

using namespace std::chrono_literals;
using rcl_interfaces::msg::SetParametersResult;

namespace module_1_localization {

// ============================================================
// Constructor
// ============================================================

TFAligner::TFAligner(const rclcpp::NodeOptions & options)
: rclcpp::Node("tf_aligner", options)
{
  declareParameters();

  // Read frame names
  parent_frame_  = get_parameter("parent_frame").as_string();
  laser_frame_   = get_parameter("laser_frame").as_string();
  imu_frame_     = get_parameter("imu_frame").as_string();
  broadcast_rate_ = get_parameter("broadcast_rate").as_double();

  // Read laser extrinsics
  laser_x_     = get_parameter("laser_x").as_double();
  laser_y_     = get_parameter("laser_y").as_double();
  laser_z_     = get_parameter("laser_z").as_double();
  laser_roll_  = get_parameter("laser_roll").as_double();
  laser_pitch_ = get_parameter("laser_pitch").as_double();
  laser_yaw_   = get_parameter("laser_yaw").as_double();

  // Read IMU extrinsics
  imu_x_     = get_parameter("imu_x").as_double();
  imu_y_     = get_parameter("imu_y").as_double();
  imu_z_     = get_parameter("imu_z").as_double();
  imu_roll_  = get_parameter("imu_roll").as_double();
  imu_pitch_ = get_parameter("imu_pitch").as_double();
  imu_yaw_   = get_parameter("imu_yaw").as_double();

  RCLCPP_INFO(get_logger(),
    "TFAligner: parent='%s' laser='%s' imu='%s' rate=%.1f Hz",
    parent_frame_.c_str(), laser_frame_.c_str(),
    imu_frame_.c_str(), broadcast_rate_);

  // Create broadcasters
  static_broadcaster_  = std::make_shared<tf2_ros::StaticTransformBroadcaster>(this);
  dynamic_broadcaster_ = std::make_shared<tf2_ros::TransformBroadcaster>(this);

  // Register dynamic parameter callback
  param_cb_handle_ = add_on_set_parameters_callback(
    std::bind(&TFAligner::onParamChange, this, std::placeholders::_1));

  // Broadcast initial static transforms
  broadcastAll();

  // Start periodic timer for re-broadcasting on param changes
  const auto period_ms = static_cast<int>(1000.0 / broadcast_rate_);
  broadcast_timer_ = create_wall_timer(
    std::chrono::milliseconds(period_ms),
    std::bind(&TFAligner::timerCallback, this));
}

// ============================================================
// declareParameters
// ============================================================

void TFAligner::declareParameters()
{
  declare_parameter<std::string>("parent_frame",   "base_link");
  declare_parameter<std::string>("laser_frame",    "laser");
  declare_parameter<std::string>("imu_frame",      "imu_link");
  declare_parameter<double>     ("broadcast_rate", 1.0);

  // Laser extrinsics
  declare_parameter<double>("laser_x",     0.0);
  declare_parameter<double>("laser_y",     0.0);
  declare_parameter<double>("laser_z",     0.18);
  declare_parameter<double>("laser_roll",  0.0);
  declare_parameter<double>("laser_pitch", 0.0);
  declare_parameter<double>("laser_yaw",   0.0);

  // IMU extrinsics
  declare_parameter<double>("imu_x",     0.0);
  declare_parameter<double>("imu_y",     0.0);
  declare_parameter<double>("imu_z",     0.1);
  declare_parameter<double>("imu_roll",  0.0);
  declare_parameter<double>("imu_pitch", 0.0);
  declare_parameter<double>("imu_yaw",   0.0);
}

// ============================================================
// buildTransform  (static helper)
// ============================================================

geometry_msgs::msg::TransformStamped TFAligner::buildTransform(
  const std::string & parent_frame,
  const std::string & child_frame,
  double x, double y, double z,
  double roll, double pitch, double yaw,
  const rclcpp::Time & stamp)
{
  geometry_msgs::msg::TransformStamped ts;
  ts.header.stamp    = stamp;
  ts.header.frame_id = parent_frame;
  ts.child_frame_id  = child_frame;

  ts.transform.translation.x = x;
  ts.transform.translation.y = y;
  ts.transform.translation.z = z;

  tf2::Quaternion q;
  q.setRPY(roll, pitch, yaw);
  q.normalize();

  ts.transform.rotation.x = q.x();
  ts.transform.rotation.y = q.y();
  ts.transform.rotation.z = q.z();
  ts.transform.rotation.w = q.w();

  return ts;
}

// ============================================================
// broadcastLaserTransform
// ============================================================

void TFAligner::broadcastLaserTransform()
{
  const auto ts = buildTransform(
    parent_frame_, laser_frame_,
    laser_x_, laser_y_, laser_z_,
    laser_roll_, laser_pitch_, laser_yaw_,
    now());

  static_broadcaster_->sendTransform(ts);

  RCLCPP_DEBUG(get_logger(),
    "TFAligner: broadcast %s->%s [%.3f, %.3f, %.3f] rpy[%.3f, %.3f, %.3f]",
    parent_frame_.c_str(), laser_frame_.c_str(),
    laser_x_, laser_y_, laser_z_,
    laser_roll_, laser_pitch_, laser_yaw_);
}

// ============================================================
// broadcastImuTransform
// ============================================================

void TFAligner::broadcastImuTransform()
{
  const auto ts = buildTransform(
    parent_frame_, imu_frame_,
    imu_x_, imu_y_, imu_z_,
    imu_roll_, imu_pitch_, imu_yaw_,
    now());

  static_broadcaster_->sendTransform(ts);

  RCLCPP_DEBUG(get_logger(),
    "TFAligner: broadcast %s->%s [%.3f, %.3f, %.3f] rpy[%.3f, %.3f, %.3f]",
    parent_frame_.c_str(), imu_frame_.c_str(),
    imu_x_, imu_y_, imu_z_,
    imu_roll_, imu_pitch_, imu_yaw_);
}

// ============================================================
// broadcastAll
// ============================================================

void TFAligner::broadcastAll()
{
  broadcastLaserTransform();
  broadcastImuTransform();
  params_dirty_ = false;
}

// ============================================================
// timerCallback
// ============================================================

void TFAligner::timerCallback()
{
  if (params_dirty_) {
    RCLCPP_INFO(get_logger(),
      "TFAligner: re-broadcasting transforms after parameter change.");
    broadcastAll();
  }
}

// ============================================================
// onParamChange
// ============================================================

SetParametersResult TFAligner::onParamChange(
  const std::vector<rclcpp::Parameter> & params)
{
  SetParametersResult result;
  result.successful = true;

  for (const auto & p : params) {
    const std::string & name = p.get_name();

    if (name == "laser_x")      { laser_x_     = p.as_double(); params_dirty_ = true; }
    else if (name == "laser_y")      { laser_y_     = p.as_double(); params_dirty_ = true; }
    else if (name == "laser_z")      { laser_z_     = p.as_double(); params_dirty_ = true; }
    else if (name == "laser_roll")   { laser_roll_  = p.as_double(); params_dirty_ = true; }
    else if (name == "laser_pitch")  { laser_pitch_ = p.as_double(); params_dirty_ = true; }
    else if (name == "laser_yaw")    { laser_yaw_   = p.as_double(); params_dirty_ = true; }
    else if (name == "imu_x")        { imu_x_       = p.as_double(); params_dirty_ = true; }
    else if (name == "imu_y")        { imu_y_       = p.as_double(); params_dirty_ = true; }
    else if (name == "imu_z")        { imu_z_       = p.as_double(); params_dirty_ = true; }
    else if (name == "imu_roll")     { imu_roll_    = p.as_double(); params_dirty_ = true; }
    else if (name == "imu_pitch")    { imu_pitch_   = p.as_double(); params_dirty_ = true; }
    else if (name == "imu_yaw")      { imu_yaw_     = p.as_double(); params_dirty_ = true; }
    else if (name == "parent_frame") { parent_frame_= p.as_string(); params_dirty_ = true; }
    else if (name == "laser_frame")  { laser_frame_ = p.as_string(); params_dirty_ = true; }
    else if (name == "imu_frame")    { imu_frame_   = p.as_string(); params_dirty_ = true; }
    // broadcast_rate change not handled at runtime (would require timer recreation)
  }

  if (params_dirty_) {
    RCLCPP_INFO(get_logger(),
      "TFAligner: %zu parameter(s) changed – will re-broadcast on next timer tick.",
      params.size());
  }

  return result;
}

}  // namespace module_1_localization

// ============================================================
// main
// ============================================================
#include <rclcpp/rclcpp.hpp>

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<module_1_localization::TFAligner>());
  rclcpp::shutdown();
  return 0;
}
