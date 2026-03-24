#pragma once
#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <std_msgs/msg/string.hpp>
#include <optional>
#include <vector>
#include <map>
#include <string>

namespace module_5_multi_robot {

/// Runtime robot state: pose + velocity extracted from odometry.
struct RobotState {
  int    id{-1};
  double x{0.0}, y{0.0};        ///< Position [m]
  double vx{0.0}, vy{0.0};      ///< Linear velocity [m/s]
  double yaw{0.0};               ///< Heading [rad]
  rclcpp::Time stamp;
};

/**
 * @class ConflictResolver
 * @brief Online conflict detector and resolver for a fleet of robots.
 *
 * Checks at 10 Hz whether any two robots are on a collision course (TTC below
 * threshold).  When a conflict is detected the robot with the lower id yields
 * by following a laterally offset waypoint (right-hand traffic rule).
 *
 * Subscribes to:
 *   /robot_N/odom   (nav_msgs/Odometry)
 *
 * Publishes:
 *   /conflict_resolved_waypoint  (geometry_msgs/PoseStamped)
 *   /conflict_events             (std_msgs/String, JSON)
 *
 * Parameters:
 *   num_robots                  (int,    default 3)
 *   conflict_detection_distance (double, default 2.0)  [m]
 *   ttc_threshold               (double, default 3.0)  [s]
 *   lateral_offset              (double, default 0.5)  [m]
 */
class ConflictResolver : public rclcpp::Node {
public:
  explicit ConflictResolver(const rclcpp::NodeOptions & options = rclcpp::NodeOptions{});

  /**
   * Compute Time-To-Collision between two robots.
   *
   * Uses relative position and relative velocity to solve for the time t >= 0
   * at which the separation equals zero (point-mass model).  Returns nullopt if
   * the robots are diverging or already separated beyond detection distance.
   *
   * @return time to collision [s], or nullopt if no imminent collision.
   */
  std::optional<double> computeTTC(const RobotState & robot_a,
                                    const RobotState & robot_b) const;

  /**
   * Generate a laterally offset waypoint for `robot` to avoid a conflict.
   *
   * The offset is perpendicular to the robot's current heading, positive to
   * the right (right-hand traffic convention).
   *
   * @param robot            Robot whose path should be shifted.
   * @param offset_distance  How far to shift perpendicular to heading [m].
   * @return                 Offset waypoint as PoseStamped in map frame.
   */
  geometry_msgs::msg::PoseStamped computeLateralOffset(
      const RobotState & robot,
      double offset_distance) const;

private:
  // ── Callbacks ────────────────────────────────────────────────────────────────

  void robotStatesCallback(const nav_msgs::msg::Odometry::SharedPtr msg, int robot_id);

  /// 10 Hz: check all robot pairs for conflicts and publish resolutions.
  void checkConflicts();

  /// Resolve a detected conflict between robots a and b.
  /// Lower-id robot yields (right-hand offset); publishes waypoint + event.
  void resolveConflict(int robot_a_id, int robot_b_id, double ttc);

  // ── Helpers ──────────────────────────────────────────────────────────────────

  /// Euclidean distance between two robots.
  double robotDistance(const RobotState & a, const RobotState & b) const;

  /// Build JSON conflict-event string.
  std::string buildConflictJson(int robot_a, int robot_b,
                                 double ttc, const std::string & action) const;

  // ── Members ──────────────────────────────────────────────────────────────────

  std::map<int, RobotState>                                          robot_states_;
  std::vector<rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr> robot_subs_;

  rclcpp::Publisher<geometry_msgs::msg::PoseStamped>::SharedPtr offset_cmd_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr            conflict_events_pub_;
  rclcpp::TimerBase::SharedPtr                                   check_timer_;

  // ── Parameters ───────────────────────────────────────────────────────────────

  double conflict_detection_distance_{2.0};  // m
  double ttc_threshold_{3.0};                // s
  double lateral_offset_{0.5};               // m
  int    num_robots_{3};
};

}  // namespace module_5_multi_robot
