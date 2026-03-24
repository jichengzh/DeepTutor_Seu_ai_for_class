#pragma once
#include <rclcpp/rclcpp.hpp>
#include <nav_msgs/msg/path.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <std_msgs/msg/string.hpp>
#include <vector>
#include <map>
#include <string>
#include <optional>

namespace module_5_multi_robot {

/// A contiguous segment of a path with associated time window.
struct PathSegment {
  int         robot_id{-1};
  int         segment_id{-1};
  std::vector<geometry_msgs::msg::PoseStamped> waypoints;
  double      t_start{0.0};   ///< Earliest entry time [s]
  double      t_end{0.0};     ///< Latest exit time    [s]
  double      duration{0.0};  ///< Expected travel time [s]
};

/// A reserved time window: robot occupies a spatial cell during [t_enter, t_exit].
struct TimeWindow {
  int    robot_id;
  double t_enter;   ///< [s]
  double t_exit;    ///< [s]
  double x;         ///< Cell centre x [m]
  double y;         ///< Cell centre y [m]
  double cell_size; ///< Side length of the occupancy cell [m]
};

/// Result of a RSTC pre-planning round for one robot.
struct RSTCPlan {
  int                      robot_id;
  std::vector<PathSegment> segments;
  bool                     feasible{true};
  std::string              infeasibility_reason{};
};

/**
 * @class RSTCPreplanner
 * @brief Reservation-based Space-Time Conflict (RSTC) pre-planner.
 *
 * Each robot submits its path; the preplanner assigns non-overlapping time
 * windows to path segments so that no two robots occupy the same cell at the
 * same time.  If conflicts are found, later robots are delayed by inserting
 * wait time at their segment start.
 *
 * Subscribes to:
 *   /robot_N/plan        (nav_msgs/Path)         — per-robot planned path
 *   /task_assignment     (std_msgs/String, JSON)  — used to derive priority order
 *
 * Publishes:
 *   /rstc/timed_plans    (std_msgs/String, JSON)  — timed path segments
 *   /rstc/conflicts      (std_msgs/String, JSON)  — detected conflicts before resolution
 *
 * Parameters:
 *   num_robots      (int,    default 3)
 *   robot_speed     (double, default 0.5)  [m/s]
 *   cell_size       (double, default 0.5)  [m]
 *   time_horizon    (double, default 60.0) [s]
 */
class RSTCPreplanner : public rclcpp::Node {
public:
  explicit RSTCPreplanner(const rclcpp::NodeOptions & options = rclcpp::NodeOptions{});

  /**
   * Run the full RSTC pre-planning algorithm.
   *
   * @param paths  One nav_msgs/Path per robot (index == robot_id).
   * @return       Timed RSTCPlan per robot, with feasibility flag.
   */
  std::vector<RSTCPlan> preplan(const std::vector<nav_msgs::msg::Path> & paths);

private:
  // ── Core algorithm ───────────────────────────────────────────────────────────

  /// Divide path into segments and compute nominal travel time per segment.
  std::vector<PathSegment> computeSegments(const nav_msgs::msg::Path & path,
                                           int robot_id) const;

  /// Build flat list of time windows from all robot segments.
  std::vector<TimeWindow> buildTimeWindows(
      const std::vector<std::vector<PathSegment>> & all_segments) const;

  /// Check whether two time windows overlap spatially and temporally.
  bool windowsConflict(const TimeWindow & a, const TimeWindow & b) const;

  /**
   * Resolve a conflict by delaying `lower_priority_robot_id`:
   * shift its segment start time forward until the overlap clears.
   * Returns the required delay in seconds.
   */
  double resolveConflict(PathSegment & seg,
                          const TimeWindow & blocker) const;

  /// Propagate a delay forward through all subsequent segments of the same robot.
  void propagateDelay(std::vector<PathSegment> & segments, int start_idx,
                       double delay) const;

  // ── ROS callbacks ────────────────────────────────────────────────────────────

  void pathCallback(const nav_msgs::msg::Path::SharedPtr msg, int robot_id);
  void assignmentCallback(const std_msgs::msg::String::SharedPtr msg);

  void publishTimedPlans(const std::vector<RSTCPlan> & plans);
  void runPreplannerIfReady();

  // ── Members ──────────────────────────────────────────────────────────────────

  std::map<int, nav_msgs::msg::Path> received_paths_;
  std::map<int, TimeWindow>          reserved_windows_;  // flat reservation table

  std::vector<rclcpp::Subscription<nav_msgs::msg::Path>::SharedPtr> path_subs_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr             assignment_sub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr                timed_plans_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr                conflicts_pub_;

  // ── Parameters ───────────────────────────────────────────────────────────────

  int    num_robots_{3};
  double robot_speed_{0.5};   // m/s
  double cell_size_{0.5};     // m
  double time_horizon_{60.0}; // s
};

}  // namespace module_5_multi_robot
