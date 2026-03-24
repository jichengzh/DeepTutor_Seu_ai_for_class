/**
 * @file amcl_likelihood_field.cpp
 * @brief Implementation of AdaptiveLikelihoodFieldModel.
 *
 * Implements the Likelihood Field Model (Thrun, Burgard, Fox – Probabilistic
 * Robotics, Section 6.4) with:
 *  - BFS-based exact Euclidean distance transform from occupancy grid.
 *  - Adaptive sigma_hit computed from local occupancy density.
 *  - Standard z_hit (Gaussian) + z_rand (uniform) mixture.
 */

#include "module_1_localization/amcl_likelihood_field.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>

namespace module_1_localization {

// ============================================================
// Constructor
// ============================================================

AdaptiveLikelihoodFieldModel::AdaptiveLikelihoodFieldModel(
  const rclcpp::NodeOptions & options)
: rclcpp::Node("amcl_likelihood_field", options)
{
  declareParameters();

  sigma_hit_                 = get_parameter("sigma_hit").as_double();
  z_hit_                     = get_parameter("z_hit").as_double();
  z_rand_                    = get_parameter("z_rand").as_double();
  max_obstacle_dist_         = get_parameter("max_obstacle_dist").as_double();
  laser_likelihood_max_dist_ = get_parameter("laser_likelihood_max_dist").as_double();
  max_beams_                 = static_cast<int>(get_parameter("max_beams").as_int());
  adaptive_sigma_min_        = get_parameter("adaptive_sigma_min").as_double();
  adaptive_sigma_max_        = get_parameter("adaptive_sigma_max").as_double();
  density_radius_cells_      = static_cast<int>(get_parameter("density_radius_cells").as_int());

  // Subscribe to the map
  map_sub_ = create_subscription<nav_msgs::msg::OccupancyGrid>(
    "map", rclcpp::QoS(rclcpp::KeepLast(1)).transient_local().reliable(),
    std::bind(&AdaptiveLikelihoodFieldModel::mapCallback, this,
              std::placeholders::_1));

  RCLCPP_INFO(get_logger(),
    "AdaptiveLikelihoodFieldModel: sigma_hit=%.2f z_hit=%.2f z_rand=%.2f "
    "max_dist=%.1f max_beams=%d",
    sigma_hit_, z_hit_, z_rand_, max_obstacle_dist_, max_beams_);
}

// ============================================================
// declareParameters
// ============================================================

void AdaptiveLikelihoodFieldModel::declareParameters()
{
  declare_parameter<double>("sigma_hit",                 0.2);
  declare_parameter<double>("z_hit",                     0.5);
  declare_parameter<double>("z_rand",                    0.5);
  declare_parameter<double>("max_obstacle_dist",         2.0);
  declare_parameter<double>("laser_likelihood_max_dist", 2.0);
  declare_parameter<int>   ("max_beams",                 60);
  declare_parameter<double>("adaptive_sigma_min",        0.1);
  declare_parameter<double>("adaptive_sigma_max",        0.5);
  declare_parameter<int>   ("density_radius_cells",      5);
}

// ============================================================
// mapCallback
// ============================================================

void AdaptiveLikelihoodFieldModel::mapCallback(
  const nav_msgs::msg::OccupancyGrid::ConstSharedPtr & msg)
{
  RCLCPP_INFO(get_logger(),
    "AdaptiveLikelihoodFieldModel: received map %ux%u res=%.3f m/cell",
    msg->info.width, msg->info.height, msg->info.resolution);

  map_ = msg;
  buildField(*msg);

  RCLCPP_INFO(get_logger(),
    "AdaptiveLikelihoodFieldModel: distance field ready.");
}

// ============================================================
// buildField  – BFS-based exact distance transform
// ============================================================

void AdaptiveLikelihoodFieldModel::buildField(
  const nav_msgs::msg::OccupancyGrid & grid)
{
  const int W = static_cast<int>(grid.info.width);
  const int H = static_cast<int>(grid.info.height);
  const double res = grid.info.resolution;
  const int N = W * H;

  // Initialise distance field to a large value
  dist_field_.assign(static_cast<size_t>(N), max_obstacle_dist_);

  // BFS queue contains (cell_x, cell_y)
  std::queue<std::pair<int,int>> queue;

  // Seed BFS with all occupied cells (value >= 65 in OccupancyGrid)
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      const int idx = y * W + x;
      const int8_t occ = grid.data[static_cast<size_t>(idx)];
      if (occ >= 65) {           // occupied
        dist_field_[static_cast<size_t>(idx)] = 0.0;
        queue.push({x, y});
      }
    }
  }

  // 4-connected BFS
  const int dx[4] = { 1, -1,  0,  0};
  const int dy[4] = { 0,  0,  1, -1};

  while (!queue.empty()) {
    const auto [cx, cy] = queue.front();
    queue.pop();

    const double cur_dist = dist_field_[static_cast<size_t>(cy * W + cx)];

    for (int d = 0; d < 4; ++d) {
      const int nx = cx + dx[d];
      const int ny = cy + dy[d];
      if (nx < 0 || nx >= W || ny < 0 || ny >= H) { continue; }

      const size_t nidx = static_cast<size_t>(ny * W + nx);
      const double new_dist = cur_dist + res;

      if (new_dist < dist_field_[nidx]) {
        dist_field_[nidx] = new_dist;
        queue.push({nx, ny});
      }
    }
  }

  // Clamp to max_obstacle_dist_
  for (auto & d : dist_field_) {
    d = std::min(d, max_obstacle_dist_);
  }

  field_ready_ = true;
}

// ============================================================
// worldToMap
// ============================================================

bool AdaptiveLikelihoodFieldModel::worldToMap(
  double wx, double wy, int & mx, int & my) const
{
  if (!map_) { return false; }

  const auto & info = map_->info;
  const double ox = info.origin.position.x;
  const double oy = info.origin.position.y;
  const double res = info.resolution;

  mx = static_cast<int>((wx - ox) / res);
  my = static_cast<int>((wy - oy) / res);

  const int W = static_cast<int>(info.width);
  const int H = static_cast<int>(info.height);

  return (mx >= 0 && mx < W && my >= 0 && my < H);
}

// ============================================================
// distanceAt
// ============================================================

double AdaptiveLikelihoodFieldModel::distanceAt(int mx, int my) const
{
  if (!field_ready_ || !map_) { return max_obstacle_dist_; }

  const int W = static_cast<int>(map_->info.width);
  const int H = static_cast<int>(map_->info.height);

  if (mx < 0 || mx >= W || my < 0 || my >= H) {
    return max_obstacle_dist_;
  }

  return dist_field_[static_cast<size_t>(my * W + mx)];
}

// ============================================================
// localOccupancyDensity
// ============================================================

double AdaptiveLikelihoodFieldModel::localOccupancyDensity(
  int mx, int my, int radius_cells) const
{
  if (!map_) { return 0.0; }

  const int W = static_cast<int>(map_->info.width);
  const int H = static_cast<int>(map_->info.height);

  int occupied = 0;
  int total    = 0;

  for (int dy = -radius_cells; dy <= radius_cells; ++dy) {
    for (int dx = -radius_cells; dx <= radius_cells; ++dx) {
      const int nx = mx + dx;
      const int ny = my + dy;
      if (nx < 0 || nx >= W || ny < 0 || ny >= H) { continue; }

      ++total;
      const int8_t occ = map_->data[static_cast<size_t>(ny * W + nx)];
      if (occ >= 65) { ++occupied; }
    }
  }

  if (total == 0) { return 0.0; }
  return static_cast<double>(occupied) / static_cast<double>(total);
}

// ============================================================
// adaptiveSigmaHit
// ============================================================

double AdaptiveLikelihoodFieldModel::adaptiveSigmaHit(int mx, int my) const
{
  // High density → well-mapped area → use smaller sigma (tighter likelihood).
  // Low  density → sparse area     → use larger  sigma (more permissive).
  const double density = localOccupancyDensity(mx, my, density_radius_cells_);

  // Linear interpolation: density=1.0 → sigma_min, density=0.0 → sigma_max
  return adaptive_sigma_max_ - density * (adaptive_sigma_max_ - adaptive_sigma_min_);
}

// ============================================================
// gaussHit
// ============================================================

double AdaptiveLikelihoodFieldModel::gaussHit(
  double dist_m, int mx, int my) const
{
  const double sigma = adaptiveSigmaHit(mx, my);
  const double exponent = -(dist_m * dist_m) / (2.0 * sigma * sigma);
  return std::exp(exponent) / (sigma * std::sqrt(2.0 * M_PI));
}

// ============================================================
// computeObservationProb
// ============================================================

double AdaptiveLikelihoodFieldModel::computeObservationProb(
  const sensor_msgs::msg::LaserScan & scan,
  const geometry_msgs::msg::Pose &    pose) const
{
  if (!field_ready_) {
    RCLCPP_WARN_ONCE(get_logger(),
      "AdaptiveLikelihoodFieldModel: field not ready, returning 0.");
    return 0.0;
  }

  const double px = pose.position.x;
  const double py = pose.position.y;

  // Extract yaw from quaternion
  const double qw = pose.orientation.w;
  const double qz = pose.orientation.z;
  const double robot_yaw = 2.0 * std::atan2(qz, qw);

  const double angle_min = scan.angle_min;
  const double angle_inc = scan.angle_increment;
  const double range_max = scan.range_max;
  const double range_min = scan.range_min;

  const int num_readings = static_cast<int>(scan.ranges.size());

  // Stride to stay within max_beams_
  const int step = std::max(1, num_readings / max_beams_);

  double log_prob = 0.0;
  int    used     = 0;

  for (int i = 0; i < num_readings; i += step) {
    const double r = scan.ranges[static_cast<size_t>(i)];

    // Skip invalid readings
    if (!std::isfinite(r) || r < range_min || r > range_max) { continue; }

    // Compute endpoint in world frame
    const double beam_angle = robot_yaw + angle_min + static_cast<double>(i) * angle_inc;
    const double ex = px + r * std::cos(beam_angle);
    const double ey = py + r * std::sin(beam_angle);

    int mx = 0, my = 0;
    if (!worldToMap(ex, ey, mx, my)) {
      // Endpoint outside map – use max distance
      const double d = max_obstacle_dist_;
      const double p = z_hit_ * gaussHit(d, mx, my) +
                       z_rand_ / range_max;
      log_prob += std::log(std::max(p, 1e-300));
      ++used;
      continue;
    }

    const double dist = distanceAt(mx, my);
    const double p    = z_hit_ * gaussHit(dist, mx, my) +
                        z_rand_ / range_max;

    log_prob += std::log(std::max(p, 1e-300));
    ++used;
  }

  if (used == 0) { return 0.0; }

  // Return average log-likelihood (normalised by beam count)
  return log_prob / static_cast<double>(used);
}

}  // namespace module_1_localization

// ============================================================
// main
// ============================================================
#include <rclcpp/rclcpp.hpp>

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(
    std::make_shared<module_1_localization::AdaptiveLikelihoodFieldModel>());
  rclcpp::shutdown();
  return 0;
}
