/**
 * cbba_allocator.cpp
 *
 * Consensus-Based Bundle Algorithm (CBBA) implementation.
 *
 * Algorithm overview
 * ──────────────────
 * 1. Bidding phase  : every robot greedily builds a bundle by adding the task
 *    whose marginal value exceeds the current winning bid in the shared table.
 * 2. Consensus phase: all robots update their local bid/winner tables by taking
 *    the maximum bid for each task across all robots.  Assignments change if a
 *    higher bid is found.
 * 3. Convergence   : iteration stops when no assignment changed during a full
 *    consensus pass or max_iterations_ is reached.
 */

#include "module_5_multi_robot/cbba_allocator.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>

namespace module_5_multi_robot {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

CBBAAllocator::CBBAAllocator(const rclcpp::NodeOptions & options)
: Node("cbba_allocator", options)
{
  // Declare and get parameters
  this->declare_parameter("distance_weight",  distance_weight_);
  this->declare_parameter("priority_weight",  priority_weight_);
  this->declare_parameter("battery_weight",   battery_weight_);
  this->declare_parameter("max_iterations",   max_iterations_);
  this->declare_parameter("max_bundle_size",  max_bundle_size_);
  this->declare_parameter("num_robots",       num_robots_);

  distance_weight_ = this->get_parameter("distance_weight").as_double();
  priority_weight_ = this->get_parameter("priority_weight").as_double();
  battery_weight_  = this->get_parameter("battery_weight").as_double();
  max_iterations_  = this->get_parameter("max_iterations").as_int();
  max_bundle_size_ = this->get_parameter("max_bundle_size").as_int();
  num_robots_      = this->get_parameter("num_robots").as_int();

  // Publisher
  assignment_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/task_assignment", rclcpp::QoS(10).reliable().transient_local());

  // Task subscriber (visualization markers from a task generator)
  task_sub_ = this->create_subscription<visualization_msgs::msg::MarkerArray>(
      "/tasks_markers", 10,
      std::bind(&CBBAAllocator::tasksCallback, this, std::placeholders::_1));

  // Per-robot odometry subscribers
  robot_states_.resize(num_robots_);
  for (int i = 0; i < num_robots_; ++i) {
    robot_states_[i].id = i;
    std::string topic = "/robot_" + std::to_string(i) + "/odom";
    robot_subs_.push_back(
        this->create_subscription<nav_msgs::msg::Odometry>(
            topic, 10,
            [this, i](const nav_msgs::msg::Odometry::SharedPtr msg) {
              robotStateCallback(msg, i);
            }));
  }

  RCLCPP_INFO(this->get_logger(),
              "CBBAAllocator ready: %d robots, max_bundle=%d, max_iter=%d",
              num_robots_, max_bundle_size_, max_iterations_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Public: allocate
// ─────────────────────────────────────────────────────────────────────────────

TaskAssignment CBBAAllocator::allocate(std::vector<Task>       tasks,
                                       std::vector<RobotState> robots)
{
  // Initialise all bids to 0, no winner
  for (auto & robot : robots) {
    robot.bids.clear();
    robot.winners.clear();
    robot.task_queue.clear();
    for (const auto & task : tasks) {
      robot.bids[task.id]    = 0.0;
      robot.winners[task.id] = -1;
    }
  }

  TaskAssignment result;
  bool converged = false;

  for (int iter = 0; iter < max_iterations_ && !converged; ++iter) {
    result.iterations = iter + 1;

    // Phase 1: every robot bids
    for (auto & robot : robots) {
      biddingPhase(robot.id, tasks, robots);
    }

    // Phase 2: consensus (returns true when nothing changed)
    converged = consensusPhase(tasks, robots);
  }

  result.converged = converged;

  // Build final assignment map and compute total value
  for (const auto & robot : robots) {
    result.robot_to_tasks[robot.id] = robot.task_queue;
    for (int tid : robot.task_queue) {
      // Find the task
      for (const auto & task : tasks) {
        if (task.id == tid) {
          result.total_value += taskValue(task, robot);
          break;
        }
      }
    }
  }

  return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Private: biddingPhase
// ─────────────────────────────────────────────────────────────────────────────

void CBBAAllocator::biddingPhase(int                      robot_id,
                                  std::vector<Task> &      tasks,
                                  std::vector<RobotState> & robots)
{
  RobotState & robot = robots[robot_id];

  // Reset this robot's bundle — will rebuild greedily
  robot.task_queue.clear();

  // Greedily add tasks up to max_bundle_size_
  for (int bundle_slot = 0; bundle_slot < max_bundle_size_; ++bundle_slot) {
    double best_marginal = 0.0;
    int    best_task_id  = -1;

    for (const auto & task : tasks) {
      if (task.assigned && task.assigned_robot != robot_id) {
        continue;  // already locked by someone else
      }

      // Skip tasks already in this robot's current bundle
      bool already_in_bundle = false;
      for (int tid : robot.task_queue) {
        if (tid == task.id) { already_in_bundle = true; break; }
      }
      if (already_in_bundle) continue;

      double value = taskValue(task, robot);

      // Marginal value: value minus the current best bid on that task
      double current_best = robot.bids.count(task.id) ? robot.bids.at(task.id) : 0.0;
      double marginal = value - current_best;

      if (marginal > best_marginal) {
        best_marginal = marginal;
        best_task_id  = task.id;
      }
    }

    if (best_task_id == -1) break;  // no beneficial task found

    // Place bid: update our local table if we're the best
    double new_bid = taskValue(*std::find_if(tasks.begin(), tasks.end(),
                                [best_task_id](const Task & t){
                                  return t.id == best_task_id;
                                }), robot);

    robot.bids[best_task_id]    = new_bid;
    robot.winners[best_task_id] = robot_id;
    robot.task_queue.push_back(best_task_id);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Private: consensusPhase
// ─────────────────────────────────────────────────────────────────────────────

bool CBBAAllocator::consensusPhase(std::vector<Task> &       tasks,
                                    std::vector<RobotState> & robots)
{
  bool no_change = true;

  // For each task, find the robot with the highest bid
  for (auto & task : tasks) {
    double best_bid    = 0.0;
    int    best_robot  = -1;

    for (const auto & robot : robots) {
      auto it = robot.bids.find(task.id);
      if (it != robot.bids.end() && it->second > best_bid) {
        best_bid   = it->second;
        best_robot = robot.id;
      }
    }

    int old_robot = task.assigned_robot;

    if (best_robot != -1 && best_robot != old_robot) {
      no_change = false;

      // Remove task from previous robot's queue
      if (old_robot >= 0 && old_robot < static_cast<int>(robots.size())) {
        auto & prev_queue = robots[old_robot].task_queue;
        prev_queue.erase(
            std::remove(prev_queue.begin(), prev_queue.end(), task.id),
            prev_queue.end());
      }

      task.assigned       = true;
      task.assigned_robot = best_robot;

      // Broadcast the winner to all robots so they can update their tables
      for (auto & robot : robots) {
        if (robot.bids.count(task.id)) {
          // A robot that placed a lower bid relinquishes the task
          if (robot.id != best_robot && robot.bids[task.id] < best_bid) {
            robot.bids[task.id]    = best_bid;
            robot.winners[task.id] = best_robot;
            // Remove from its local bundle if it had it
            auto & q = robot.task_queue;
            q.erase(std::remove(q.begin(), q.end(), task.id), q.end());
          }
        }
        robots[best_robot].bids[task.id]    = best_bid;
        robots[best_robot].winners[task.id] = best_robot;
      }
    }
  }

  return no_change;
}

// ─────────────────────────────────────────────────────────────────────────────
// Private: taskValue
// ─────────────────────────────────────────────────────────────────────────────

double CBBAAllocator::taskValue(const Task & task, const RobotState & robot) const
{
  double dist  = distanceCost(robot.pose, task.pose);
  double value = distance_weight_ / (1.0 + dist)
               + priority_weight_ * task.priority
               + battery_weight_  * robot.battery_level;
  return value;
}

// ─────────────────────────────────────────────────────────────────────────────
// Private: distanceCost
// ─────────────────────────────────────────────────────────────────────────────

double CBBAAllocator::distanceCost(
    const geometry_msgs::msg::PoseStamped & from,
    const geometry_msgs::msg::PoseStamped & to) const
{
  double dx = to.pose.position.x - from.pose.position.x;
  double dy = to.pose.position.y - from.pose.position.y;
  return std::sqrt(dx * dx + dy * dy);
}

// ─────────────────────────────────────────────────────────────────────────────
// Private: tasksCallback
// ─────────────────────────────────────────────────────────────────────────────

void CBBAAllocator::tasksCallback(
    const visualization_msgs::msg::MarkerArray::SharedPtr msg)
{
  pending_tasks_.clear();
  for (const auto & marker : msg->markers) {
    Task t;
    t.id = marker.id;
    t.pose.header   = marker.header;
    t.pose.pose     = marker.pose;
    // Priority encoded in marker scale x (optional convention)
    t.priority = marker.scale.x > 0.0 ? marker.scale.x : 1.0;
    pending_tasks_.push_back(t);
  }
  RCLCPP_INFO(this->get_logger(), "Received %zu tasks, running CBBA…",
              pending_tasks_.size());

  if (!pending_tasks_.empty() && !robot_states_.empty()) {
    auto assignment = allocate(pending_tasks_, robot_states_);
    publishAssignment(assignment);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Private: robotStateCallback
// ─────────────────────────────────────────────────────────────────────────────

void CBBAAllocator::robotStateCallback(
    const nav_msgs::msg::Odometry::SharedPtr msg, int robot_id)
{
  if (robot_id < 0 || robot_id >= static_cast<int>(robot_states_.size())) return;

  auto & rs         = robot_states_[robot_id];
  rs.pose.header    = msg->header;
  rs.pose.pose      = msg->pose.pose;
}

// ─────────────────────────────────────────────────────────────────────────────
// Private: publishAssignment
// ─────────────────────────────────────────────────────────────────────────────

void CBBAAllocator::publishAssignment(const TaskAssignment & assignment)
{
  // Minimal hand-written JSON — avoids external JSON library dependency
  std::ostringstream oss;
  oss << "{"
      << "\"converged\":" << (assignment.converged ? "true" : "false") << ","
      << "\"iterations\":" << assignment.iterations << ","
      << "\"total_value\":" << assignment.total_value << ","
      << "\"assignment\":{";

  bool first_robot = true;
  for (const auto & [robot_id, task_ids] : assignment.robot_to_tasks) {
    if (!first_robot) oss << ",";
    first_robot = false;
    oss << "\"" << robot_id << "\":[";
    bool first_task = true;
    for (int tid : task_ids) {
      if (!first_task) oss << ",";
      first_task = false;
      // Serialise task pose as well for the FleetExecutor
      oss << "{\"task_id\":" << tid;
      // Look up pose in pending_tasks_
      for (const auto & t : pending_tasks_) {
        if (t.id == tid) {
          oss << ",\"x\":" << t.pose.pose.position.x
              << ",\"y\":" << t.pose.pose.position.y
              << ",\"z\":" << t.pose.pose.position.z
              << ",\"qx\":" << t.pose.pose.orientation.x
              << ",\"qy\":" << t.pose.pose.orientation.y
              << ",\"qz\":" << t.pose.pose.orientation.z
              << ",\"qw\":" << t.pose.pose.orientation.w;
          break;
        }
      }
      oss << "}";
    }
    oss << "]";
  }
  oss << "}}";

  std_msgs::msg::String out;
  out.data = oss.str();
  assignment_pub_->publish(out);

  RCLCPP_INFO(this->get_logger(),
              "Published assignment: converged=%s, iter=%d, value=%.3f",
              assignment.converged ? "yes" : "no",
              assignment.iterations,
              assignment.total_value);
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

}  // namespace module_5_multi_robot

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<module_5_multi_robot::CBBAAllocator>());
  rclcpp::shutdown();
  return 0;
}
