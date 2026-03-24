#pragma once
#include <nav2_core/global_planner.hpp>
#include <nav2_costmap_2d/costmap_2d_ros.hpp>
#include <nav_msgs/msg/path.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <tf2_ros/buffer.h>
#include <vector>
#include <queue>
#include <unordered_map>

namespace module_2_planning {

// ──────────────────────────────────────────────────────────────────────────────
// Node3D — a single state in the (x, y, theta) search space
// ──────────────────────────────────────────────────────────────────────────────
struct Node3D {
  float x{0.f}, y{0.f}, theta{0.f};  // continuous grid coordinates + heading
  float g{0.f}, h{0.f}, f{0.f};      // path cost, heuristic, total
  int   parent_idx{-1};
  bool  is_open{true};

  bool operator>(const Node3D& other) const { return f > other.f; }
};

// ──────────────────────────────────────────────────────────────────────────────
// LFMConfig — Likelihood Field Model sensor params (used in cost scoring)
// ──────────────────────────────────────────────────────────────────────────────
struct LFMConfig {
  double sigma_hit{0.2};
  double z_hit{0.9};
  double z_rand{0.1};
};

// ──────────────────────────────────────────────────────────────────────────────
// VelocityLimits — kinematic envelope for the vehicle
// ──────────────────────────────────────────────────────────────────────────────
struct VelocityLimits {
  double v_max_linear{1.0};
  double omega_max{1.0};
  double a_max{0.5};
};

// ──────────────────────────────────────────────────────────────────────────────
// HybridAStarPlanner — nav2_core::GlobalPlanner plugin
// ──────────────────────────────────────────────────────────────────────────────
class HybridAStarPlanner : public nav2_core::GlobalPlanner {
public:
  HybridAStarPlanner() = default;
  ~HybridAStarPlanner() override = default;

  // ── nav2_core::GlobalPlanner interface ─────────────────────────────────────
  void configure(
    const rclcpp_lifecycle::LifecycleNode::WeakPtr& parent,
    std::string name,
    std::shared_ptr<tf2_ros::Buffer> tf,
    std::shared_ptr<nav2_costmap_2d::Costmap2DROS> costmap_ros) override;

  void cleanup()    override;
  void activate()   override;
  void deactivate() override;

  nav_msgs::msg::Path createPlan(
    const geometry_msgs::msg::PoseStamped& start,
    const geometry_msgs::msg::PoseStamped& goal) override;

private:
  // ── Search helpers ──────────────────────────────────────────────────────────
  /// Expand motion primitives from current node.
  std::vector<Node3D> expand(
    const Node3D& current,
    const nav2_costmap_2d::Costmap2D* costmap) const;

  /// Combined heuristic: max(euclidean, dubins_distance).
  double heuristic(const Node3D& node, const Node3D& goal) const;

  /// Dubins curve distance lower bound.
  double dubinsHeuristic(const Node3D& from, const Node3D& to) const;

  /// Reconstruct path from closed-set map.
  nav_msgs::msg::Path tracePath(
    const std::unordered_map<int, Node3D>& nodes,
    int goal_idx,
    const std::string& frame_id) const;

  /// Encode (ix, iy, ith) into a single integer key.
  int nodeIndex(int ix, int iy, int ith) const;

  /// Return true when the footprint at (x, y) intersects an obstacle.
  bool isCollision(
    float x, float y,
    const nav2_costmap_2d::Costmap2D* costmap) const;

  // ── Lifecycle members ───────────────────────────────────────────────────────
  rclcpp_lifecycle::LifecycleNode::WeakPtr          node_;
  std::shared_ptr<tf2_ros::Buffer>                  tf_;
  std::shared_ptr<nav2_costmap_2d::Costmap2DROS>    costmap_ros_;
  std::string                                        name_;

  // ── Parameters ──────────────────────────────────────────────────────────────
  double min_turning_radius_{0.5};     ///< minimum turning radius (m)
  double angle_resolution_{0.0873};    ///< ~5 degrees per bin
  int    num_angle_bins_{72};          ///< 360 / 5
  double max_planning_time_{5.0};      ///< hard deadline (seconds)
  double step_size_{0.5};              ///< arc length per expansion step (m)
  int    num_steering_angles_{3};      ///< number of steering angles (each side)
  double obstacle_cost_threshold_{200.0}; ///< costmap lethal threshold
};

}  // namespace module_2_planning
