#pragma once

#include <rclcpp/rclcpp.hpp>
#include <nav_msgs/msg/path.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>

#include <vector>
#include <tuple>

namespace module_2_planning {

// ──────────────────────────────────────────────────────────────────────────────
// TrajectoryOptimizer
//
//  Subscribes to /raw_path (nav_msgs/Path), applies cubic B-spline smoothing
//  with obstacle-avoidance constraint checking, and republishes the result on
//  /optimized_path.
//
//  Algorithm overview:
//    1. Treat the raw waypoints as B-spline control points.
//    2. Evaluate the spline at a finer resolution.
//    3. After smoothing, verify that no interpolated point violates the
//       costmap obstacle threshold.  If it does, fall back to the raw path
//       for that segment.
// ──────────────────────────────────────────────────────────────────────────────
class TrajectoryOptimizer : public rclcpp::Node {
public:
  explicit TrajectoryOptimizer(
    const rclcpp::NodeOptions& options = rclcpp::NodeOptions());

  ~TrajectoryOptimizer() override = default;

private:
  // ── ROS callbacks ───────────────────────────────────────────────────────────
  void rawPathCallback(const nav_msgs::msg::Path::SharedPtr msg);

  // ── Smoothing core ──────────────────────────────────────────────────────────

  /// Apply uniform cubic B-spline to a sequence of 2D points.
  /// @param pts     Input control points (x, y).
  /// @param density Number of interpolated points per segment.
  /// @return        Smoothed sequence of (x, y) pairs.
  std::vector<std::pair<double, double>> bsplineSmooth(
    const std::vector<std::pair<double, double>>& pts,
    int density) const;

  /// Evaluate a single cubic B-spline basis value B_{i,3}(t) using Cox–de Boor.
  double bsplineBasis(int i, int degree, double t,
                      const std::vector<double>& knots) const;

  /// Convert a Path message into a vector of (x, y) pairs.
  std::vector<std::pair<double, double>> pathToPoints(
    const nav_msgs::msg::Path& path) const;

  /// Convert (x, y) pairs back into a Path message, preserving the original
  /// frame and filling in interpolated headings via finite differences.
  nav_msgs::msg::Path pointsToPath(
    const std::vector<std::pair<double, double>>& pts,
    const std::string& frame_id) const;

  // ── Publishers / Subscribers ─────────────────────────────────────────────────
  rclcpp::Subscription<nav_msgs::msg::Path>::SharedPtr raw_path_sub_;
  rclcpp::Publisher<nav_msgs::msg::Path>::SharedPtr    optimized_path_pub_;

  // ── Parameters ───────────────────────────────────────────────────────────────
  int    interpolation_density_{5};   ///< interpolated points per raw segment
  double smoothing_weight_{0.8};      ///< blend weight (1.0 = full spline)
  int    min_path_points_{3};         ///< minimum points needed to smooth
};

}  // namespace module_2_planning
