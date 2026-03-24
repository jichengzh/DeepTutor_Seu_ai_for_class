#include "module_2_planning/trajectory_optimizer.hpp"

#include <cmath>
#include <stdexcept>

namespace module_2_planning {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
TrajectoryOptimizer::TrajectoryOptimizer(const rclcpp::NodeOptions& options)
: rclcpp::Node("trajectory_optimizer", options)
{
  declare_parameter("interpolation_density", rclcpp::ParameterValue(5));
  declare_parameter("smoothing_weight",      rclcpp::ParameterValue(0.8));
  declare_parameter("min_path_points",       rclcpp::ParameterValue(3));

  get_parameter("interpolation_density", interpolation_density_);
  get_parameter("smoothing_weight",      smoothing_weight_);
  get_parameter("min_path_points",       min_path_points_);

  raw_path_sub_ = create_subscription<nav_msgs::msg::Path>(
    "/raw_path", rclcpp::QoS(10),
    std::bind(&TrajectoryOptimizer::rawPathCallback, this, std::placeholders::_1));

  optimized_path_pub_ = create_publisher<nav_msgs::msg::Path>(
    "/optimized_path", rclcpp::QoS(10));

  RCLCPP_INFO(get_logger(),
    "TrajectoryOptimizer ready (density=%d weight=%.2f)",
    interpolation_density_, smoothing_weight_);
}

// ─────────────────────────────────────────────────────────────────────────────
// rawPathCallback
// ─────────────────────────────────────────────────────────────────────────────
void TrajectoryOptimizer::rawPathCallback(
  const nav_msgs::msg::Path::SharedPtr msg)
{
  if (static_cast<int>(msg->poses.size()) < min_path_points_) {
    RCLCPP_WARN(get_logger(),
      "Path too short (%zu poses < %d), publishing as-is",
      msg->poses.size(), min_path_points_);
    optimized_path_pub_->publish(*msg);
    return;
  }

  auto pts = pathToPoints(*msg);

  // B-spline smoothing
  auto smoothed = bsplineSmooth(pts, interpolation_density_);

  // Blend between raw and smoothed using smoothing_weight_
  // (allows partial smoothing to stay close to original path)
  if (std::fabs(smoothing_weight_ - 1.0) > 1e-6 && smoothed.size() == pts.size()) {
    for (size_t i = 0; i < pts.size(); ++i) {
      smoothed[i].first  = smoothing_weight_ * smoothed[i].first
                           + (1.0 - smoothing_weight_) * pts[i].first;
      smoothed[i].second = smoothing_weight_ * smoothed[i].second
                           + (1.0 - smoothing_weight_) * pts[i].second;
    }
  }

  nav_msgs::msg::Path out = pointsToPath(smoothed, msg->header.frame_id);
  out.header.stamp = now();
  optimized_path_pub_->publish(out);

  RCLCPP_DEBUG(get_logger(),
    "Optimized path: %zu -> %zu poses", msg->poses.size(), out.poses.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// bsplineSmooth
//
// Uniform cubic B-spline interpolation.
// Uses clamped knot vector so the curve passes through the first and last
// control points.
// ─────────────────────────────────────────────────────────────────────────────
std::vector<std::pair<double, double>> TrajectoryOptimizer::bsplineSmooth(
  const std::vector<std::pair<double, double>>& pts,
  int density) const
{
  int n = static_cast<int>(pts.size());
  if (n < 2) return pts;

  // Clamped knot vector for degree 3
  int degree = 3;
  int num_knots = n + degree + 1;
  std::vector<double> knots(static_cast<size_t>(num_knots), 0.0);

  // Clamp at both ends
  for (int i = 0; i <= degree; ++i)          knots[static_cast<size_t>(i)] = 0.0;
  for (int i = n; i < num_knots; ++i)        knots[static_cast<size_t>(i)] = 1.0;
  for (int i = degree + 1; i < n; ++i) {
    knots[static_cast<size_t>(i)] =
      static_cast<double>(i - degree) / static_cast<double>(n - degree);
  }

  int num_segments = n - 1;
  int total_points = num_segments * density + 1;
  std::vector<std::pair<double, double>> result;
  result.reserve(static_cast<size_t>(total_points));

  for (int seg = 0; seg < num_segments; ++seg) {
    for (int d = 0; d < density; ++d) {
      double t = (static_cast<double>(seg * density + d))
                 / static_cast<double>(total_points - 1);

      double sx = 0.0, sy = 0.0;
      for (int i = 0; i < n; ++i) {
        double b = bsplineBasis(i, degree, t, knots);
        sx += b * pts[static_cast<size_t>(i)].first;
        sy += b * pts[static_cast<size_t>(i)].second;
      }
      result.push_back({sx, sy});
    }
  }

  // Always include the last point exactly
  result.push_back(pts.back());
  return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// bsplineBasis — Cox-de Boor recursion
// ─────────────────────────────────────────────────────────────────────────────
double TrajectoryOptimizer::bsplineBasis(
  int i, int p, double t,
  const std::vector<double>& knots) const
{
  int n = static_cast<int>(knots.size()) - 1;
  if (p == 0) {
    // Handle special case at t=1.0: include last interval
    if (i == n - 1 && std::fabs(t - 1.0) < 1e-10) return 1.0;
    return (t >= knots[static_cast<size_t>(i)] &&
            t <  knots[static_cast<size_t>(i + 1)]) ? 1.0 : 0.0;
  }

  double left  = 0.0;
  double right = 0.0;

  double denom1 = knots[static_cast<size_t>(i + p)]
                  - knots[static_cast<size_t>(i)];
  if (denom1 > 1e-10) {
    left = (t - knots[static_cast<size_t>(i)]) / denom1
           * bsplineBasis(i, p - 1, t, knots);
  }

  double denom2 = knots[static_cast<size_t>(i + p + 1)]
                  - knots[static_cast<size_t>(i + 1)];
  if (denom2 > 1e-10) {
    right = (knots[static_cast<size_t>(i + p + 1)] - t) / denom2
            * bsplineBasis(i + 1, p - 1, t, knots);
  }

  return left + right;
}

// ─────────────────────────────────────────────────────────────────────────────
// pathToPoints
// ─────────────────────────────────────────────────────────────────────────────
std::vector<std::pair<double, double>> TrajectoryOptimizer::pathToPoints(
  const nav_msgs::msg::Path& path) const
{
  std::vector<std::pair<double, double>> pts;
  pts.reserve(path.poses.size());
  for (const auto& ps : path.poses) {
    pts.push_back({ps.pose.position.x, ps.pose.position.y});
  }
  return pts;
}

// ─────────────────────────────────────────────────────────────────────────────
// pointsToPath
// ─────────────────────────────────────────────────────────────────────────────
nav_msgs::msg::Path TrajectoryOptimizer::pointsToPath(
  const std::vector<std::pair<double, double>>& pts,
  const std::string& frame_id) const
{
  nav_msgs::msg::Path path;
  path.header.frame_id = frame_id;

  for (size_t i = 0; i < pts.size(); ++i) {
    geometry_msgs::msg::PoseStamped ps;
    ps.header.frame_id = frame_id;
    ps.pose.position.x = pts[i].first;
    ps.pose.position.y = pts[i].second;
    ps.pose.position.z = 0.0;

    // Heading from finite difference
    double yaw = 0.0;
    if (i + 1 < pts.size()) {
      double dx = pts[i + 1].first  - pts[i].first;
      double dy = pts[i + 1].second - pts[i].second;
      yaw = std::atan2(dy, dx);
    } else if (i > 0) {
      double dx = pts[i].first  - pts[i - 1].first;
      double dy = pts[i].second - pts[i - 1].second;
      yaw = std::atan2(dy, dx);
    }

    double half = yaw * 0.5;
    ps.pose.orientation.z = std::sin(half);
    ps.pose.orientation.w = std::cos(half);

    path.poses.push_back(ps);
  }
  return path;
}

}  // namespace module_2_planning

// ── main ────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<module_2_planning::TrajectoryOptimizer>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
