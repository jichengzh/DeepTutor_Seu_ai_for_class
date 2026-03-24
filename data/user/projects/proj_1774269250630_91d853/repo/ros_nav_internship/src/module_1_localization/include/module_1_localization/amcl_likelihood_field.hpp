#pragma once

/**
 * @file amcl_likelihood_field.hpp
 * @brief Adaptive Likelihood Field Model for AMCL-style localization.
 *
 * Implements the Likelihood Field Model (Thrun, Burgard, Fox – Probabilistic
 * Robotics, Ch.6.4) with an adaptive sigma_hit that varies based on local
 * map quality metrics.
 */

#include <memory>
#include <vector>
#include <cstdint>

#include <rclcpp/rclcpp.hpp>
#include <nav_msgs/msg/occupancy_grid.hpp>
#include <sensor_msgs/msg/laser_scan.hpp>
#include <geometry_msgs/msg/pose.hpp>

namespace module_1_localization {

/**
 * @class AdaptiveLikelihoodFieldModel
 * @brief Computes observation likelihood using the Likelihood Field Model
 *        with an adaptive hit-sigma derived from local map quality.
 *
 * Usage:
 *  1. Construct the node – it subscribes to /map automatically.
 *  2. Call computeObservationProb() from your particle filter update step.
 */
class AdaptiveLikelihoodFieldModel : public rclcpp::Node {
public:
  /**
   * @brief Construct the node and subscribe to the occupancy grid map.
   */
  explicit AdaptiveLikelihoodFieldModel(
    const rclcpp::NodeOptions & options = rclcpp::NodeOptions{});

  /// Default destructor.
  ~AdaptiveLikelihoodFieldModel() override = default;

  /**
   * @brief Compute the total observation log-likelihood for a given pose.
   *
   * @param scan  Laser scan message (range readings).
   * @param pose  Robot pose hypothesis in the map frame.
   * @return      Log-likelihood of the observation given the pose.
   */
  double computeObservationProb(
    const sensor_msgs::msg::LaserScan & scan,
    const geometry_msgs::msg::Pose & pose) const;

  /// @return true if the distance-transform field has been built.
  bool isFieldReady() const { return field_ready_; }

private:
  // ----------------------------------------------------------------
  // Map callbacks and field construction
  // ----------------------------------------------------------------

  /// Callback for incoming OccupancyGrid messages.
  void mapCallback(const nav_msgs::msg::OccupancyGrid::ConstSharedPtr & msg);

  /**
   * @brief Construct the distance transform from the occupancy grid.
   *
   * Uses a BFS-based exact distance transform.  Each cell stores the
   * Euclidean distance (in metres) to the nearest occupied cell.
   *
   * @param grid  Occupancy grid to process.
   */
  void buildField(const nav_msgs::msg::OccupancyGrid & grid);

  // ----------------------------------------------------------------
  // Probability helpers
  // ----------------------------------------------------------------

  /**
   * @brief Gaussian hit probability using adaptive sigma.
   *
   * @param dist_m  Distance to nearest obstacle in metres.
   * @param mx      Map cell x of the measurement endpoint.
   * @param my      Map cell y of the measurement endpoint.
   * @return        Hit probability in [0, 1].
   */
  double gaussHit(double dist_m, int mx, int my) const;

  /**
   * @brief Compute adaptive sigma_hit based on local map quality.
   *
   * Regions with sparse obstacle data get a larger sigma to improve
   * robustness against poor map coverage.
   *
   * @param mx  Map cell x.
   * @param my  Map cell y.
   * @return    Adaptive sigma in metres.
   */
  double adaptiveSigmaHit(int mx, int my) const;

  /**
   * @brief Convert world coordinates to map cell indices.
   *
   * @param wx  World x (metres).
   * @param wy  World y (metres).
   * @param mx  Output map cell x.
   * @param my  Output map cell y.
   * @return    true if the cell is within map bounds.
   */
  bool worldToMap(double wx, double wy, int & mx, int & my) const;

  /**
   * @brief Retrieve the precomputed distance at a map cell (metres).
   *
   * @return  Distance in metres, or max_obstacle_dist_ if out of bounds.
   */
  double distanceAt(int mx, int my) const;

  /**
   * @brief Compute a local density metric used by adaptiveSigmaHit.
   *
   * Counts occupied cells in a neighbourhood around (mx, my) and
   * returns a normalised value in [0, 1].
   *
   * @param mx          Cell x.
   * @param my          Cell y.
   * @param radius_cells Radius (in cells) of the neighbourhood.
   */
  double localOccupancyDensity(int mx, int my, int radius_cells) const;

  // ----------------------------------------------------------------
  // Parameter initialisation
  // ----------------------------------------------------------------
  void declareParameters();

  // ----------------------------------------------------------------
  // Map subscription
  // ----------------------------------------------------------------
  rclcpp::Subscription<nav_msgs::msg::OccupancyGrid>::SharedPtr map_sub_;

  // ----------------------------------------------------------------
  // Occupancy grid data
  // ----------------------------------------------------------------
  nav_msgs::msg::OccupancyGrid::ConstSharedPtr map_;  ///< Latest map.
  std::vector<double>  dist_field_;                   ///< Distance transform (metres).
  bool                 field_ready_{false};

  // ----------------------------------------------------------------
  // Parameters
  // ----------------------------------------------------------------
  double sigma_hit_{0.2};                ///< Baseline Gaussian std-dev (m).
  double z_hit_{0.5};                    ///< Weight for hit component.
  double z_rand_{0.5};                   ///< Weight for random component.
  double max_obstacle_dist_{2.0};        ///< Maximum distance stored in field (m).
  double laser_likelihood_max_dist_{2.0};
  int    max_beams_{60};                 ///< Max scan rays used per update.
  double adaptive_sigma_min_{0.1};       ///< Minimum adaptive sigma (m).
  double adaptive_sigma_max_{0.5};       ///< Maximum adaptive sigma (m).
  int    density_radius_cells_{5};       ///< Neighbourhood radius for density.
};

}  // namespace module_1_localization
