/**
 * semantic_map_merger.cpp
 *
 * Merges two semantic maps received as visualization_msgs/MarkerArray topics.
 *
 * Merge strategy
 * ──────────────
 * 1. Objects from map_a are loaded into the working map.
 * 2. For each object in map_b the 3D IoU with every existing object is computed.
 * 3. If IoU ≥ iou_threshold  → the two observations refer to the same physical
 *    object; label candidates are merged and conflict resolution is run.
 * 4. If IoU < iou_threshold  → the object from map_b is appended as a new
 *    observation.
 * 5. The merged map is published as a MarkerArray.
 *
 * IoU approximation
 * ─────────────────
 * Each SemanticObject is treated as a sphere with radius r = 0.5 m.  Sphere
 * IoU is derived from the intersection volume of two spheres at distance d:
 *
 *   V_intersection = π(r - d/2)²(d²/4 + dr + 2r²) / (3d/2)   [d < 2r]
 *                  = 0                                          [d ≥ 2r]
 *
 * For equal-radius spheres this simplifies to the standard formula.
 */

#include "module_3_mapping/semantic_map_merger.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace module_3_mapping {

// ─────────────────────────────────────────────────────────────────────────────
// Anonymous helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

constexpr double kSphereRadius = 0.5;   // metres

double euclidean3D(const geometry_msgs::msg::Point& a,
                   const geometry_msgs::msg::Point& b) {
  double dx = a.x - b.x;
  double dy = a.y - b.y;
  double dz = a.z - b.z;
  return std::sqrt(dx*dx + dy*dy + dz*dz);
}

double sphereVolume(double r) {
  return (4.0 / 3.0) * M_PI * r * r * r;
}

/// Intersection volume of two spheres with equal radius r separated by distance d.
double sphereIntersectionVolume(double d, double r) {
  if (d >= 2.0 * r) return 0.0;
  if (d <= 0.0)     return sphereVolume(r); // identical centres
  double h = r - d / 2.0;
  return M_PI * h * h * (3.0 * r - h) * 2.0 / 3.0;
}

/// Convert a SemanticObject to a visualization_msgs::Marker (SPHERE).
visualization_msgs::msg::Marker objectToMarker(const SemanticObject& obj,
                                                int marker_id,
                                                const std::string& frame_id,
                                                const rclcpp::Time& stamp) {
  visualization_msgs::msg::Marker m;
  m.header.frame_id = frame_id;
  m.header.stamp    = stamp;
  m.ns              = "semantic_merged";
  m.id              = marker_id;
  m.type            = visualization_msgs::msg::Marker::SPHERE;
  m.action          = visualization_msgs::msg::Marker::ADD;
  m.pose.position   = obj.position;
  m.pose.orientation.w = 1.0;
  m.scale.x = m.scale.y = m.scale.z = kSphereRadius * 2.0;
  // Colour varies by class (simple hash)
  uint32_t h = std::hash<std::string>{}(obj.resolved_label.category);
  m.color.r = ((h & 0xFF0000) >> 16) / 255.0f;
  m.color.g = ((h & 0x00FF00) >>  8) / 255.0f;
  m.color.b =  (h & 0x0000FF)        / 255.0f;
  m.color.a = static_cast<float>(std::clamp(obj.resolved_label.confidence, 0.3, 1.0));
  m.lifetime = rclcpp::Duration::from_seconds(0);  // persistent
  return m;
}

/// Build a SemanticObject from a Marker (reverse of objectToMarker).
SemanticObject markerToObject(const visualization_msgs::msg::Marker& m, int source_id) {
  SemanticObject obj;
  obj.id           = m.id;
  obj.position     = m.pose.position;
  obj.timestamp    = rclcpp::Time(m.header.stamp).seconds();
  SemanticLabel lbl;
  lbl.category  = m.text.empty() ? m.ns : m.text;
  lbl.confidence = static_cast<double>(m.color.a);
  lbl.source_id  = source_id;
  obj.label_candidates.push_back(lbl);
  obj.resolved_label = lbl;
  return obj;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

SemanticMapMerger::SemanticMapMerger(const rclcpp::NodeOptions& options)
: rclcpp::Node("semantic_map_merger", options)
{
  this->declare_parameter("iou_threshold",      0.5);
  this->declare_parameter("semantic_topic_a",   std::string("/semantic_annotations_a"));
  this->declare_parameter("semantic_topic_b",   std::string("/semantic_annotations_b"));
  this->declare_parameter("merged_topic",       std::string("/semantic_map_merged"));
  this->declare_parameter("map_frame",          std::string("map"));

  iou_threshold_ = this->get_parameter("iou_threshold").as_double();

  const auto topic_a  = this->get_parameter("semantic_topic_a").as_string();
  const auto topic_b  = this->get_parameter("semantic_topic_b").as_string();
  const auto out_topic = this->get_parameter("merged_topic").as_string();

  // We reuse the same callback but differentiate via lambda captures
  semantic_sub_a_ = this->create_subscription<visualization_msgs::msg::MarkerArray>(
    topic_a, 10,
    [this](const visualization_msgs::msg::MarkerArray::SharedPtr msg) {
      semanticCallback(msg);
    });

  semantic_sub_b_ = this->create_subscription<visualization_msgs::msg::MarkerArray>(
    topic_b, 10,
    [this](const visualization_msgs::msg::MarkerArray::SharedPtr msg) {
      semanticCallback(msg);
    });

  merged_pub_ = this->create_publisher<visualization_msgs::msg::MarkerArray>(
    out_topic, 10);

  RCLCPP_INFO(this->get_logger(),
    "SemanticMapMerger: listening on '%s' and '%s', publishing to '%s'. iou_thr=%.2f",
    topic_a.c_str(), topic_b.c_str(), out_topic.c_str(), iou_threshold_);
}

// ─────────────────────────────────────────────────────────────────────────────
// computeIOU
// ─────────────────────────────────────────────────────────────────────────────

double SemanticMapMerger::computeIOU(const SemanticObject& a,
                                      const SemanticObject& b) const {
  double d        = euclidean3D(a.position, b.position);
  double v_inter  = sphereIntersectionVolume(d, kSphereRadius);
  double v_union  = 2.0 * sphereVolume(kSphereRadius) - v_inter;
  if (v_union <= 0.0) return 0.0;
  return v_inter / v_union;
}

// ─────────────────────────────────────────────────────────────────────────────
// resolveConflict — weighted confidence voting
// ─────────────────────────────────────────────────────────────────────────────

SemanticLabel SemanticMapMerger::resolveConflict(
  const std::vector<SemanticLabel>& candidates) const
{
  if (candidates.empty()) return SemanticLabel{};
  if (candidates.size() == 1) return candidates[0];

  // Accumulate confidence per category
  std::map<std::string, double> scores;
  for (const auto& lbl : candidates) {
    scores[lbl.category] += lbl.confidence;
  }

  // Find winner
  auto best = std::max_element(scores.begin(), scores.end(),
    [](const auto& a, const auto& b) { return a.second < b.second; });

  // Average confidence for the winning category among all votes
  double total = 0.0;
  int    count = 0;
  int    best_source = 0;
  for (const auto& lbl : candidates) {
    if (lbl.category == best->first) {
      total += lbl.confidence;
      ++count;
      best_source = lbl.source_id;  // last wins; could also keep majority
    }
  }

  SemanticLabel resolved;
  resolved.category   = best->first;
  resolved.confidence = (count > 0) ? total / count : 0.0;
  resolved.source_id  = best_source;
  return resolved;
}

// ─────────────────────────────────────────────────────────────────────────────
// merge
// ─────────────────────────────────────────────────────────────────────────────

SemanticMap SemanticMapMerger::merge(const SemanticMap& map_a,
                                      const SemanticMap& map_b,
                                      double iou_threshold) const {
  SemanticMap merged = map_a;  // start with copy of map_a

  int next_id = 0;
  for (const auto& obj : merged) {
    next_id = std::max(next_id, obj.id + 1);
  }

  for (const auto& b_obj : map_b) {
    // Find the best matching object in the current merged map
    double best_iou = 0.0;
    SemanticMap::iterator best_it = merged.end();

    for (auto it = merged.begin(); it != merged.end(); ++it) {
      double iou = computeIOU(*it, b_obj);
      if (iou > best_iou) {
        best_iou = iou;
        best_it  = it;
      }
    }

    if (best_iou >= iou_threshold && best_it != merged.end()) {
      // Merge: append label candidates and resolve conflict
      for (const auto& lbl : b_obj.label_candidates) {
        best_it->label_candidates.push_back(lbl);
      }
      best_it->resolved_label = resolveConflict(best_it->label_candidates);

      // Update position as confidence-weighted average
      double wa = best_it->resolved_label.confidence;
      double wb = b_obj.resolved_label.confidence;
      double w_total = wa + wb;
      if (w_total > 1e-9) {
        best_it->position.x = (wa * best_it->position.x + wb * b_obj.position.x) / w_total;
        best_it->position.y = (wa * best_it->position.y + wb * b_obj.position.y) / w_total;
        best_it->position.z = (wa * best_it->position.z + wb * b_obj.position.z) / w_total;
      }
      // Keep the more recent timestamp
      best_it->timestamp = std::max(best_it->timestamp, b_obj.timestamp);
    } else {
      // New object
      SemanticObject new_obj = b_obj;
      new_obj.id = next_id++;
      merged.push_back(std::move(new_obj));
    }
  }

  return merged;
}

// ─────────────────────────────────────────────────────────────────────────────
// semanticCallback
// ─────────────────────────────────────────────────────────────────────────────

void SemanticMapMerger::semanticCallback(
  const visualization_msgs::msg::MarkerArray::SharedPtr msg)
{
  if (!msg || msg->markers.empty()) return;

  // Determine a source_id from the topic name encoded in the namespace
  int source_id = 0;
  if (!msg->markers.empty()) {
    const auto& ns = msg->markers[0].ns;
    // Simple heuristic: "a" topics → 0, "b" topics → 1
    source_id = (ns.find('b') != std::string::npos) ? 1 : 0;
  }

  // Convert incoming markers to SemanticMap
  SemanticMap incoming;
  for (const auto& m : msg->markers) {
    if (m.action == visualization_msgs::msg::Marker::DELETE ||
        m.action == visualization_msgs::msg::Marker::DELETEALL) continue;
    incoming.push_back(markerToObject(m, source_id));
  }

  current_map_ = merge(current_map_, incoming, iou_threshold_);
  publishMergedMap(current_map_);
}

// ─────────────────────────────────────────────────────────────────────────────
// publishMergedMap
// ─────────────────────────────────────────────────────────────────────────────

void SemanticMapMerger::publishMergedMap(const SemanticMap& merged) {
  visualization_msgs::msg::MarkerArray array;
  const auto map_frame = this->get_parameter("map_frame").as_string();
  const auto stamp     = this->now();

  // First, send a DELETEALL to clear stale markers
  visualization_msgs::msg::Marker del_all;
  del_all.action = visualization_msgs::msg::Marker::DELETEALL;
  array.markers.push_back(del_all);

  int mk_id = 0;
  for (const auto& obj : merged) {
    // Sphere marker
    auto sphere = objectToMarker(obj, mk_id++, map_frame, stamp);
    array.markers.push_back(sphere);

    // Text label
    auto text = sphere;
    text.id   = mk_id++;
    text.ns   = "semantic_labels";
    text.type = visualization_msgs::msg::Marker::TEXT_VIEW_FACING;
    text.pose.position.z += kSphereRadius + 0.15;
    text.scale.x = text.scale.y = 0.0;
    text.scale.z = 0.2;
    text.color.r = text.color.g = text.color.b = 1.0f;
    text.color.a = 1.0f;
    std::ostringstream oss;
    oss << obj.resolved_label.category
        << " (" << static_cast<int>(obj.resolved_label.confidence * 100) << "%)";
    text.text = oss.str();
    array.markers.push_back(text);
  }

  merged_pub_->publish(array);

  RCLCPP_DEBUG(this->get_logger(),
    "Published merged semantic map with %zu objects.", merged.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

} // namespace module_3_mapping

int main(int argc, char* argv[]) {
  rclcpp::init(argc, argv);
  rclcpp::NodeOptions options;
  auto node = std::make_shared<module_3_mapping::SemanticMapMerger>(options);
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
