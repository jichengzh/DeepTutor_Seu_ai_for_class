#pragma once
#include <nav2_costmap_2d/obstacle_layer.hpp>
#include <visualization_msgs/msg/marker_array.hpp>
#include <rclcpp/rclcpp.hpp>
#include <mutex>

namespace module_4_obstacle {

/**
 * @brief Extended obstacle layer that inflates cost around tracked dynamic
 *        obstacles using a velocity-dependent collision cone.
 *
 * The cone radius is:
 *   r = v_relative * reaction_time + robot_radius
 *
 * All cells within radius r of a tracked obstacle centroid are marked with
 * LETHAL_OBSTACLE cost.  The layer falls back to the parent ObstacleLayer
 * behaviour for static sensor data.
 */
class ObstacleLayerExt : public nav2_costmap_2d::ObstacleLayer {
public:
  ObstacleLayerExt() = default;
  ~ObstacleLayerExt() override = default;

  // ── Costmap layer interface ───────────────────────────────────────────────
  void onInitialize() override;

  void updateBounds(double robot_x, double robot_y, double robot_yaw,
                    double* min_x, double* min_y,
                    double* max_x, double* max_y) override;

  void updateCosts(nav2_costmap_2d::Costmap2D& master_grid,
                   int min_i, int min_j,
                   int max_i, int max_j) override;

private:
  /**
   * @brief Compute the safety cone radius for a given relative speed.
   * @param v_relative    Relative speed between robot and obstacle [m/s].
   * @param reaction_time Driver/planner reaction time [s].
   * @return              Cone radius [m].
   */
  double computeConeRadius(double v_relative, double reaction_time) const;

  void trackedObstaclesCallback(
      const visualization_msgs::msg::MarkerArray::SharedPtr msg);

  rclcpp::Subscription<visualization_msgs::msg::MarkerArray>::SharedPtr
      tracked_obs_sub_;

  visualization_msgs::msg::MarkerArray::SharedPtr latest_obstacles_;
  mutable std::mutex obstacles_mutex_;

  double reaction_time_{0.5};       ///< seconds
  double robot_radius_{0.3};        ///< metres
  double v_relative_default_{1.0};  ///< m/s (used when velocity unknown)
};

}  // namespace module_4_obstacle
