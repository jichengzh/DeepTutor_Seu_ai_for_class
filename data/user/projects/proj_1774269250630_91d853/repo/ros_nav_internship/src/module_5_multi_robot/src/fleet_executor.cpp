/**
 * fleet_executor.cpp
 *
 * Dispatches NavigateToPose action goals to a configurable fleet of robots.
 *
 * Design
 * ──────
 * - One NavigateToPose action client per robot, connected to
 *   /robot_N/navigate_to_pose.
 * - Parses the JSON task assignment from /task_assignment and builds per-robot
 *   goal queues.
 * - dispatchNextGoal() is called whenever a robot becomes idle or a new
 *   assignment arrives.
 * - Failed goals are retried up to max_retries_ times, then skipped with a
 *   warning.
 * - A 1 Hz timer publishes a JSON fleet-status message on /fleet_status.
 */

#include "module_5_multi_robot/fleet_executor.hpp"

#include <functional>
#include <sstream>
#include <string>
#include <regex>

namespace module_5_multi_robot {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

FleetExecutor::FleetExecutor(const rclcpp::NodeOptions & options)
: Node("fleet_executor", options)
{
  this->declare_parameter("num_robots", num_robots_);
  num_robots_ = this->get_parameter("num_robots").as_int();

  // Subscriber for CBBA assignments
  assignment_sub_ = this->create_subscription<std_msgs::msg::String>(
      "/task_assignment",
      rclcpp::QoS(10).reliable().transient_local(),
      std::bind(&FleetExecutor::assignmentCallback, this, std::placeholders::_1));

  // Fleet status publisher
  fleet_status_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/fleet_status", rclcpp::QoS(10));

  // One action client + queue per robot
  for (int i = 0; i < num_robots_; ++i) {
    RobotTaskQueue q;
    q.robot_id = i;
    robot_queues_[i] = q;

    std::string action_name = "/robot_" + std::to_string(i) + "/navigate_to_pose";
    nav_clients_[i] = rclcpp_action::create_client<NavigateToPose>(this, action_name);
  }

  // 1 Hz status timer
  status_timer_ = this->create_wall_timer(
      std::chrono::seconds(1),
      std::bind(&FleetExecutor::statusTimerCallback, this));

  RCLCPP_INFO(this->get_logger(),
              "FleetExecutor ready — managing %d robots", num_robots_);
}

// ─────────────────────────────────────────────────────────────────────────────
// assignmentCallback — parse JSON, fill queues
// ─────────────────────────────────────────────────────────────────────────────

void FleetExecutor::assignmentCallback(const std_msgs::msg::String::SharedPtr msg)
{
  std::lock_guard<std::mutex> lock(queue_mutex_);

  RCLCPP_INFO(this->get_logger(), "Received assignment, parsing…");

  // ── Minimal JSON parser ────────────────────────────────────────────────────
  // Expected format (from cbba_allocator):
  //   {"converged":true,"iterations":N,"total_value":V,
  //    "assignment":{"0":[{"task_id":0,"x":1.0,"y":2.0,...}],"1":[...]}}

  const std::string & json = msg->data;

  // Find the "assignment" object
  auto asgn_pos = json.find("\"assignment\":");
  if (asgn_pos == std::string::npos) {
    RCLCPP_WARN(this->get_logger(), "No 'assignment' key in message");
    return;
  }

  // Use regex to extract per-robot task lists
  // Match: "ROBOT_ID":[...array of task objects...]
  std::regex robot_re(R"("(\d+)":\[([^\]]*)\])");
  std::sregex_iterator it(json.begin() + asgn_pos, json.end(), robot_re);
  std::sregex_iterator end_it;

  // Clear existing queues
  for (int i = 0; i < num_robots_; ++i) {
    while (!robot_queues_[i].goals.empty()) robot_queues_[i].goals.pop();
    robot_queues_[i].retry_count = 0;
  }

  for (; it != end_it; ++it) {
    const std::smatch & m   = *it;
    int robot_id             = std::stoi(m[1].str());
    std::string tasks_str    = m[2].str();

    if (robot_id < 0 || robot_id >= num_robots_) continue;

    // Extract each task object: {"task_id":N,"x":...,"y":...,"z":...,"qx":...,"qy":...,"qz":...,"qw":...}
    std::regex task_re(
        R"(\{[^}]*"task_id":(\d+)[^}]*"x":([0-9e.+-]+)[^}]*"y":([0-9e.+-]+)[^}]*"z":([0-9e.+-]+)[^}]*"qx":([0-9e.+-]+)[^}]*"qy":([0-9e.+-]+)[^}]*"qz":([0-9e.+-]+)[^}]*"qw":([0-9e.+-]+)[^}]*\})");
    std::sregex_iterator tit(tasks_str.begin(), tasks_str.end(), task_re);
    std::sregex_iterator tend;

    for (; tit != tend; ++tit) {
      const std::smatch & tm = *tit;
      geometry_msgs::msg::PoseStamped goal;
      goal.header.frame_id      = "map";
      goal.header.stamp         = this->now();
      goal.pose.position.x      = std::stod(tm[2].str());
      goal.pose.position.y      = std::stod(tm[3].str());
      goal.pose.position.z      = std::stod(tm[4].str());
      goal.pose.orientation.x   = std::stod(tm[5].str());
      goal.pose.orientation.y   = std::stod(tm[6].str());
      goal.pose.orientation.z   = std::stod(tm[7].str());
      goal.pose.orientation.w   = std::stod(tm[8].str());

      // Default orientation if not meaningful
      if (goal.pose.orientation.w == 0.0) goal.pose.orientation.w = 1.0;

      robot_queues_[robot_id].goals.push(goal);

      RCLCPP_DEBUG(this->get_logger(),
                   "  Robot %d <- task %s at (%.2f, %.2f)",
                   robot_id, tm[1].str().c_str(),
                   goal.pose.position.x, goal.pose.position.y);
    }
  }

  // Dispatch to all idle robots
  for (int i = 0; i < num_robots_; ++i) {
    if (!robot_queues_[i].busy && !robot_queues_[i].goals.empty()) {
      dispatchNextGoal(i);
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// dispatchNextGoal
// ─────────────────────────────────────────────────────────────────────────────

void FleetExecutor::dispatchNextGoal(int robot_id)
{
  auto & q = robot_queues_[robot_id];
  if (q.goals.empty()) {
    q.busy   = false;
    q.status = "idle";
    RCLCPP_INFO(this->get_logger(), "Robot %d: all tasks done", robot_id);
    return;
  }

  auto & client = nav_clients_[robot_id];
  if (!client->wait_for_action_server(std::chrono::seconds(2))) {
    RCLCPP_WARN(this->get_logger(),
                "Robot %d: action server not available, will retry", robot_id);
    return;
  }

  geometry_msgs::msg::PoseStamped goal_pose = q.goals.front();
  q.goals.pop();

  NavigateToPose::Goal goal_msg;
  goal_msg.pose = goal_pose;

  auto send_goal_opts = rclcpp_action::Client<NavigateToPose>::SendGoalOptions{};

  send_goal_opts.goal_response_callback =
      [this, robot_id](const GoalHandle::SharedPtr & handle) {
        goalResponseCallback(handle, robot_id);
      };

  send_goal_opts.feedback_callback =
      [this, robot_id](GoalHandle::SharedPtr gh,
                        const std::shared_ptr<const NavigateToPose::Feedback> & fb) {
        feedbackCallback(gh, fb, robot_id);
      };

  send_goal_opts.result_callback =
      [this, robot_id](const GoalHandle::WrappedResult & result) {
        resultCallback(result, robot_id);
      };

  q.busy   = true;
  q.status = "navigating";

  RCLCPP_INFO(this->get_logger(),
              "Robot %d: dispatching goal (%.2f, %.2f)",
              robot_id,
              goal_pose.pose.position.x,
              goal_pose.pose.position.y);

  client->async_send_goal(goal_msg, send_goal_opts);
}

// ─────────────────────────────────────────────────────────────────────────────
// goalResponseCallback
// ─────────────────────────────────────────────────────────────────────────────

void FleetExecutor::goalResponseCallback(const GoalHandle::SharedPtr & handle,
                                          int robot_id)
{
  std::lock_guard<std::mutex> lock(queue_mutex_);
  if (!handle) {
    RCLCPP_ERROR(this->get_logger(), "Robot %d: goal rejected", robot_id);
    robot_queues_[robot_id].busy   = false;
    robot_queues_[robot_id].status = "goal_rejected";
    dispatchNextGoal(robot_id);
    return;
  }
  robot_queues_[robot_id].current_goal_handle = handle;
  robot_queues_[robot_id].status = "goal_accepted";
  RCLCPP_INFO(this->get_logger(), "Robot %d: goal accepted", robot_id);
}

// ─────────────────────────────────────────────────────────────────────────────
// resultCallback
// ─────────────────────────────────────────────────────────────────────────────

void FleetExecutor::resultCallback(const GoalHandle::WrappedResult & result,
                                    int robot_id)
{
  std::lock_guard<std::mutex> lock(queue_mutex_);
  auto & q = robot_queues_[robot_id];

  switch (result.code) {
    case rclcpp_action::ResultCode::SUCCEEDED:
      RCLCPP_INFO(this->get_logger(), "Robot %d: goal succeeded", robot_id);
      q.status      = "goal_succeeded";
      q.retry_count = 0;
      q.busy        = false;
      dispatchNextGoal(robot_id);
      break;

    case rclcpp_action::ResultCode::ABORTED:
      RCLCPP_WARN(this->get_logger(),
                  "Robot %d: goal aborted (retry %d/%d)",
                  robot_id, q.retry_count + 1, q.max_retries);
      if (q.retry_count < q.max_retries) {
        q.retry_count++;
        q.status = "retrying";
        q.busy   = false;
        dispatchNextGoal(robot_id);
      } else {
        RCLCPP_ERROR(this->get_logger(),
                     "Robot %d: max retries exceeded, skipping goal", robot_id);
        q.retry_count = 0;
        q.status      = "goal_failed";
        q.busy        = false;
        dispatchNextGoal(robot_id);
      }
      break;

    case rclcpp_action::ResultCode::CANCELED:
      RCLCPP_INFO(this->get_logger(), "Robot %d: goal cancelled", robot_id);
      q.status      = "goal_cancelled";
      q.retry_count = 0;
      q.busy        = false;
      break;

    default:
      RCLCPP_WARN(this->get_logger(), "Robot %d: unknown result code", robot_id);
      q.busy   = false;
      q.status = "unknown";
      dispatchNextGoal(robot_id);
      break;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// feedbackCallback
// ─────────────────────────────────────────────────────────────────────────────

void FleetExecutor::feedbackCallback(
    GoalHandle::SharedPtr /*gh*/,
    const std::shared_ptr<const NavigateToPose::Feedback> & feedback,
    int robot_id)
{
  RCLCPP_DEBUG(this->get_logger(),
               "Robot %d: %.1f m remaining",
               robot_id,
               feedback->distance_remaining);
  robot_queues_[robot_id].status =
      "navigating (" + std::to_string(static_cast<int>(feedback->distance_remaining)) +
      "m remaining)";
}

// ─────────────────────────────────────────────────────────────────────────────
// statusTimerCallback — publish JSON fleet status at 1 Hz
// ─────────────────────────────────────────────────────────────────────────────

void FleetExecutor::statusTimerCallback()
{
  std::lock_guard<std::mutex> lock(queue_mutex_);

  std::ostringstream oss;
  oss << "{\"fleet_status\":{";

  bool first = true;
  for (const auto & [rid, q] : robot_queues_) {
    if (!first) oss << ",";
    first = false;
    oss << "\"" << rid << "\":{"
        << "\"busy\":"          << (q.busy ? "true" : "false") << ","
        << "\"status\":\""      << q.status                    << "\","
        << "\"queue_size\":"    << q.goals.size()              << ","
        << "\"retry_count\":"   << q.retry_count
        << "}";
  }
  oss << "}}";

  std_msgs::msg::String out;
  out.data = oss.str();
  fleet_status_pub_->publish(out);
}

}  // namespace module_5_multi_robot

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<module_5_multi_robot::FleetExecutor>());
  rclcpp::shutdown();
  return 0;
}
