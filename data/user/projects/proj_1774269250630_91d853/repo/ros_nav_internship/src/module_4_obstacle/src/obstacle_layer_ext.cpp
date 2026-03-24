#include "module_4_obstacle/obstacle_layer_ext.hpp"

#include <nav2_costmap_2d/cost_values.hpp>
#include <pluginlib/class_list_macros.hpp>
#include <cmath>
#include <algorithm>

// Register as a costmap plugin so Nav2 can load it dynamically
PLUGINLIB_EXPORT_CLASS(module_4_obstacle::ObstacleLayerExt,
                       nav2_costmap_2d::Layer)

namespace module_4_obstacle {

// ─────────────────────────────────────────────────────────────────────────────
void ObstacleLayerExt::onInitialize()
{
  // Call parent initialisation first (sets up laser/PC sensor sources)
  nav2_costmap_2d::ObstacleLayer::onInitialize();

  // Load our own parameters
  auto node = node_.lock();
  if (!node) {
    throw std::runtime_error("ObstacleLayerExt: parent node expired during init");
  }

  declareParameter("reaction_time",       rclcpp::ParameterValue(reaction_time_));
  declareParameter("robot_radius",        rclcpp::ParameterValue(robot_radius_));
  declareParameter("v_relative_default",  rclcpp::ParameterValue(v_relative_default_));

  node->get_parameter(name_ + "." + "reaction_time",      reaction_time_);
  node->get_parameter(name_ + "." + "robot_radius",       robot_radius_);
  node->get_parameter(name_ + "." + "v_relative_default", v_relative_default_);

  RCLCPP_INFO(logger_,
    "ObstacleLayerExt: reaction_time=%.2f  robot_radius=%.2f  v_rel_default=%.2f",
    reaction_time_, robot_radius_, v_relative_default_);

  // Subscribe to tracked obstacles from obstacle_kf_tracker
  tracked_obs_sub_ =
    node->create_subscription<visualization_msgs::msg::MarkerArray>(
      "/tracked_obstacles",
      rclcpp::SystemDefaultsQoS(),
      std::bind(&ObstacleLayerExt::trackedObstaclesCallback,
                this, std::placeholders::_1));
}

// ─────────────────────────────────────────────────────────────────────────────
double ObstacleLayerExt::computeConeRadius(double v_relative,
                                            double reaction_time) const
{
  // Safety cone radius: distance travelled during reaction time + robot footprint
  return v_relative * reaction_time + robot_radius_;
}

// ─────────────────────────────────────────────────────────────────────────────
void ObstacleLayerExt::trackedObstaclesCallback(
    const visualization_msgs::msg::MarkerArray::SharedPtr msg)
{
  std::lock_guard<std::mutex> lock(obstacles_mutex_);
  latest_obstacles_ = msg;
}

// ─────────────────────────────────────────────────────────────────────────────
void ObstacleLayerExt::updateBounds(
    double robot_x, double robot_y, double robot_yaw,
    double* min_x, double* min_y, double* max_x, double* max_y)
{
  // Let the parent layer update bounds from sensor data first
  nav2_costmap_2d::ObstacleLayer::updateBounds(
      robot_x, robot_y, robot_yaw, min_x, min_y, max_x, max_y);

  std::lock_guard<std::mutex> lock(obstacles_mutex_);
  if (!latest_obstacles_) return;

  for (const auto& marker : latest_obstacles_->markers) {
    if (marker.action == visualization_msgs::msg::Marker::DELETE ||
        marker.action == visualization_msgs::msg::Marker::DELETEALL) {
      continue;
    }

    // Extract speed from marker scale z (convention used by kf_tracker)
    // or fall back to default
    const double v_rel = (marker.scale.z > 0.0)
                            ? marker.scale.z
                            : v_relative_default_;

    const double radius = computeConeRadius(v_rel, reaction_time_);

    const double ox = marker.pose.position.x;
    const double oy = marker.pose.position.y;

    *min_x = std::min(*min_x, ox - radius);
    *min_y = std::min(*min_y, oy - radius);
    *max_x = std::max(*max_x, ox + radius);
    *max_y = std::max(*max_y, oy + radius);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
void ObstacleLayerExt::updateCosts(
    nav2_costmap_2d::Costmap2D& master_grid,
    int min_i, int min_j, int max_i, int max_j)
{
  // Apply parent layer costs (static obstacles from sensors)
  nav2_costmap_2d::ObstacleLayer::updateCosts(
      master_grid, min_i, min_j, max_i, max_j);

  if (!enabled_) return;

  std::lock_guard<std::mutex> lock(obstacles_mutex_);
  if (!latest_obstacles_) return;

  const double resolution = master_grid.getResolution();
  unsigned int mx = 0, my = 0;

  for (const auto& marker : latest_obstacles_->markers) {
    if (marker.action == visualization_msgs::msg::Marker::DELETE ||
        marker.action == visualization_msgs::msg::Marker::DELETEALL) {
      continue;
    }

    const double ox = marker.pose.position.x;
    const double oy = marker.pose.position.y;

    const double v_rel = (marker.scale.z > 0.0)
                            ? marker.scale.z
                            : v_relative_default_;

    const double radius = computeConeRadius(v_rel, reaction_time_);

    // Iterate over a square bounding box, mark cells within the cone circle
    const int cells = static_cast<int>(std::ceil(radius / resolution));

    if (!master_grid.worldToMap(ox, oy, mx, my)) {
      continue;  // obstacle centre is outside the map
    }

    const int cx = static_cast<int>(mx);
    const int cy = static_cast<int>(my);

    for (int di = -cells; di <= cells; ++di) {
      for (int dj = -cells; dj <= cells; ++dj) {
        const int ni = cx + di;
        const int nj = cy + dj;

        // Clamp to update bounds
        if (ni < min_i || ni >= max_i || nj < min_j || nj >= max_j) continue;
        if (ni < 0 || nj < 0 ||
            ni >= static_cast<int>(master_grid.getSizeInCellsX()) ||
            nj >= static_cast<int>(master_grid.getSizeInCellsY())) {
          continue;
        }

        // Check radial distance
        const double dist = std::hypot(di * resolution, dj * resolution);
        if (dist <= radius) {
          const unsigned int cell_idx = master_grid.getIndex(ni, nj);
          const unsigned char current = master_grid.getCost(ni, nj);
          // Only raise cost, never lower it
          if (current < nav2_costmap_2d::LETHAL_OBSTACLE) {
            master_grid.setCost(ni, nj, nav2_costmap_2d::LETHAL_OBSTACLE);
            (void)cell_idx;  // suppress unused warning
          }
        }
      }
    }
  }
}

}  // namespace module_4_obstacle
