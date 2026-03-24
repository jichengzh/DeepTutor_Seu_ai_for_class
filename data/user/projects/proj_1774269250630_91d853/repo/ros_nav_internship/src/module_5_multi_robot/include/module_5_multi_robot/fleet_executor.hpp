#pragma once
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <nav2_msgs/action/navigate_to_pose.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <std_msgs/msg/string.hpp>
#include <queue>
#include <map>
#include <mutex>
#include <string>

namespace module_5_multi_robot {

using NavigateToPose = nav2_msgs::action::NavigateToPose;
using GoalHandle     = rclcpp_action::ClientGoalHandle<NavigateToPose>;

/// Per-robot task queue and execution state.
struct RobotTaskQueue {
  int robot_id{-1};
  std::queue<geometry_msgs::msg::PoseStamped> goals;
  GoalHandle::SharedPtr current_goal_handle{nullptr};
  bool        busy{false};
  std::string status{"idle"};
  int         retry_count{0};
  int         max_retries{3};
};

/**
 * @class FleetExecutor
 * @brief Dispatches NavigateToPose action goals to a fleet of nav2-enabled robots.
 *
 * Subscribes to:
 *   - /task_assignment (std_msgs/String, JSON)  — CBBA allocation result
 *
 * Publishes:
 *   - /fleet_status   (std_msgs/String, JSON)   — per-robot status at 1 Hz
 *
 * For each robot N it creates a NavigateToPose action client on:
 *   /robot_N/navigate_to_pose
 */
class FleetExecutor : public rclcpp::Node {
public:
  explicit FleetExecutor(const rclcpp::NodeOptions & options = rclcpp::NodeOptions{});

private:
  // ── Callbacks ────────────────────────────────────────────────────────────────

  /// Parse JSON assignment message and populate per-robot goal queues.
  void assignmentCallback(const std_msgs::msg::String::SharedPtr msg);

  /// Send the next queued goal to robot `robot_id` if it is idle.
  void dispatchNextGoal(int robot_id);

  /// Called when the action server accepts or rejects the goal.
  void goalResponseCallback(const GoalHandle::SharedPtr & handle, int robot_id);

  /// Called when the action finishes (succeeded / aborted / cancelled).
  void resultCallback(const GoalHandle::WrappedResult & result, int robot_id);

  /// Called periodically with navigation progress.
  void feedbackCallback(GoalHandle::SharedPtr,
                        const std::shared_ptr<const NavigateToPose::Feedback> & feedback,
                        int robot_id);

  /// 1 Hz timer: publish JSON fleet-status message.
  void statusTimerCallback();

  // ── Members ──────────────────────────────────────────────────────────────────

  std::map<int, RobotTaskQueue>                                    robot_queues_;
  std::map<int, rclcpp_action::Client<NavigateToPose>::SharedPtr> nav_clients_;

  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr assignment_sub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr    fleet_status_pub_;
  rclcpp::TimerBase::SharedPtr                           status_timer_;

  int        num_robots_{3};
  std::mutex queue_mutex_;
};

}  // namespace module_5_multi_robot
