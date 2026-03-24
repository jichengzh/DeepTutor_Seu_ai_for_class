#pragma once

#include <rclcpp/rclcpp.hpp>
#include <nav_msgs/msg/occupancy_grid.hpp>
#include <visualization_msgs/msg/marker_array.hpp>
#include <geometry_msgs/msg/point.hpp>

#include <array>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace module_2_planning {

// ──────────────────────────────────────────────────────────────────────────────
// KalmanState — constant-velocity Kalman filter state per obstacle
//   State vector: [x, y, vx, vy]
// ──────────────────────────────────────────────────────────────────────────────
struct KalmanState {
  // State mean: x, y, vx, vy
  std::array<double, 4> x{0.0, 0.0, 0.0, 0.0};

  // 4x4 covariance matrix (row-major)
  std::array<double, 16> P{
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1
  };

  // Process noise Q (diagonal variances for [x,y,vx,vy])
  double q_pos{0.01};  ///< position process noise variance
  double q_vel{0.1};   ///< velocity process noise variance

  // Measurement noise R (diagonal variances for [x,y])
  double r_pos{0.1};   ///< position measurement noise variance

  rclcpp::Time last_update;
  bool initialized{false};
};

// ──────────────────────────────────────────────────────────────────────────────
// PredictedPosition — a single obstacle's expected position at future time
// ──────────────────────────────────────────────────────────────────────────────
struct PredictedPosition {
  double x{0.0};
  double y{0.0};
  double uncertainty_radius{0.5};  ///< 1-sigma covariance ellipse approximation
};

// ──────────────────────────────────────────────────────────────────────────────
// DynamicObstaclePredictor
//
//  Subscribes to /tracked_obstacles (MarkerArray), runs a constant-velocity
//  Kalman filter per tracked ID, predicts positions N steps forward and
//  publishes the combined occupancy on /predicted_occupancy (OccupancyGrid).
// ──────────────────────────────────────────────────────────────────────────────
class DynamicObstaclePredictor : public rclcpp::Node {
public:
  explicit DynamicObstaclePredictor(
    const rclcpp::NodeOptions& options = rclcpp::NodeOptions());

  ~DynamicObstaclePredictor() override = default;

  /// Predict obstacle states N dt-steps into the future.
  /// Returns a map from obstacle ID → list of predicted positions (one per step).
  std::unordered_map<int, std::vector<PredictedPosition>>
  predict(int steps, double dt) const;

  /// Access the latest occupancy grid (thread-safe copy).
  nav_msgs::msg::OccupancyGrid getOccupancyGrid() const;

private:
  // ── Kalman filter operations ────────────────────────────────────────────────
  /// Propagate state forward by dt seconds (constant-velocity model).
  void propagate(KalmanState& state, double dt) const;

  /// Update state with a new (x, y) measurement.
  void update(KalmanState& state, double meas_x, double meas_y) const;

  // ── ROS callbacks ───────────────────────────────────────────────────────────
  void obstacleCallback(
    const visualization_msgs::msg::MarkerArray::SharedPtr msg);

  /// Rebuild and publish the occupancy grid from current predictions.
  void publishOccupancy();

  // ── Member variables ────────────────────────────────────────────────────────
  rclcpp::Subscription<visualization_msgs::msg::MarkerArray>::SharedPtr obstacle_sub_;
  rclcpp::Publisher<nav_msgs::msg::OccupancyGrid>::SharedPtr             occupancy_pub_;
  rclcpp::TimerBase::SharedPtr                                            publish_timer_;

  mutable std::mutex                           state_mutex_;
  std::unordered_map<int, KalmanState>         obstacle_states_;
  nav_msgs::msg::OccupancyGrid                 latest_grid_;

  // Parameters
  int    prediction_steps_{10};
  double prediction_dt_{0.1};      ///< seconds per prediction step
  double grid_resolution_{0.1};    ///< metres per cell
  double grid_half_width_{10.0};   ///< half-width of occupancy grid (m)
  std::string fixed_frame_{"map"};
};

}  // namespace module_2_planning
