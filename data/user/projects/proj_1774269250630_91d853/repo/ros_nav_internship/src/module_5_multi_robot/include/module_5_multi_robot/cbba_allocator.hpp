#pragma once
#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <std_msgs/msg/string.hpp>
#include <visualization_msgs/msg/marker_array.hpp>
#include <vector>
#include <map>
#include <string>
#include <limits>

namespace module_5_multi_robot {

/// A task with a goal pose, priority, and optional deadline.
struct Task {
  int id{-1};
  geometry_msgs::msg::PoseStamped pose;
  double priority{1.0};
  double deadline{std::numeric_limits<double>::infinity()};
  bool assigned{false};
  int assigned_robot{-1};
};

/// Per-robot state used by CBBA: current pose, battery, and bid book.
struct RobotState {
  int id{-1};
  geometry_msgs::msg::PoseStamped pose;
  double battery_level{1.0};
  bool available{true};
  std::vector<int> task_queue;          // ordered list of task ids in bundle
  std::map<int, double> bids;           // task_id -> winning bid value
  std::map<int, int>    winners;        // task_id -> robot_id that currently holds it
};

/// Result of one CBBA allocation run.
struct TaskAssignment {
  std::map<int, std::vector<int>> robot_to_tasks;  // robot_id -> [task_ids]
  double total_value{0.0};
  int iterations{0};
  bool converged{false};
};

/**
 * @class CBBAAllocator
 * @brief Consensus-Based Bundle Algorithm (CBBA) task allocator.
 *
 * Subscribes to:
 *   - /tasks_markers (visualization_msgs/MarkerArray) — incoming task poses
 *   - /robot_N/odom  (nav_msgs/Odometry)              — per-robot odometry
 *
 * Publishes:
 *   - /task_assignment (std_msgs/String, JSON)         — CBBA assignment result
 */
class CBBAAllocator : public rclcpp::Node {
public:
  explicit CBBAAllocator(const rclcpp::NodeOptions & options = rclcpp::NodeOptions{});

  /**
   * Run one full CBBA round and return the resulting assignment.
   * Tasks and robots are passed by value so CBBA can modify bid state internally.
   */
  TaskAssignment allocate(std::vector<Task> tasks,
                          std::vector<RobotState> robots);

private:
  // ── CBBA phases ─────────────────────────────────────────────────────────────

  /**
   * Bidding phase: robot `robot_id` greedily builds its bundle by adding the
   * task that maximises marginal value, provided the robot's bid beats the
   * current winning bid in the shared bid table.
   */
  void biddingPhase(int robot_id,
                    std::vector<Task> & tasks,
                    std::vector<RobotState> & robots);

  /**
   * Consensus phase: robots compare bid tables and resolve conflicts.
   * The highest-bid robot wins each task; ties are broken by lower robot id.
   * Returns true when no assignment changed (convergence).
   */
  bool consensusPhase(std::vector<Task> & tasks,
                      std::vector<RobotState> & robots);

  // ── Value / cost helpers ─────────────────────────────────────────────────────

  /// Composite task value for a given robot:
  ///   distance_weight_ / (1 + dist)  +  priority_weight_ * priority
  ///   + battery_weight_ * battery
  double taskValue(const Task & task, const RobotState & robot) const;

  /// Euclidean distance between two PoseStamped positions.
  double distanceCost(const geometry_msgs::msg::PoseStamped & from,
                      const geometry_msgs::msg::PoseStamped & to) const;

  // ── ROS callbacks ────────────────────────────────────────────────────────────

  void tasksCallback(const visualization_msgs::msg::MarkerArray::SharedPtr msg);
  void robotStateCallback(const nav_msgs::msg::Odometry::SharedPtr msg, int robot_id);

  /// Serialise assignment to JSON and publish on /task_assignment.
  void publishAssignment(const TaskAssignment & assignment);

  // ── Members ──────────────────────────────────────────────────────────────────

  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr assignment_pub_;
  rclcpp::Subscription<visualization_msgs::msg::MarkerArray>::SharedPtr task_sub_;
  std::vector<rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr> robot_subs_;

  std::vector<Task>        pending_tasks_;
  std::vector<RobotState>  robot_states_;

  // ── Parameters ───────────────────────────────────────────────────────────────

  double distance_weight_{0.6};
  double priority_weight_{0.3};
  double battery_weight_{0.1};
  int    max_iterations_{100};
  int    max_bundle_size_{3};
  int    num_robots_{3};
};

}  // namespace module_5_multi_robot
