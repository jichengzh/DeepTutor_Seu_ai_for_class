#include "module_2_planning/dwb_predictor_plugin.hpp"

#include <nav2_util/node_utils.hpp>
#include <dwb_core/exceptions.hpp>

#include <cmath>
#include <mutex>

namespace module_2_planning {

// ─────────────────────────────────────────────────────────────────────────────
// onInit
// ─────────────────────────────────────────────────────────────────────────────
void DWBPredictorPlugin::onInit()
{
  auto node = node_.lock();
  if (!node) {
    throw std::runtime_error("DWBPredictorPlugin::onInit: node handle expired");
  }

  // Declare parameters
  nav2_util::declare_parameter_if_not_declared(
    node, dwb_plugin_name_ + ".collision_penalty",
    rclcpp::ParameterValue(1000.0));
  nav2_util::declare_parameter_if_not_declared(
    node, dwb_plugin_name_ + ".occupancy_threshold",
    rclcpp::ParameterValue(50));
  nav2_util::declare_parameter_if_not_declared(
    node, dwb_plugin_name_ + ".time_horizon",
    rclcpp::ParameterValue(2.0));

  node->get_parameter(dwb_plugin_name_ + ".collision_penalty",
                      collision_penalty_);
  node->get_parameter(dwb_plugin_name_ + ".occupancy_threshold",
                      occupancy_threshold_);
  node->get_parameter(dwb_plugin_name_ + ".time_horizon",
                      time_horizon_);

  name_ = dwb_plugin_name_;

  // Subscribe to occupancy predictions from DynamicObstaclePredictor
  occupancy_sub_ = node->create_subscription<nav_msgs::msg::OccupancyGrid>(
    "/predicted_occupancy", rclcpp::QoS(10),
    [this](const nav_msgs::msg::OccupancyGrid::SharedPtr msg) {
      std::lock_guard<std::mutex> lock(grid_mutex_);
      latest_grid_ = msg;
    });

  RCLCPP_INFO(node->get_logger(),
    "[%s] DWBPredictorPlugin initialised (penalty=%.0f threshold=%d)",
    name_.c_str(), collision_penalty_, occupancy_threshold_);
}

// ─────────────────────────────────────────────────────────────────────────────
// prepare — called once per control cycle before scoring trajectories
// ─────────────────────────────────────────────────────────────────────────────
void DWBPredictorPlugin::prepare(
  const geometry_msgs::msg::Pose2D& /*pose*/,
  const nav_2d_msgs::msg::Twist2D& /*velocity*/,
  const geometry_msgs::msg::Pose2D& /*goal*/,
  const nav_2d_msgs::msg::Path2D& /*global_plan*/)
{
  // The subscriber callback keeps latest_grid_ updated; nothing extra needed.
}

// ─────────────────────────────────────────────────────────────────────────────
// getOccupancy
// ─────────────────────────────────────────────────────────────────────────────
int DWBPredictorPlugin::getOccupancy(double wx, double wy) const
{
  std::lock_guard<std::mutex> lock(grid_mutex_);
  if (!latest_grid_) return 0;

  const auto& info = latest_grid_->info;
  double ox = info.origin.position.x;
  double oy = info.origin.position.y;

  int gx = static_cast<int>((wx - ox) / info.resolution);
  int gy = static_cast<int>((wy - oy) / info.resolution);

  if (gx < 0 || gy < 0 ||
      gx >= static_cast<int>(info.width) ||
      gy >= static_cast<int>(info.height)) {
    return 0;  // outside grid → assume free
  }

  int idx = gy * static_cast<int>(info.width) + gx;
  return static_cast<int>(latest_grid_->data[static_cast<size_t>(idx)]);
}

// ─────────────────────────────────────────────────────────────────────────────
// scoreTrajectory
// ─────────────────────────────────────────────────────────────────────────────
double DWBPredictorPlugin::scoreTrajectory(
  const dwb_msgs::msg::Trajectory2D& traj)
{
  if (traj.poses.empty()) return 0.0;

  double total_penalty = 0.0;

  for (const auto& pose : traj.poses) {
    double wx = pose.x;
    double wy = pose.y;

    int occ = getOccupancy(wx, wy);
    if (occ < 0) {
      // Off-grid: treat as obstacle
      return collision_penalty_ * static_cast<double>(traj.poses.size());
    }
    if (occ >= occupancy_threshold_) {
      total_penalty += collision_penalty_
                       * (static_cast<double>(occ) / 100.0);
    }
  }

  return total_penalty;
}

}  // namespace module_2_planning

// ── pluginlib registration ──────────────────────────────────────────────────
#include <pluginlib/class_list_macros.hpp>
PLUGINLIB_EXPORT_CLASS(
  module_2_planning::DWBPredictorPlugin,
  dwb_core::TrajectoryCritic)
