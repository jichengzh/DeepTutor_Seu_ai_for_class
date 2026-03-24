#pragma once
#include <rclcpp/rclcpp.hpp>
#include <nav_msgs/msg/occupancy_grid.hpp>
#include <geometry_msgs/msg/point_stamped.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <visualization_msgs/msg/marker_array.hpp>
#include <nav2_msgs/action/navigate_to_pose.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <vector>

namespace module_3_mapping {

struct Frontier {
  geometry_msgs::msg::Point centroid;
  double information_gain{0.0};
  double distance_to_robot{0.0};
  double score{0.0};
  std::vector<geometry_msgs::msg::Point> cells;
};

class FrontierExplorer : public rclcpp::Node {
public:
  explicit FrontierExplorer(const rclcpp::NodeOptions& options);

  std::vector<Frontier> detectFrontiers(
    const nav_msgs::msg::OccupancyGrid& map) const;

  void selectAndPublishGoal(const std::vector<Frontier>& frontiers);

private:
  std::vector<geometry_msgs::msg::Point> wfd(
    const nav_msgs::msg::OccupancyGrid& map,
    const geometry_msgs::msg::Point& robot_pos) const;

  void mapCallback(const nav_msgs::msg::OccupancyGrid::SharedPtr msg);
  bool getRobotPose(geometry_msgs::msg::Point& position) const;
  double computeInformationGain(const Frontier& f, const nav_msgs::msg::OccupancyGrid& map) const;
  void publishFrontierMarkers(const std::vector<Frontier>& frontiers);
  void navigationResultCallback(
    const rclcpp_action::ClientGoalHandle<nav2_msgs::action::NavigateToPose>::WrappedResult& result);

  rclcpp::Subscription<nav_msgs::msg::OccupancyGrid>::SharedPtr map_sub_;
  rclcpp::Publisher<geometry_msgs::msg::PoseStamped>::SharedPtr goal_pub_;
  rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr frontier_vis_pub_;
  rclcpp_action::Client<nav2_msgs::action::NavigateToPose>::SharedPtr nav_client_;

  double min_frontier_size_{0.5};
  double exploration_radius_{10.0};
  double info_gain_weight_{0.7};
  double distance_weight_{0.3};
  bool exploring_{false};
};

} // namespace module_3_mapping
