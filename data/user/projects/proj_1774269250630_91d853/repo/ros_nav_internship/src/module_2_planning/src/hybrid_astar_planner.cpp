#include "module_2_planning/hybrid_astar_planner.hpp"

#include <nav2_util/node_utils.hpp>
#include <nav2_costmap_2d/costmap_2d.hpp>
#include <tf2_geometry_msgs/tf2_geometry_msgs.hpp>

#include <cmath>
#include <chrono>
#include <algorithm>
#include <stdexcept>

namespace module_2_planning {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static constexpr double TWO_PI = 2.0 * M_PI;

/// Wrap angle into [-π, π).
static double wrapAngle(double a) {
  while (a >  M_PI) a -= TWO_PI;
  while (a < -M_PI) a += TWO_PI;
  return a;
}

/// Euclidean distance between two 2D points.
static double euclidean2D(double x1, double y1, double x2, double y2) {
  double dx = x2 - x1, dy = y2 - y1;
  return std::sqrt(dx * dx + dy * dy);
}

// ─────────────────────────────────────────────────────────────────────────────
// configure
// ─────────────────────────────────────────────────────────────────────────────
void HybridAStarPlanner::configure(
  const rclcpp_lifecycle::LifecycleNode::WeakPtr& parent,
  std::string name,
  std::shared_ptr<tf2_ros::Buffer> tf,
  std::shared_ptr<nav2_costmap_2d::Costmap2DROS> costmap_ros)
{
  node_        = parent;
  name_        = name;
  tf_          = tf;
  costmap_ros_ = costmap_ros;

  auto node = node_.lock();
  if (!node) {
    throw std::runtime_error("HybridAStarPlanner::configure: parent node expired");
  }

  RCLCPP_INFO(node->get_logger(),
    "[%s] Configuring HybridAStarPlanner", name_.c_str());

  // Declare and read parameters
  nav2_util::declare_parameter_if_not_declared(
    node, name_ + ".min_turning_radius",  rclcpp::ParameterValue(0.5));
  nav2_util::declare_parameter_if_not_declared(
    node, name_ + ".angle_resolution",    rclcpp::ParameterValue(0.0873));
  nav2_util::declare_parameter_if_not_declared(
    node, name_ + ".max_planning_time",   rclcpp::ParameterValue(5.0));
  nav2_util::declare_parameter_if_not_declared(
    node, name_ + ".step_size",           rclcpp::ParameterValue(0.5));
  nav2_util::declare_parameter_if_not_declared(
    node, name_ + ".num_steering_angles", rclcpp::ParameterValue(3));
  nav2_util::declare_parameter_if_not_declared(
    node, name_ + ".obstacle_cost_threshold", rclcpp::ParameterValue(200.0));

  node->get_parameter(name_ + ".min_turning_radius",      min_turning_radius_);
  node->get_parameter(name_ + ".angle_resolution",        angle_resolution_);
  node->get_parameter(name_ + ".max_planning_time",       max_planning_time_);
  node->get_parameter(name_ + ".step_size",               step_size_);
  node->get_parameter(name_ + ".num_steering_angles",     num_steering_angles_);
  node->get_parameter(name_ + ".obstacle_cost_threshold", obstacle_cost_threshold_);

  num_angle_bins_ = static_cast<int>(std::round(TWO_PI / angle_resolution_));

  RCLCPP_INFO(node->get_logger(),
    "[%s] min_turning_radius=%.2f  angle_bins=%d  max_time=%.1f s",
    name_.c_str(), min_turning_radius_, num_angle_bins_, max_planning_time_);
}

void HybridAStarPlanner::cleanup()   { costmap_ros_.reset(); }
void HybridAStarPlanner::activate()  {}
void HybridAStarPlanner::deactivate() {}

// ─────────────────────────────────────────────────────────────────────────────
// nodeIndex
// ─────────────────────────────────────────────────────────────────────────────
int HybridAStarPlanner::nodeIndex(int ix, int iy, int ith) const {
  const int W = static_cast<int>(costmap_ros_->getCostmap()->getSizeInCellsX());
  const int H = static_cast<int>(costmap_ros_->getCostmap()->getSizeInCellsY());
  return (ith * H + iy) * W + ix;
}

// ─────────────────────────────────────────────────────────────────────────────
// isCollision
// ─────────────────────────────────────────────────────────────────────────────
bool HybridAStarPlanner::isCollision(
  float x, float y,
  const nav2_costmap_2d::Costmap2D* costmap) const
{
  unsigned int mx = 0, my = 0;
  if (!costmap->worldToMap(
        static_cast<double>(x), static_cast<double>(y), mx, my)) {
    return true;  // outside map → treat as obstacle
  }
  return costmap->getCost(mx, my) >= static_cast<unsigned char>(obstacle_cost_threshold_);
}

// ─────────────────────────────────────────────────────────────────────────────
// heuristic
// ─────────────────────────────────────────────────────────────────────────────
double HybridAStarPlanner::heuristic(const Node3D& node, const Node3D& goal) const {
  double eucl = euclidean2D(node.x, node.y, goal.x, goal.y);
  double dub  = dubinsHeuristic(node, goal);
  return std::max(eucl, dub);
}

// ─────────────────────────────────────────────────────────────────────────────
// dubinsHeuristic — simplified Dubins path lower bound
// ─────────────────────────────────────────────────────────────────────────────
double HybridAStarPlanner::dubinsHeuristic(
  const Node3D& from, const Node3D& to) const
{
  // We approximate the Dubins path length as the distance plus a turning cost.
  // A proper implementation would use the six Dubins path types; here we use
  // a conservative under-estimate: the straight-line distance divided by the
  // cosine of the half-heading-difference (bounded to avoid divide-by-zero).
  double dx    = to.x - from.x;
  double dy    = to.y - from.y;
  double dist  = std::sqrt(dx * dx + dy * dy);

  double alpha = std::atan2(dy, dx);
  double dth1  = std::fabs(wrapAngle(static_cast<double>(from.theta) - alpha));
  double dth2  = std::fabs(wrapAngle(static_cast<double>(to.theta)   - alpha));

  // Lower bound: the path must cover at least these two turns.
  double turn_cost = min_turning_radius_ * (dth1 + dth2);
  return dist + turn_cost;
}

// ─────────────────────────────────────────────────────────────────────────────
// expand — generate successors using Ackermann motion primitives
// ─────────────────────────────────────────────────────────────────────────────
std::vector<Node3D> HybridAStarPlanner::expand(
  const Node3D& current,
  const nav2_costmap_2d::Costmap2D* costmap) const
{
  std::vector<Node3D> successors;
  successors.reserve(2 * num_steering_angles_ + 1);

  // Build steering angles: 0 (straight) + symmetric set left/right
  std::vector<double> steers;
  steers.push_back(0.0);
  for (int i = 1; i <= num_steering_angles_; ++i) {
    double delta = static_cast<double>(i) / num_steering_angles_
                   * (M_PI / 4.0);  // max steer ≈ 45 deg
    steers.push_back( delta);
    steers.push_back(-delta);
  }

  for (double steer : steers) {
    Node3D next;
    double radius = (std::fabs(steer) < 1e-6)
                    ? std::numeric_limits<double>::infinity()
                    : min_turning_radius_ / std::sin(std::fabs(steer));

    double theta = static_cast<double>(current.theta);

    if (std::isinf(radius)) {
      // Straight motion
      next.x = current.x + static_cast<float>(step_size_ * std::cos(theta));
      next.y = current.y + static_cast<float>(step_size_ * std::sin(theta));
      next.theta = current.theta;
    } else {
      // Arc motion
      double arc_angle = step_size_ / radius;
      if (steer < 0.0) arc_angle = -arc_angle;

      // ICC (Instantaneous Centre of Curvature) offset
      double icc_x = static_cast<double>(current.x)
                     - radius * std::sin(theta);
      double icc_y = static_cast<double>(current.y)
                     + radius * std::cos(theta);

      double new_theta = theta + arc_angle;
      next.x = static_cast<float>(
        std::cos(arc_angle) * (static_cast<double>(current.x) - icc_x)
        - std::sin(arc_angle) * (static_cast<double>(current.y) - icc_y)
        + icc_x);
      next.y = static_cast<float>(
        std::sin(arc_angle) * (static_cast<double>(current.x) - icc_x)
        + std::cos(arc_angle) * (static_cast<double>(current.y) - icc_y)
        + icc_y);
      next.theta = static_cast<float>(wrapAngle(new_theta));
    }

    if (isCollision(next.x, next.y, costmap)) {
      continue;
    }

    next.g          = current.g + static_cast<float>(step_size_);
    next.parent_idx = -1;  // filled in by caller
    successors.push_back(next);
  }

  return successors;
}

// ─────────────────────────────────────────────────────────────────────────────
// tracePath
// ─────────────────────────────────────────────────────────────────────────────
nav_msgs::msg::Path HybridAStarPlanner::tracePath(
  const std::unordered_map<int, Node3D>& nodes,
  int goal_idx,
  const std::string& frame_id) const
{
  nav_msgs::msg::Path path;
  path.header.frame_id = frame_id;

  std::vector<const Node3D*> chain;
  int idx = goal_idx;
  while (idx != -1) {
    auto it = nodes.find(idx);
    if (it == nodes.end()) break;
    chain.push_back(&it->second);
    idx = it->second.parent_idx;
  }

  std::reverse(chain.begin(), chain.end());

  for (const Node3D* n : chain) {
    geometry_msgs::msg::PoseStamped ps;
    ps.header.frame_id = frame_id;
    ps.pose.position.x = static_cast<double>(n->x);
    ps.pose.position.y = static_cast<double>(n->y);
    ps.pose.position.z = 0.0;

    // Convert theta to quaternion (rotation about Z)
    double half = static_cast<double>(n->theta) * 0.5;
    ps.pose.orientation.z = std::sin(half);
    ps.pose.orientation.w = std::cos(half);

    path.poses.push_back(ps);
  }

  return path;
}

// ─────────────────────────────────────────────────────────────────────────────
// createPlan — Hybrid A* search
// ─────────────────────────────────────────────────────────────────────────────
nav_msgs::msg::Path HybridAStarPlanner::createPlan(
  const geometry_msgs::msg::PoseStamped& start,
  const geometry_msgs::msg::PoseStamped& goal)
{
  auto node = node_.lock();
  if (!node) {
    throw std::runtime_error("HybridAStarPlanner::createPlan: parent node expired");
  }

  const auto t0 = std::chrono::steady_clock::now();

  nav2_costmap_2d::Costmap2D* costmap = costmap_ros_->getCostmap();
  const std::string frame_id = start.header.frame_id;

  // ── Extract start/goal angles from quaternion ──
  auto quat_to_yaw = [](const geometry_msgs::msg::Quaternion& q) -> double {
    return std::atan2(
      2.0 * (q.w * q.z + q.x * q.y),
      1.0 - 2.0 * (q.y * q.y + q.z * q.z));
  };

  Node3D start_node, goal_node;
  start_node.x     = static_cast<float>(start.pose.position.x);
  start_node.y     = static_cast<float>(start.pose.position.y);
  start_node.theta = static_cast<float>(quat_to_yaw(start.pose.orientation));
  start_node.g     = 0.f;

  goal_node.x     = static_cast<float>(goal.pose.position.x);
  goal_node.y     = static_cast<float>(goal.pose.position.y);
  goal_node.theta = static_cast<float>(quat_to_yaw(goal.pose.orientation));

  // ── Discretize to grid cell ──
  auto toGrid = [&](float wx, float wy, float wth,
                    int& ix, int& iy, int& ith) -> bool {
    unsigned int ux = 0, uy = 0;
    if (!costmap->worldToMap(
          static_cast<double>(wx), static_cast<double>(wy), ux, uy)) {
      return false;
    }
    ix  = static_cast<int>(ux);
    iy  = static_cast<int>(uy);
    ith = static_cast<int>(
      (static_cast<double>(wth) + M_PI) / angle_resolution_) % num_angle_bins_;
    if (ith < 0) ith += num_angle_bins_;
    return true;
  };

  int s_ix, s_iy, s_ith, g_ix, g_iy, g_ith;
  if (!toGrid(start_node.x, start_node.y, start_node.theta,
              s_ix, s_iy, s_ith)) {
    RCLCPP_WARN(node->get_logger(), "[%s] Start pose is off the map", name_.c_str());
    return nav_msgs::msg::Path{};
  }
  if (!toGrid(goal_node.x, goal_node.y, goal_node.theta,
              g_ix, g_iy, g_ith)) {
    RCLCPP_WARN(node->get_logger(), "[%s] Goal pose is off the map", name_.c_str());
    return nav_msgs::msg::Path{};
  }

  int start_idx = nodeIndex(s_ix, s_iy, s_ith);
  int goal_idx  = nodeIndex(g_ix, g_iy, g_ith);

  // ── Priority queue (min-heap by f) ──
  using PQ = std::priority_queue<
    std::pair<float, int>,
    std::vector<std::pair<float, int>>,
    std::greater<std::pair<float, int>>>;

  PQ open_set;
  std::unordered_map<int, Node3D> all_nodes;

  start_node.h = static_cast<float>(heuristic(start_node, goal_node));
  start_node.f = start_node.g + start_node.h;
  all_nodes[start_idx] = start_node;
  open_set.push({start_node.f, start_idx});

  const double goal_tolerance = step_size_ * 1.5;  // metres
  const double goal_angle_tol = angle_resolution_ * 2.0;

  // ── RS curve short-cut parameters ──
  const double rs_trigger_dist = min_turning_radius_ * 3.0;

  int best_goal_idx = -1;

  while (!open_set.empty()) {
    // ── Time check ──
    double elapsed = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - t0).count();
    if (elapsed > max_planning_time_) {
      RCLCPP_WARN(node->get_logger(),
        "[%s] Planning time exceeded %.1f s", name_.c_str(), max_planning_time_);
      break;
    }

    auto [f_top, cur_idx] = open_set.top();
    open_set.pop();

    auto it = all_nodes.find(cur_idx);
    if (it == all_nodes.end()) continue;
    Node3D& current = it->second;
    if (!current.is_open) continue;
    current.is_open = false;

    // ── Goal check ──
    float dx = current.x - goal_node.x;
    float dy = current.y - goal_node.y;
    float dist_to_goal = std::sqrt(dx * dx + dy * dy);
    float dth = std::fabs(static_cast<float>(
      wrapAngle(static_cast<double>(current.theta - goal_node.theta))));

    if (dist_to_goal < static_cast<float>(goal_tolerance) &&
        dth < static_cast<float>(goal_angle_tol)) {
      best_goal_idx = cur_idx;
      break;
    }

    // ── RS curve short-cut when close to goal ──
    if (dist_to_goal < static_cast<float>(rs_trigger_dist)) {
      // Simple straight-line check from current to goal
      int steps = static_cast<int>(dist_to_goal / step_size_) + 1;
      bool clear = true;
      for (int s = 1; s <= steps; ++s) {
        float t  = static_cast<float>(s) / static_cast<float>(steps);
        float cx = current.x + t * (goal_node.x - current.x);
        float cy = current.y + t * (goal_node.y - current.y);
        if (isCollision(cx, cy, costmap)) { clear = false; break; }
      }
      if (clear) {
        // Insert goal node as child of current
        goal_node.g          = current.g + dist_to_goal;
        goal_node.h          = 0.f;
        goal_node.f          = goal_node.g;
        goal_node.parent_idx = cur_idx;
        goal_node.is_open    = false;
        all_nodes[goal_idx]  = goal_node;
        best_goal_idx = goal_idx;
        break;
      }
    }

    // ── Expand ──
    auto successors = expand(current, costmap);
    for (auto& succ : successors) {
      int s_ix2, s_iy2, s_ith2;
      if (!toGrid(succ.x, succ.y, succ.theta, s_ix2, s_iy2, s_ith2)) continue;
      int succ_idx = nodeIndex(s_ix2, s_iy2, s_ith2);

      succ.parent_idx = cur_idx;
      succ.h = static_cast<float>(heuristic(succ, goal_node));
      succ.f = succ.g + succ.h;

      auto existing = all_nodes.find(succ_idx);
      if (existing != all_nodes.end()) {
        if (!existing->second.is_open) continue;  // already closed
        if (succ.g >= existing->second.g)  continue;  // not cheaper
      }
      all_nodes[succ_idx] = succ;
      open_set.push({succ.f, succ_idx});
    }
  }

  if (best_goal_idx == -1) {
    RCLCPP_WARN(node->get_logger(),
      "[%s] Failed to find a plan from (%.2f,%.2f) to (%.2f,%.2f)",
      name_.c_str(),
      static_cast<double>(start_node.x), static_cast<double>(start_node.y),
      static_cast<double>(goal_node.x),  static_cast<double>(goal_node.y));
    return nav_msgs::msg::Path{};
  }

  nav_msgs::msg::Path path = tracePath(all_nodes, best_goal_idx, frame_id);
  path.header.stamp = node->now();

  RCLCPP_INFO(node->get_logger(),
    "[%s] Plan found with %zu waypoints",
    name_.c_str(), path.poses.size());

  return path;
}

}  // namespace module_2_planning

// ── pluginlib registration ──────────────────────────────────────────────────
#include <pluginlib/class_list_macros.hpp>
PLUGINLIB_EXPORT_CLASS(
  module_2_planning::HybridAStarPlanner,
  nav2_core::GlobalPlanner)
