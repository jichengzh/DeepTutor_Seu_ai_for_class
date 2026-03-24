/**
 * rstc_preplanner.cpp
 *
 * Reservation-based Space-Time Conflict (RSTC) pre-planner.
 *
 * Algorithm
 * ─────────
 * 1. For each robot, divide its path into fixed-length segments and compute
 *    expected travel time per segment based on robot_speed_.
 * 2. Build a flat list of (robot, cell, t_enter, t_exit) time-windows.
 * 3. Detect conflicts: two windows conflict if they share the same grid cell
 *    and their time intervals overlap.
 * 4. Resolve conflicts greedily (lower robot-id has priority): delay the
 *    lower-priority robot's segment start until the conflict clears, then
 *    propagate the delay to all subsequent segments.
 * 5. Publish the timed plans and any residual conflicts as JSON.
 */

#include "module_5_multi_robot/rstc_preplanner.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

namespace module_5_multi_robot {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

RSTCPreplanner::RSTCPreplanner(const rclcpp::NodeOptions & options)
: Node("rstc_preplanner", options)
{
  this->declare_parameter("num_robots",   num_robots_);
  this->declare_parameter("robot_speed",  robot_speed_);
  this->declare_parameter("cell_size",    cell_size_);
  this->declare_parameter("time_horizon", time_horizon_);

  num_robots_   = this->get_parameter("num_robots").as_int();
  robot_speed_  = this->get_parameter("robot_speed").as_double();
  cell_size_    = this->get_parameter("cell_size").as_double();
  time_horizon_ = this->get_parameter("time_horizon").as_double();

  // Per-robot path subscribers
  for (int i = 0; i < num_robots_; ++i) {
    std::string topic = "/robot_" + std::to_string(i) + "/plan";
    path_subs_.push_back(
        this->create_subscription<nav_msgs::msg::Path>(
            topic, rclcpp::QoS(10),
            [this, i](const nav_msgs::msg::Path::SharedPtr msg) {
              pathCallback(msg, i);
            }));
  }

  assignment_sub_ = this->create_subscription<std_msgs::msg::String>(
      "/task_assignment",
      rclcpp::QoS(10).reliable().transient_local(),
      std::bind(&RSTCPreplanner::assignmentCallback, this, std::placeholders::_1));

  timed_plans_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/rstc/timed_plans", rclcpp::QoS(10));

  conflicts_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/rstc/conflicts", rclcpp::QoS(10));

  RCLCPP_INFO(this->get_logger(),
              "RSTCPreplanner ready: %d robots, speed=%.2f m/s, cell=%.2f m",
              num_robots_, robot_speed_, cell_size_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Public: preplan
// ─────────────────────────────────────────────────────────────────────────────

std::vector<RSTCPlan> RSTCPreplanner::preplan(
    const std::vector<nav_msgs::msg::Path> & paths)
{
  // Step 1: compute segments per robot
  std::vector<std::vector<PathSegment>> all_segments(paths.size());
  for (size_t i = 0; i < paths.size(); ++i) {
    all_segments[i] = computeSegments(paths[i], static_cast<int>(i));
  }

  // Step 2: iterative conflict resolution (lower id = higher priority)
  bool conflict_found = true;
  int  max_passes     = 20;

  while (conflict_found && max_passes-- > 0) {
    conflict_found = false;

    // Build time window list from current segment timings
    auto windows = buildTimeWindows(all_segments);

    for (size_t wi = 0; wi < windows.size(); ++wi) {
      for (size_t wj = wi + 1; wj < windows.size(); ++wj) {
        if (windows[wi].robot_id == windows[wj].robot_id) continue;

        if (windowsConflict(windows[wi], windows[wj])) {
          conflict_found = true;

          // Determine which robot yields (higher id = lower priority)
          int priority_robot = std::min(windows[wi].robot_id, windows[wj].robot_id);
          int yield_robot    = std::max(windows[wi].robot_id, windows[wj].robot_id);
          const TimeWindow & blocker = (windows[wi].robot_id == priority_robot)
                                        ? windows[wi] : windows[wj];
          TimeWindow & yielder_win   = (windows[wi].robot_id == yield_robot)
                                        ? windows[wi] : windows[wj];

          // Find the segment in all_segments that corresponds to yielder_win
          auto & yield_segs = all_segments[yield_robot];
          for (size_t si = 0; si < yield_segs.size(); ++si) {
            // Match by overlap with the yielder window's time range
            if (yield_segs[si].t_start <= yielder_win.t_exit &&
                yield_segs[si].t_end   >= yielder_win.t_enter) {
              double delay = resolveConflict(yield_segs[si], blocker);
              propagateDelay(yield_segs, static_cast<int>(si), delay);
              (void)yielder_win;  // suppress warning; we modified yield_segs
              break;
            }
          }

          // Republish detected conflict
          std::ostringstream conf_oss;
          conf_oss << "{\"conflict\":{\"robot_a\":" << priority_robot
                   << ",\"robot_b\":" << yield_robot
                   << ",\"x\":" << blocker.x
                   << ",\"y\":" << blocker.y
                   << "}}";
          std_msgs::msg::String conf_msg;
          conf_msg.data = conf_oss.str();
          conflicts_pub_->publish(conf_msg);

          goto next_pass;  // rebuild windows from scratch
        }
      }
    }
    next_pass:;
  }

  // Step 3: build result
  std::vector<RSTCPlan> plans;
  for (size_t i = 0; i < all_segments.size(); ++i) {
    RSTCPlan plan;
    plan.robot_id = static_cast<int>(i);
    plan.segments = all_segments[i];
    // Check if any segment exceeds the time horizon
    for (const auto & seg : plan.segments) {
      if (seg.t_end > time_horizon_) {
        plan.feasible = false;
        plan.infeasibility_reason =
            "Segment exceeds time horizon (" + std::to_string(time_horizon_) + " s)";
        break;
      }
    }
    plans.push_back(plan);
  }

  return plans;
}

// ─────────────────────────────────────────────────────────────────────────────
// Private: computeSegments
// ─────────────────────────────────────────────────────────────────────────────

std::vector<PathSegment> RSTCPreplanner::computeSegments(
    const nav_msgs::msg::Path & path, int robot_id) const
{
  std::vector<PathSegment> segments;
  if (path.poses.empty()) return segments;

  // Group waypoints into segments of ~cell_size_ length
  PathSegment current;
  current.robot_id   = robot_id;
  current.segment_id = 0;
  current.t_start    = 0.0;

  double accumulated_dist = 0.0;

  for (size_t i = 0; i < path.poses.size(); ++i) {
    current.waypoints.push_back(path.poses[i]);

    if (i > 0) {
      double dx = path.poses[i].pose.position.x - path.poses[i-1].pose.position.x;
      double dy = path.poses[i].pose.position.y - path.poses[i-1].pose.position.y;
      accumulated_dist += std::sqrt(dx * dx + dy * dy);
    }

    bool last_point  = (i == path.poses.size() - 1);
    bool cell_full   = (accumulated_dist >= cell_size_);

    if (cell_full || last_point) {
      double travel_time  = accumulated_dist / robot_speed_;
      current.duration    = travel_time;
      current.t_end       = current.t_start + travel_time;

      segments.push_back(current);

      if (!last_point) {
        PathSegment next;
        next.robot_id   = robot_id;
        next.segment_id = current.segment_id + 1;
        next.t_start    = current.t_end;
        next.waypoints.push_back(path.poses[i]);  // overlap one waypoint
        current          = next;
        accumulated_dist = 0.0;
      }
    }
  }

  return segments;
}

// ─────────────────────────────────────────────────────────────────────────────
// Private: buildTimeWindows
// ─────────────────────────────────────────────────────────────────────────────

std::vector<TimeWindow> RSTCPreplanner::buildTimeWindows(
    const std::vector<std::vector<PathSegment>> & all_segments) const
{
  std::vector<TimeWindow> windows;

  for (const auto & segs : all_segments) {
    for (const auto & seg : segs) {
      if (seg.waypoints.empty()) continue;

      // Use centre of first waypoint as cell representative
      TimeWindow w;
      w.robot_id  = seg.robot_id;
      w.t_enter   = seg.t_start;
      w.t_exit    = seg.t_end;
      w.x         = seg.waypoints.front().pose.position.x;
      w.y         = seg.waypoints.front().pose.position.y;
      w.cell_size = cell_size_;
      windows.push_back(w);
    }
  }
  return windows;
}

// ─────────────────────────────────────────────────────────────────────────────
// Private: windowsConflict
// ─────────────────────────────────────────────────────────────────────────────

bool RSTCPreplanner::windowsConflict(const TimeWindow & a,
                                      const TimeWindow & b) const
{
  // Spatial: are they in the same cell?
  bool same_cell = (std::abs(a.x - b.x) < cell_size_) &&
                   (std::abs(a.y - b.y) < cell_size_);
  if (!same_cell) return false;

  // Temporal: do their intervals overlap?
  return (a.t_enter < b.t_exit) && (b.t_enter < a.t_exit);
}

// ─────────────────────────────────────────────────────────────────────────────
// Private: resolveConflict
// ─────────────────────────────────────────────────────────────────────────────

double RSTCPreplanner::resolveConflict(PathSegment &      seg,
                                        const TimeWindow & blocker) const
{
  // Delay the segment so it starts after the blocker vacates the cell
  double required_start = blocker.t_exit + 0.1;  // 100 ms safety buffer
  double delay          = 0.0;

  if (seg.t_start < required_start) {
    delay        = required_start - seg.t_start;
    seg.t_start += delay;
    seg.t_end   += delay;
  }
  return delay;
}

// ─────────────────────────────────────────────────────────────────────────────
// Private: propagateDelay
// ─────────────────────────────────────────────────────────────────────────────

void RSTCPreplanner::propagateDelay(std::vector<PathSegment> & segments,
                                     int start_idx, double delay) const
{
  for (size_t i = static_cast<size_t>(start_idx); i < segments.size(); ++i) {
    segments[i].t_start += delay;
    segments[i].t_end   += delay;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Private: pathCallback
// ─────────────────────────────────────────────────────────────────────────────

void RSTCPreplanner::pathCallback(
    const nav_msgs::msg::Path::SharedPtr msg, int robot_id)
{
  received_paths_[robot_id] = *msg;
  RCLCPP_INFO(this->get_logger(),
              "Robot %d: received path with %zu poses",
              robot_id, msg->poses.size());
  runPreplannerIfReady();
}

// ─────────────────────────────────────────────────────────────────────────────
// Private: assignmentCallback
// ─────────────────────────────────────────────────────────────────────────────

void RSTCPreplanner::assignmentCallback(const std_msgs::msg::String::SharedPtr /*msg*/)
{
  // Re-run preplanner when a new assignment arrives (paths may change)
  runPreplannerIfReady();
}

// ─────────────────────────────────────────────────────────────────────────────
// Private: runPreplannerIfReady
// ─────────────────────────────────────────────────────────────────────────────

void RSTCPreplanner::runPreplannerIfReady()
{
  if (static_cast<int>(received_paths_.size()) < num_robots_) return;

  std::vector<nav_msgs::msg::Path> paths;
  for (int i = 0; i < num_robots_; ++i) {
    paths.push_back(received_paths_.at(i));
  }

  auto plans = preplan(paths);
  publishTimedPlans(plans);
}

// ─────────────────────────────────────────────────────────────────────────────
// Private: publishTimedPlans
// ─────────────────────────────────────────────────────────────────────────────

void RSTCPreplanner::publishTimedPlans(const std::vector<RSTCPlan> & plans)
{
  std::ostringstream oss;
  oss << "{\"timed_plans\":[";

  bool first_plan = true;
  for (const auto & plan : plans) {
    if (!first_plan) oss << ",";
    first_plan = false;

    oss << "{\"robot_id\":" << plan.robot_id
        << ",\"feasible\":"  << (plan.feasible ? "true" : "false")
        << ",\"segments\":[";

    bool first_seg = true;
    for (const auto & seg : plan.segments) {
      if (!first_seg) oss << ",";
      first_seg = false;
      oss << "{\"id\":"       << seg.segment_id
          << ",\"t_start\":"  << seg.t_start
          << ",\"t_end\":"    << seg.t_end
          << ",\"duration\":" << seg.duration
          << "}";
    }
    oss << "]}";
  }
  oss << "]}";

  std_msgs::msg::String out;
  out.data = oss.str();
  timed_plans_pub_->publish(out);

  RCLCPP_INFO(this->get_logger(), "Published timed plans for %zu robots",
              plans.size());
}

}  // namespace module_5_multi_robot

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<module_5_multi_robot::RSTCPreplanner>());
  rclcpp::shutdown();
  return 0;
}
