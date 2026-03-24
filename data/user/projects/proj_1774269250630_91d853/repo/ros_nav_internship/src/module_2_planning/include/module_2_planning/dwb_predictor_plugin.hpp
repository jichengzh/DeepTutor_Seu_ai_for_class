#pragma once

#include <dwb_core/trajectory_critic.hpp>
#include <nav_msgs/msg/occupancy_grid.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>

#include <memory>
#include <string>

namespace module_2_planning {

// ──────────────────────────────────────────────────────────────────────────────
// DWBPredictorPlugin
//
//  A DWB TrajectoryCritic that penalises trajectories intersecting with cells
//  of a predicted obstacle occupancy grid.  The occupancy grid is obtained from
//  the /predicted_occupancy topic published by DynamicObstaclePredictor.
// ──────────────────────────────────────────────────────────────────────────────
class DWBPredictorPlugin : public dwb_core::TrajectoryCritic {
public:
  DWBPredictorPlugin() = default;
  ~DWBPredictorPlugin() override = default;

  // ── dwb_core::TrajectoryCritic interface ────────────────────────────────────

  /// Called once when the critic is loaded.
  void onInit() override;

  /// Called at the start of each control cycle with the current robot pose and
  /// velocity so that the critic can cache any state it needs.
  void prepare(
    const geometry_msgs::msg::Pose2D& pose,
    const nav_2d_msgs::msg::Twist2D& velocity,
    const geometry_msgs::msg::Pose2D& goal,
    const nav_2d_msgs::msg::Path2D& global_plan) override;

  /// Score a single candidate trajectory.
  /// Returns a non-negative penalty; higher is worse.
  double scoreTrajectory(const dwb_msgs::msg::Trajectory2D& traj) override;

  /// Name accessor (required by some DWB implementations).
  std::string getName() override { return name_; }

private:
  /// Fetch the latest occupancy grid from the predictor node.
  void updateOccupancy();

  /// Return the occupancy value [0..100] at world position (wx, wy).
  /// Returns -1 if the point is outside the grid.
  int getOccupancy(double wx, double wy) const;

  // ── Members ──────────────────────────────────────────────────────────────────
  rclcpp::Subscription<nav_msgs::msg::OccupancyGrid>::SharedPtr occupancy_sub_;
  nav_msgs::msg::OccupancyGrid::SharedPtr                       latest_grid_;
  std::mutex                                                     grid_mutex_;

  // Parameters
  std::string name_{"DWBPredictorPlugin"};
  double      collision_penalty_{1000.0};  ///< score added per collision cell
  int         occupancy_threshold_{50};    ///< cells above this are "occupied"
  double      time_horizon_{2.0};          ///< seconds ahead to check
};

}  // namespace module_2_planning
