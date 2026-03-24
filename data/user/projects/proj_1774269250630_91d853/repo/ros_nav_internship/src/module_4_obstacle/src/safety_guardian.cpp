#include <rclcpp/rclcpp.hpp>
#include <visualization_msgs/msg/marker_array.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <diagnostic_updater/diagnostic_updater.hpp>
#include <diagnostic_msgs/msg/diagnostic_status.hpp>

#include <Eigen/Dense>
#include <cmath>
#include <string>
#include <limits>
#include <mutex>
#include <atomic>

namespace module_4_obstacle {

// ─────────────────────────────────────────────────────────────────────────────
class SafetyGuardian : public rclcpp::Node
{
public:
  explicit SafetyGuardian(const rclcpp::NodeOptions& options)
  : Node("safety_guardian", options),
    diagnostics_(this)
  {
    // ── Parameters ────────────────────────────────────────────────────────
    declare_parameter("safety_distance",       1.5);
    declare_parameter("backup_speed",         -0.1);   // m/s (negative = backward)
    declare_parameter("backup_duration_s",     0.5);
    declare_parameter("check_rate_hz",        20.0);
    declare_parameter("stop_on_violation",     true);

    safety_distance_    = get_parameter("safety_distance").as_double();
    backup_speed_       = get_parameter("backup_speed").as_double();
    backup_duration_    = get_parameter("backup_duration_s").as_double();
    stop_on_violation_  = get_parameter("stop_on_violation").as_bool();
    const double rate   = get_parameter("check_rate_hz").as_double();

    // ── Parameter callback for dynamic reconfigure ─────────────────────────
    param_cb_handle_ = add_on_set_parameters_callback(
      [this](const std::vector<rclcpp::Parameter>& params)
      -> rcl_interfaces::msg::SetParametersResult
      {
        for (const auto& p : params) {
          if (p.get_name() == "safety_distance") {
            safety_distance_ = p.as_double();
            RCLCPP_INFO(get_logger(), "safety_distance updated → %.2f", safety_distance_);
          } else if (p.get_name() == "backup_speed") {
            backup_speed_ = p.as_double();
          } else if (p.get_name() == "stop_on_violation") {
            stop_on_violation_ = p.as_bool();
          }
        }
        rcl_interfaces::msg::SetParametersResult result;
        result.successful = true;
        return result;
      });

    // ── Subscribers ────────────────────────────────────────────────────────
    obstacles_sub_ = create_subscription<visualization_msgs::msg::MarkerArray>(
      "/tracked_obstacles", 10,
      [this](const visualization_msgs::msg::MarkerArray::SharedPtr msg) {
        std::lock_guard<std::mutex> lock(obstacles_mutex_);
        latest_obstacles_ = msg;
      });

    cmd_vel_sub_ = create_subscription<geometry_msgs::msg::Twist>(
      "/cmd_vel_in", 10,
      std::bind(&SafetyGuardian::cmdVelCallback, this, std::placeholders::_1));

    // ── Publishers ─────────────────────────────────────────────────────────
    cmd_vel_pub_ = create_publisher<geometry_msgs::msg::Twist>("/cmd_vel", 10);

    // ── Safety check timer ────────────────────────────────────────────────
    const auto period = std::chrono::duration<double>(1.0 / rate);
    check_timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::nanoseconds>(period),
      std::bind(&SafetyGuardian::checkSafety, this));

    // ── Diagnostics ───────────────────────────────────────────────────────
    diagnostics_.setHardwareID("safety_guardian");
    diagnostics_.add("Safety Status",
      this, &SafetyGuardian::produceDiagnostics);

    diag_timer_ = create_wall_timer(
      std::chrono::seconds(1),
      [this]() { diagnostics_.force_update(); });

    RCLCPP_INFO(get_logger(),
      "SafetyGuardian: safety_dist=%.2f  backup=%.2f m/s  rate=%.1f Hz",
      safety_distance_, backup_speed_, rate);
  }

private:
  // ── Incoming cmd_vel (pass-through when safe, blocked when unsafe) ────────
  void cmdVelCallback(const geometry_msgs::msg::Twist::SharedPtr msg)
  {
    last_cmd_vel_ = *msg;
    if (!safety_violated_.load()) {
      cmd_vel_pub_->publish(*msg);
    }
    // If violated, the checkSafety timer will publish STOP / backup instead
  }

  // ── Periodic safety check ─────────────────────────────────────────────────
  void checkSafety()
  {
    double min_dist = std::numeric_limits<double>::max();

    {
      std::lock_guard<std::mutex> lock(obstacles_mutex_);
      if (latest_obstacles_) {
        for (const auto& marker : latest_obstacles_->markers) {
          if (marker.action == visualization_msgs::msg::Marker::DELETEALL ||
              marker.action == visualization_msgs::msg::Marker::DELETE) {
            continue;
          }
          // Distance in the XY plane (ignore height)
          const double dist = std::hypot(marker.pose.position.x,
                                          marker.pose.position.y);
          if (dist < min_dist) {
            min_dist = dist;
          }
        }
      }
    }

    last_min_dist_ = min_dist;

    if (min_dist < safety_distance_ && stop_on_violation_) {
      if (!safety_violated_.exchange(true)) {
        RCLCPP_WARN(get_logger(),
          "SAFETY VIOLATION: closest obstacle at %.2f m (limit %.2f m)",
          min_dist, safety_distance_);
      }

      // Publish STOP command
      geometry_msgs::msg::Twist stop;
      stop.linear.x  = 0.0;
      stop.angular.z = 0.0;
      cmd_vel_pub_->publish(stop);

      // Publish backup command if obstacle is very close
      if (min_dist < safety_distance_ * 0.5) {
        geometry_msgs::msg::Twist backup;
        backup.linear.x  = backup_speed_;
        backup.angular.z = 0.0;
        cmd_vel_pub_->publish(backup);
      }

    } else {
      if (safety_violated_.exchange(false)) {
        RCLCPP_INFO(get_logger(),
          "Safety cleared: closest obstacle at %.2f m", min_dist);
      }
    }
  }

  // ── Diagnostics callback ──────────────────────────────────────────────────
  void produceDiagnostics(diagnostic_updater::DiagnosticStatusWrapper& stat)
  {
    const double dist = last_min_dist_.load();
    const bool violated = safety_violated_.load();

    stat.add("Minimum obstacle distance (m)", dist);
    stat.add("Safety distance threshold (m)", safety_distance_);
    stat.add("Safety violated",               violated ? "YES" : "NO");

    if (violated) {
      stat.summary(diagnostic_msgs::msg::DiagnosticStatus::ERROR,
                   "Safety violation: robot stopped");
    } else if (dist < safety_distance_ * 1.5) {
      stat.summary(diagnostic_msgs::msg::DiagnosticStatus::WARN,
                   "Obstacle approaching safety zone");
    } else {
      stat.summary(diagnostic_msgs::msg::DiagnosticStatus::OK,
                   "Safe");
    }
  }

  // ── Members ───────────────────────────────────────────────────────────────
  rclcpp::Subscription<visualization_msgs::msg::MarkerArray>::SharedPtr obstacles_sub_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr            cmd_vel_sub_;
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr               cmd_vel_pub_;
  rclcpp::TimerBase::SharedPtr check_timer_;
  rclcpp::TimerBase::SharedPtr diag_timer_;

  diagnostic_updater::Updater diagnostics_;

  rclcpp::node_interfaces::OnSetParametersCallbackHandle::SharedPtr param_cb_handle_;

  visualization_msgs::msg::MarkerArray::SharedPtr latest_obstacles_;
  std::mutex obstacles_mutex_;

  geometry_msgs::msg::Twist last_cmd_vel_;

  double safety_distance_{1.5};
  double backup_speed_{-0.1};
  double backup_duration_{0.5};
  bool   stop_on_violation_{true};

  std::atomic<bool>   safety_violated_{false};
  std::atomic<double> last_min_dist_{std::numeric_limits<double>::max()};
};

}  // namespace module_4_obstacle

// ── Main ─────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::NodeOptions options;
  auto node = std::make_shared<module_4_obstacle::SafetyGuardian>(options);
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
