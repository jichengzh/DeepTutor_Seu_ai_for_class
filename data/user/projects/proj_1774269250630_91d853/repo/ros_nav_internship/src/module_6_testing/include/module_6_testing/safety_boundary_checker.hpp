#pragma once
#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <visualization_msgs/msg/marker_array.hpp>
#include <diagnostic_msgs/msg/diagnostic_array.hpp>
#include <string>
#include <vector>
#include <chrono>

namespace module_6_testing {

enum class AsilLevel { NONE, QM, ASIL_A, ASIL_B, ASIL_C, ASIL_D };

struct AsilEvent {
  std::chrono::system_clock::time_point timestamp;
  AsilLevel level;
  std::string description;
  double actual_value;
  double threshold_value;
  std::string parameter_name;
};

class SafetyBoundaryChecker : public rclcpp::Node {
public:
  explicit SafetyBoundaryChecker(const rclcpp::NodeOptions& options);

  void checkMinDistance(double actual_dist, double min_dist_asil);
  void checkMaxVelocity(double actual_vel, double max_vel_asil);
  bool hasAsilViolation() const { return !violation_log_.empty(); }
  std::vector<AsilEvent> getViolationLog() const { return violation_log_; }
  void clearViolationLog() { violation_log_.clear(); }

private:
  void velocityCallback(const geometry_msgs::msg::Twist::SharedPtr msg);
  void obstacleCallback(
      const visualization_msgs::msg::MarkerArray::SharedPtr msg);
  void publishDiagnostics();
  void logAsilEvent(AsilLevel level, const std::string& desc,
                     double actual, double threshold,
                     const std::string& param);

  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr vel_sub_;
  rclcpp::Subscription<visualization_msgs::msg::MarkerArray>::SharedPtr
      obstacle_sub_;
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr
      diag_pub_;
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr
      emergency_stop_pub_;
  rclcpp::TimerBase::SharedPtr diag_timer_;

  std::vector<AsilEvent> violation_log_;
  double min_distance_asil_{0.5};
  double max_velocity_asil_{1.5};
  bool emergency_stop_enabled_{true};
  AsilLevel asil_level_{AsilLevel::ASIL_B};
};

} // namespace module_6_testing
