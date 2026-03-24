/**
 * safety_boundary_checker.cpp
 *
 * Monitors velocity and obstacle distance against ASIL-B safety thresholds.
 * Issues emergency stops and publishes diagnostic status when violations occur.
 */
#include "module_6_testing/safety_boundary_checker.hpp"

#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <diagnostic_msgs/msg/diagnostic_array.hpp>
#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <diagnostic_msgs/msg/key_value.hpp>
#include <visualization_msgs/msg/marker_array.hpp>

#include <cmath>
#include <chrono>
#include <sstream>
#include <limits>

using namespace std::chrono_literals;

namespace module_6_testing {

// ──────────────────────────────────────────────────────────────────────────────
// Helper: convert AsilLevel to string
// ──────────────────────────────────────────────────────────────────────────────
static std::string asilToString(AsilLevel level)
{
  switch (level) {
    case AsilLevel::NONE:   return "NONE";
    case AsilLevel::QM:     return "QM";
    case AsilLevel::ASIL_A: return "ASIL-A";
    case AsilLevel::ASIL_B: return "ASIL-B";
    case AsilLevel::ASIL_C: return "ASIL-C";
    case AsilLevel::ASIL_D: return "ASIL-D";
    default:                return "UNKNOWN";
  }
}

// ──────────────────────────────────────────────────────────────────────────────
// Constructor
// ──────────────────────────────────────────────────────────────────────────────
SafetyBoundaryChecker::SafetyBoundaryChecker(const rclcpp::NodeOptions& options)
: rclcpp::Node("safety_boundary_checker", options)
{
  // Parameters
  this->declare_parameter<double>("min_distance_asil", 0.5);
  this->declare_parameter<double>("max_velocity_asil", 1.5);
  this->declare_parameter<bool>  ("emergency_stop_enabled", true);
  this->declare_parameter<std::string>("asil_level", "ASIL_B");

  min_distance_asil_     = this->get_parameter("min_distance_asil").as_double();
  max_velocity_asil_     = this->get_parameter("max_velocity_asil").as_double();
  emergency_stop_enabled_= this->get_parameter("emergency_stop_enabled").as_bool();

  {
    const auto level_str = this->get_parameter("asil_level").as_string();
    if      (level_str == "QM")     asil_level_ = AsilLevel::QM;
    else if (level_str == "ASIL_A") asil_level_ = AsilLevel::ASIL_A;
    else if (level_str == "ASIL_B") asil_level_ = AsilLevel::ASIL_B;
    else if (level_str == "ASIL_C") asil_level_ = AsilLevel::ASIL_C;
    else if (level_str == "ASIL_D") asil_level_ = AsilLevel::ASIL_D;
    else                            asil_level_ = AsilLevel::ASIL_B;
  }

  // ── Subscribers ──────────────────────────────────────────────────────────
  vel_sub_ = this->create_subscription<geometry_msgs::msg::Twist>(
      "/cmd_vel", rclcpp::QoS(10),
      std::bind(&SafetyBoundaryChecker::velocityCallback,
                this, std::placeholders::_1));

  obstacle_sub_ = this->create_subscription<visualization_msgs::msg::MarkerArray>(
      "/obstacles", rclcpp::QoS(10),
      std::bind(&SafetyBoundaryChecker::obstacleCallback,
                this, std::placeholders::_1));

  // ── Publishers ────────────────────────────────────────────────────────────
  diag_pub_ = this->create_publisher<diagnostic_msgs::msg::DiagnosticArray>(
      "/diagnostics", rclcpp::QoS(10));

  emergency_stop_pub_ = this->create_publisher<geometry_msgs::msg::Twist>(
      "/cmd_vel_safe", rclcpp::QoS(10));

  // ── Diagnostics timer ────────────────────────────────────────────────────
  diag_timer_ = this->create_wall_timer(
      1s, std::bind(&SafetyBoundaryChecker::publishDiagnostics, this));

  RCLCPP_INFO(get_logger(),
              "SafetyBoundaryChecker started [%s] "
              "min_dist=%.2f m  max_vel=%.2f m/s  e-stop=%s",
              asilToString(asil_level_).c_str(),
              min_distance_asil_,
              max_velocity_asil_,
              emergency_stop_enabled_ ? "enabled" : "disabled");
}

// ──────────────────────────────────────────────────────────────────────────────
// logAsilEvent
// ──────────────────────────────────────────────────────────────────────────────
void SafetyBoundaryChecker::logAsilEvent(AsilLevel level,
                                          const std::string& desc,
                                          double actual,
                                          double threshold,
                                          const std::string& param)
{
  AsilEvent event;
  event.timestamp       = std::chrono::system_clock::now();
  event.level           = level;
  event.description     = desc;
  event.actual_value    = actual;
  event.threshold_value = threshold;
  event.parameter_name  = param;
  violation_log_.push_back(event);

  RCLCPP_WARN(get_logger(),
              "[%s] %s | %s=%.3f (threshold=%.3f)",
              asilToString(level).c_str(),
              desc.c_str(),
              param.c_str(),
              actual,
              threshold);
}

// ──────────────────────────────────────────────────────────────────────────────
// checkMaxVelocity
// ──────────────────────────────────────────────────────────────────────────────
void SafetyBoundaryChecker::checkMaxVelocity(double actual_vel,
                                               double max_vel_asil)
{
  if (actual_vel > max_vel_asil) {
    const std::string desc = "Velocity exceeds ASIL safety threshold";
    logAsilEvent(asil_level_, desc, actual_vel, max_vel_asil, "linear_velocity");

    if (emergency_stop_enabled_) {
      // Publish zero-velocity emergency stop
      geometry_msgs::msg::Twist stop;
      stop.linear.x  = 0.0;
      stop.linear.y  = 0.0;
      stop.linear.z  = 0.0;
      stop.angular.x = 0.0;
      stop.angular.y = 0.0;
      stop.angular.z = 0.0;
      emergency_stop_pub_->publish(stop);
      RCLCPP_ERROR(get_logger(),
                   "EMERGENCY STOP issued: velocity %.3f > %.3f m/s",
                   actual_vel, max_vel_asil);
    }
  }
}

// ──────────────────────────────────────────────────────────────────────────────
// checkMinDistance
// ──────────────────────────────────────────────────────────────────────────────
void SafetyBoundaryChecker::checkMinDistance(double actual_dist,
                                               double min_dist_asil)
{
  if (actual_dist < min_dist_asil) {
    const std::string desc = "Obstacle distance below ASIL safety threshold";
    logAsilEvent(asil_level_, desc, actual_dist, min_dist_asil,
                 "obstacle_distance");

    if (emergency_stop_enabled_) {
      geometry_msgs::msg::Twist stop;
      emergency_stop_pub_->publish(stop);
      RCLCPP_ERROR(get_logger(),
                   "EMERGENCY STOP issued: distance %.3f < %.3f m",
                   actual_dist, min_dist_asil);
    }
  }
}

// ──────────────────────────────────────────────────────────────────────────────
// velocityCallback
// ──────────────────────────────────────────────────────────────────────────────
void SafetyBoundaryChecker::velocityCallback(
    const geometry_msgs::msg::Twist::SharedPtr msg)
{
  // Compute linear speed magnitude
  const double speed = std::hypot(msg->linear.x, msg->linear.y);
  checkMaxVelocity(speed, max_velocity_asil_);
}

// ──────────────────────────────────────────────────────────────────────────────
// obstacleCallback  —  find nearest obstacle in the MarkerArray
// ──────────────────────────────────────────────────────────────────────────────
void SafetyBoundaryChecker::obstacleCallback(
    const visualization_msgs::msg::MarkerArray::SharedPtr msg)
{
  double min_dist = std::numeric_limits<double>::infinity();

  for (const auto& marker : msg->markers) {
    // Assume obstacles are expressed in the robot's base frame; use XY distance
    const double dist = std::hypot(marker.pose.position.x,
                                   marker.pose.position.y);
    if (dist < min_dist) {
      min_dist = dist;
    }
  }

  if (std::isfinite(min_dist)) {
    checkMinDistance(min_dist, min_distance_asil_);
  }
}

// ──────────────────────────────────────────────────────────────────────────────
// publishDiagnostics
// ──────────────────────────────────────────────────────────────────────────────
void SafetyBoundaryChecker::publishDiagnostics()
{
  diagnostic_msgs::msg::DiagnosticArray diag_array;
  diag_array.header.stamp = this->now();

  // ── Overall safety status ────────────────────────────────────────────────
  diagnostic_msgs::msg::DiagnosticStatus status;
  status.name      = "safety_boundary_checker/" + asilToString(asil_level_);
  status.hardware_id = "navigation_stack";

  if (violation_log_.empty()) {
    status.level   = diagnostic_msgs::msg::DiagnosticStatus::OK;
    status.message = "All safety boundaries nominal";
  } else {
    status.level   = diagnostic_msgs::msg::DiagnosticStatus::ERROR;
    status.message = "ASIL violation(s) detected: " +
                     std::to_string(violation_log_.size()) + " event(s)";
  }

  // Add key-value pairs
  {
    diagnostic_msgs::msg::KeyValue kv;
    kv.key   = "asil_level";
    kv.value = asilToString(asil_level_);
    status.values.push_back(kv);
  }
  {
    diagnostic_msgs::msg::KeyValue kv;
    kv.key   = "violation_count";
    kv.value = std::to_string(violation_log_.size());
    status.values.push_back(kv);
  }
  {
    diagnostic_msgs::msg::KeyValue kv;
    kv.key   = "min_distance_threshold_m";
    kv.value = std::to_string(min_distance_asil_);
    status.values.push_back(kv);
  }
  {
    diagnostic_msgs::msg::KeyValue kv;
    kv.key   = "max_velocity_threshold_ms";
    kv.value = std::to_string(max_velocity_asil_);
    status.values.push_back(kv);
  }
  {
    diagnostic_msgs::msg::KeyValue kv;
    kv.key   = "emergency_stop_enabled";
    kv.value = emergency_stop_enabled_ ? "true" : "false";
    status.values.push_back(kv);
  }

  // Add last violation details if present
  if (!violation_log_.empty()) {
    const auto& last = violation_log_.back();
    diagnostic_msgs::msg::KeyValue kv;
    kv.key   = "last_violation_param";
    kv.value = last.parameter_name + "=" + std::to_string(last.actual_value) +
               " (limit=" + std::to_string(last.threshold_value) + ")";
    status.values.push_back(kv);
  }

  diag_array.status.push_back(status);
  diag_pub_->publish(diag_array);
}

// ──────────────────────────────────────────────────────────────────────────────
// main
// ──────────────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::NodeOptions opts;
  auto node = std::make_shared<SafetyBoundaryChecker>(opts);
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}

} // namespace module_6_testing
