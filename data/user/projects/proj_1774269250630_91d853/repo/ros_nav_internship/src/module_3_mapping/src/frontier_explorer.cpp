/**
 * frontier_explorer.cpp
 *
 * Implements the Wavefront Frontier Detector (WFD) algorithm for autonomous
 * map exploration in ROS 2.  The node:
 *   1. Subscribes to /map (nav_msgs/OccupancyGrid).
 *   2. Runs WFD to extract frontier cells (free/unknown boundaries).
 *   3. Clusters frontier cells into Frontier objects.
 *   4. Scores each frontier by information gain and distance to the robot.
 *   5. Sends the best goal to Nav2 via the NavigateToPose action.
 *   6. Re-triggers exploration when navigation completes.
 */

#include "module_3_mapping/frontier_explorer.hpp"

#include <tf2_ros/buffer.h>
#include <tf2_ros/transform_listener.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace module_3_mapping {

// ─────────────────────────────────────────────────────────────────────────────
// Helper utilities
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Convert a grid (row, col) to a flat index.
inline int toIndex(int col, int row, int width) {
  return row * width + col;
}

/// Convert a flat index to (col, row).
inline std::pair<int,int> fromIndex(int idx, int width) {
  return {idx % width, idx / width};
}

/// Return 8-connected neighbours that lie within the grid.
std::vector<int> neighbours8(int idx, int width, int height) {
  auto [c, r] = fromIndex(idx, width);
  std::vector<int> nbrs;
  nbrs.reserve(8);
  for (int dr = -1; dr <= 1; ++dr) {
    for (int dc = -1; dc <= 1; ++dc) {
      if (dr == 0 && dc == 0) continue;
      int nr = r + dr;
      int nc = c + dc;
      if (nr >= 0 && nr < height && nc >= 0 && nc < width) {
        nbrs.push_back(toIndex(nc, nr, width));
      }
    }
  }
  return nbrs;
}

/// A cell is "free" if its occupancy value is in [0, 50].
inline bool isFree(int8_t v) { return v >= 0 && v <= 50; }

/// A cell is "unknown" when the value is -1.
inline bool isUnknown(int8_t v) { return v == -1; }

/// A cell is a frontier cell if it is free and has at least one unknown neighbour.
bool isFrontierCell(int idx,
                    const nav_msgs::msg::OccupancyGrid& map) {
  int w = static_cast<int>(map.info.width);
  int h = static_cast<int>(map.info.height);
  if (!isFree(map.data[idx])) return false;
  for (int n : neighbours8(idx, w, h)) {
    if (isUnknown(map.data[n])) return true;
  }
  return false;
}

/// Convert a grid index to world-frame Point.
geometry_msgs::msg::Point indexToWorld(int idx,
                                       const nav_msgs::msg::OccupancyGrid& map) {
  int w = static_cast<int>(map.info.width);
  auto [c, r] = fromIndex(idx, w);
  geometry_msgs::msg::Point p;
  p.x = map.info.origin.position.x + (c + 0.5) * map.info.resolution;
  p.y = map.info.origin.position.y + (r + 0.5) * map.info.resolution;
  p.z = 0.0;
  return p;
}

/// Convert world Point to grid index.  Returns -1 if out of bounds.
int worldToIndex(const geometry_msgs::msg::Point& pt,
                 const nav_msgs::msg::OccupancyGrid& map) {
  int w = static_cast<int>(map.info.width);
  int h = static_cast<int>(map.info.height);
  int c = static_cast<int>((pt.x - map.info.origin.position.x) / map.info.resolution);
  int r = static_cast<int>((pt.y - map.info.origin.position.y) / map.info.resolution);
  if (c < 0 || c >= w || r < 0 || r >= h) return -1;
  return toIndex(c, r, w);
}

double euclidean(const geometry_msgs::msg::Point& a,
                 const geometry_msgs::msg::Point& b) {
  double dx = a.x - b.x;
  double dy = a.y - b.y;
  return std::sqrt(dx*dx + dy*dy);
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

FrontierExplorer::FrontierExplorer(const rclcpp::NodeOptions& options)
: rclcpp::Node("frontier_explorer", options)
{
  // Parameter declarations with defaults
  this->declare_parameter("min_frontier_size",   0.5);
  this->declare_parameter("exploration_radius",  10.0);
  this->declare_parameter("info_gain_weight",    0.7);
  this->declare_parameter("distance_weight",     0.3);
  this->declare_parameter("map_topic",           std::string("/map"));
  this->declare_parameter("goal_topic",          std::string("/goal_pose"));
  this->declare_parameter("frontier_vis_topic",  std::string("/frontier_markers"));
  this->declare_parameter("robot_frame",         std::string("base_link"));
  this->declare_parameter("map_frame",           std::string("map"));

  min_frontier_size_  = this->get_parameter("min_frontier_size").as_double();
  exploration_radius_ = this->get_parameter("exploration_radius").as_double();
  info_gain_weight_   = this->get_parameter("info_gain_weight").as_double();
  distance_weight_    = this->get_parameter("distance_weight").as_double();

  const auto map_topic    = this->get_parameter("map_topic").as_string();
  const auto goal_topic   = this->get_parameter("goal_topic").as_string();
  const auto vis_topic    = this->get_parameter("frontier_vis_topic").as_string();

  // Subscriptions & publishers
  map_sub_ = this->create_subscription<nav_msgs::msg::OccupancyGrid>(
    map_topic, rclcpp::QoS(1).transient_local(),
    std::bind(&FrontierExplorer::mapCallback, this, std::placeholders::_1));

  goal_pub_ = this->create_publisher<geometry_msgs::msg::PoseStamped>(
    goal_topic, 10);

  frontier_vis_pub_ = this->create_publisher<visualization_msgs::msg::MarkerArray>(
    vis_topic, 10);

  // Nav2 action client
  nav_client_ = rclcpp_action::create_client<nav2_msgs::action::NavigateToPose>(
    this, "navigate_to_pose");

  RCLCPP_INFO(this->get_logger(),
    "FrontierExplorer initialised. min_size=%.2f radius=%.1f ig_w=%.2f dist_w=%.2f",
    min_frontier_size_, exploration_radius_, info_gain_weight_, distance_weight_);
}

// ─────────────────────────────────────────────────────────────────────────────
// mapCallback
// ─────────────────────────────────────────────────────────────────────────────

void FrontierExplorer::mapCallback(
  const nav_msgs::msg::OccupancyGrid::SharedPtr msg)
{
  if (exploring_) {
    RCLCPP_DEBUG(this->get_logger(), "Already exploring, skipping map update.");
    return;
  }

  geometry_msgs::msg::Point robot_pos;
  if (!getRobotPose(robot_pos)) {
    RCLCPP_WARN(this->get_logger(), "Cannot get robot pose — skipping exploration cycle.");
    return;
  }

  auto frontiers = detectFrontiers(*msg);
  if (frontiers.empty()) {
    RCLCPP_INFO(this->get_logger(), "No frontiers found — exploration complete.");
    return;
  }

  // Compute scores
  for (auto& f : frontiers) {
    f.information_gain  = computeInformationGain(f, *msg);
    f.distance_to_robot = euclidean(f.centroid, robot_pos);
    double dist_score   = (f.distance_to_robot > 1e-6) ? 1.0 / f.distance_to_robot : 0.0;
    f.score = info_gain_weight_ * f.information_gain +
              distance_weight_  * dist_score;
  }

  publishFrontierMarkers(frontiers);
  selectAndPublishGoal(frontiers);
}

// ─────────────────────────────────────────────────────────────────────────────
// WFD — Wavefront Frontier Detector
// ─────────────────────────────────────────────────────────────────────────────

std::vector<geometry_msgs::msg::Point> FrontierExplorer::wfd(
  const nav_msgs::msg::OccupancyGrid& map,
  const geometry_msgs::msg::Point& robot_pos) const
{
  int w = static_cast<int>(map.info.width);
  int h = static_cast<int>(map.info.height);
  int n_cells = w * h;

  int robot_idx = worldToIndex(robot_pos, map);
  if (robot_idx < 0 || !isFree(map.data[robot_idx])) {
    RCLCPP_WARN(rclcpp::get_logger("wfd"),
      "Robot position not in free space — broadening search.");
    // Fall back: find nearest free cell
    double best_dist = std::numeric_limits<double>::max();
    robot_idx = -1;
    for (int i = 0; i < n_cells; ++i) {
      if (isFree(map.data[i])) {
        auto p = indexToWorld(i, map);
        double d = euclidean(p, robot_pos);
        if (d < best_dist) { best_dist = d; robot_idx = i; }
      }
    }
    if (robot_idx < 0) return {};
  }

  // BFS from robot position; collect all frontier cells
  std::vector<bool> visited(n_cells, false);
  std::queue<int> queue;
  std::vector<geometry_msgs::msg::Point> frontier_cells;

  queue.push(robot_idx);
  visited[robot_idx] = true;

  while (!queue.empty()) {
    int cur = queue.front(); queue.pop();

    for (int nb : neighbours8(cur, w, h)) {
      if (visited[nb]) continue;
      visited[nb] = true;

      if (isFree(map.data[nb])) {
        queue.push(nb);
        if (isFrontierCell(nb, map)) {
          frontier_cells.push_back(indexToWorld(nb, map));
        }
      }
    }
  }

  return frontier_cells;
}

// ─────────────────────────────────────────────────────────────────────────────
// detectFrontiers — cluster WFD cells into Frontier objects
// ─────────────────────────────────────────────────────────────────────────────

std::vector<Frontier> FrontierExplorer::detectFrontiers(
  const nav_msgs::msg::OccupancyGrid& map) const
{
  // We need the robot position to seed WFD; use map origin as fallback
  geometry_msgs::msg::Point robot_pos;
  // getRobotPose is non-const; use origin approximation inside detectFrontiers
  robot_pos.x = map.info.origin.position.x + (map.info.width  / 2.0) * map.info.resolution;
  robot_pos.y = map.info.origin.position.y + (map.info.height / 2.0) * map.info.resolution;

  auto raw_cells = wfd(map, robot_pos);
  if (raw_cells.empty()) return {};

  // Simple grid-based clustering: cells within min_frontier_size_ of each other
  // belong to the same frontier.
  std::vector<bool> assigned(raw_cells.size(), false);
  std::vector<Frontier> frontiers;

  const double cluster_radius = std::max(min_frontier_size_, map.info.resolution * 2.0);

  for (std::size_t i = 0; i < raw_cells.size(); ++i) {
    if (assigned[i]) continue;
    Frontier f;
    f.cells.push_back(raw_cells[i]);
    assigned[i] = true;

    for (std::size_t j = i + 1; j < raw_cells.size(); ++j) {
      if (!assigned[j] && euclidean(raw_cells[i], raw_cells[j]) <= cluster_radius) {
        f.cells.push_back(raw_cells[j]);
        assigned[j] = true;
      }
    }

    // Compute centroid
    double sx = 0.0, sy = 0.0;
    for (const auto& c : f.cells) { sx += c.x; sy += c.y; }
    f.centroid.x = sx / f.cells.size();
    f.centroid.y = sy / f.cells.size();
    f.centroid.z = 0.0;

    // Filter by minimum size (in metres; 1 cell = resolution metres)
    double size_m = f.cells.size() * map.info.resolution;
    if (size_m >= min_frontier_size_) {
      frontiers.push_back(std::move(f));
    }
  }

  return frontiers;
}

// ─────────────────────────────────────────────────────────────────────────────
// computeInformationGain
// ─────────────────────────────────────────────────────────────────────────────

double FrontierExplorer::computeInformationGain(
  const Frontier& f, const nav_msgs::msg::OccupancyGrid& map) const
{
  int w = static_cast<int>(map.info.width);
  int h = static_cast<int>(map.info.height);

  // Count unknown cells within exploration_radius_ of the frontier centroid
  int unknown_count = 0;
  int total_count   = 0;

  int c0 = static_cast<int>((f.centroid.x - map.info.origin.position.x) / map.info.resolution);
  int r0 = static_cast<int>((f.centroid.y - map.info.origin.position.y) / map.info.resolution);
  int cell_radius = static_cast<int>(exploration_radius_ / map.info.resolution);

  for (int dr = -cell_radius; dr <= cell_radius; ++dr) {
    for (int dc = -cell_radius; dc <= cell_radius; ++dc) {
      if (dr*dr + dc*dc > cell_radius*cell_radius) continue;
      int r = r0 + dr;
      int c = c0 + dc;
      if (r < 0 || r >= h || c < 0 || c >= w) continue;
      ++total_count;
      if (isUnknown(map.data[toIndex(c, r, w)])) ++unknown_count;
    }
  }

  if (total_count == 0) return 0.0;
  return static_cast<double>(unknown_count) / static_cast<double>(total_count);
}

// ─────────────────────────────────────────────────────────────────────────────
// selectAndPublishGoal
// ─────────────────────────────────────────────────────────────────────────────

void FrontierExplorer::selectAndPublishGoal(const std::vector<Frontier>& frontiers) {
  if (frontiers.empty()) return;

  // Select the frontier with the highest score
  const Frontier* best = &frontiers[0];
  for (const auto& f : frontiers) {
    if (f.score > best->score) best = &f;
  }

  RCLCPP_INFO(this->get_logger(),
    "Sending goal to frontier at (%.2f, %.2f) score=%.4f ig=%.4f dist=%.2f",
    best->centroid.x, best->centroid.y,
    best->score, best->information_gain, best->distance_to_robot);

  // Publish as PoseStamped (for simple goal monitors)
  geometry_msgs::msg::PoseStamped goal_pose;
  goal_pose.header.stamp    = this->now();
  goal_pose.header.frame_id = this->get_parameter("map_frame").as_string();
  goal_pose.pose.position   = best->centroid;
  goal_pose.pose.orientation.w = 1.0;
  goal_pub_->publish(goal_pose);

  // Also send via Nav2 action
  if (!nav_client_->wait_for_action_server(std::chrono::seconds(1))) {
    RCLCPP_WARN(this->get_logger(), "NavigateToPose action server not available.");
    return;
  }

  auto goal_msg = nav2_msgs::action::NavigateToPose::Goal();
  goal_msg.pose = goal_pose;

  auto send_goal_options =
    rclcpp_action::Client<nav2_msgs::action::NavigateToPose>::SendGoalOptions();

  send_goal_options.result_callback =
    std::bind(&FrontierExplorer::navigationResultCallback, this, std::placeholders::_1);

  exploring_ = true;
  nav_client_->async_send_goal(goal_msg, send_goal_options);
}

// ─────────────────────────────────────────────────────────────────────────────
// navigationResultCallback
// ─────────────────────────────────────────────────────────────────────────────

void FrontierExplorer::navigationResultCallback(
  const rclcpp_action::ClientGoalHandle<
    nav2_msgs::action::NavigateToPose>::WrappedResult& result)
{
  exploring_ = false;

  switch (result.code) {
    case rclcpp_action::ResultCode::SUCCEEDED:
      RCLCPP_INFO(this->get_logger(), "Navigation succeeded — triggering next exploration cycle.");
      break;
    case rclcpp_action::ResultCode::ABORTED:
      RCLCPP_WARN(this->get_logger(), "Navigation aborted — will retry on next map update.");
      break;
    case rclcpp_action::ResultCode::CANCELED:
      RCLCPP_INFO(this->get_logger(), "Navigation cancelled.");
      break;
    default:
      RCLCPP_WARN(this->get_logger(), "Navigation returned unknown result code.");
      break;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// getRobotPose
// ─────────────────────────────────────────────────────────────────────────────

bool FrontierExplorer::getRobotPose(geometry_msgs::msg::Point& position) const {
  // Use TF2 to get the robot base_link in the map frame.
  // We keep a static buffer/listener per call for simplicity; in a real node
  // these would be class members initialised in the constructor.
  static auto tf_buffer   = std::make_shared<tf2_ros::Buffer>(this->get_clock());
  static auto tf_listener = std::make_shared<tf2_ros::TransformListener>(*tf_buffer);

  const auto robot_frame = this->get_parameter("robot_frame").as_string();
  const auto map_frame   = this->get_parameter("map_frame").as_string();

  try {
    auto tf = tf_buffer->lookupTransform(
      map_frame, robot_frame, tf2::TimePointZero, tf2::durationFromSec(0.2));
    position.x = tf.transform.translation.x;
    position.y = tf.transform.translation.y;
    position.z = 0.0;
    return true;
  } catch (const tf2::TransformException& ex) {
    RCLCPP_WARN(this->get_logger(), "TF lookup failed: %s", ex.what());
    return false;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// publishFrontierMarkers
// ─────────────────────────────────────────────────────────────────────────────

void FrontierExplorer::publishFrontierMarkers(const std::vector<Frontier>& frontiers) {
  visualization_msgs::msg::MarkerArray array;
  const auto map_frame = this->get_parameter("map_frame").as_string();
  int id = 0;

  for (const auto& f : frontiers) {
    // Sphere at centroid
    visualization_msgs::msg::Marker centroid_marker;
    centroid_marker.header.frame_id = map_frame;
    centroid_marker.header.stamp    = this->now();
    centroid_marker.ns              = "frontier_centroids";
    centroid_marker.id              = id++;
    centroid_marker.type            = visualization_msgs::msg::Marker::SPHERE;
    centroid_marker.action          = visualization_msgs::msg::Marker::ADD;
    centroid_marker.pose.position   = f.centroid;
    centroid_marker.pose.orientation.w = 1.0;
    centroid_marker.scale.x = centroid_marker.scale.y = centroid_marker.scale.z = 0.3;
    centroid_marker.color.r = 1.0f;
    centroid_marker.color.g = 0.5f;
    centroid_marker.color.b = 0.0f;
    centroid_marker.color.a = 0.8f;
    centroid_marker.lifetime = rclcpp::Duration::from_seconds(2.0);
    array.markers.push_back(centroid_marker);

    // Text label with score
    visualization_msgs::msg::Marker text_marker = centroid_marker;
    text_marker.ns   = "frontier_scores";
    text_marker.id   = id++;
    text_marker.type = visualization_msgs::msg::Marker::TEXT_VIEW_FACING;
    text_marker.pose.position.z += 0.4;
    text_marker.scale.z = 0.2;
    text_marker.color.r = text_marker.color.g = text_marker.color.b = 1.0f;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.3f", f.score);
    text_marker.text = buf;
    array.markers.push_back(text_marker);
  }

  frontier_vis_pub_->publish(array);
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

} // namespace module_3_mapping

int main(int argc, char* argv[]) {
  rclcpp::init(argc, argv);
  rclcpp::NodeOptions options;
  auto node = std::make_shared<module_3_mapping::FrontierExplorer>(options);
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
