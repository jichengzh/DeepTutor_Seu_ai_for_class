#pragma once
#include <rclcpp/rclcpp.hpp>
#include <nav_msgs/msg/occupancy_grid.hpp>
#include <visualization_msgs/msg/marker_array.hpp>
#include <string>
#include <vector>
#include <map>

namespace module_3_mapping {

struct SemanticLabel {
  std::string category;
  double confidence{0.0};
  int source_id{0};
};

struct SemanticObject {
  int id;
  geometry_msgs::msg::Point position;
  std::vector<SemanticLabel> label_candidates;
  SemanticLabel resolved_label;
  double timestamp;
};

using SemanticMap = std::vector<SemanticObject>;

class SemanticMapMerger : public rclcpp::Node {
public:
  explicit SemanticMapMerger(const rclcpp::NodeOptions& options);

  SemanticMap merge(const SemanticMap& map_a,
                    const SemanticMap& map_b,
                    double iou_threshold = 0.5) const;

  SemanticLabel resolveConflict(
    const std::vector<SemanticLabel>& candidates) const;

private:
  double computeIOU(const SemanticObject& a, const SemanticObject& b) const;
  void semanticCallback(const visualization_msgs::msg::MarkerArray::SharedPtr msg);
  void publishMergedMap(const SemanticMap& merged);

  rclcpp::Subscription<visualization_msgs::msg::MarkerArray>::SharedPtr semantic_sub_a_;
  rclcpp::Subscription<visualization_msgs::msg::MarkerArray>::SharedPtr semantic_sub_b_;
  rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr merged_pub_;

  double iou_threshold_{0.5};
  SemanticMap current_map_;
};

} // namespace module_3_mapping
