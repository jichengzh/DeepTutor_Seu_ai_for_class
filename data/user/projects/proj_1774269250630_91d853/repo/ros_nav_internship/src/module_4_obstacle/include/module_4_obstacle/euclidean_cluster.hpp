#pragma once
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>
#include <visualization_msgs/msg/marker_array.hpp>
#include <geometry_msgs/msg/point.hpp>
#include <geometry_msgs/msg/vector3.hpp>
#include <std_msgs/msg/header.hpp>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <vector>
#include <string>

namespace module_4_obstacle {

struct BoundingBox {
  int id;
  geometry_msgs::msg::Point center;
  geometry_msgs::msg::Vector3 dimensions;
  double confidence{1.0};
};

class EuclideanCluster : public rclcpp::Node {
public:
  explicit EuclideanCluster(const rclcpp::NodeOptions& options);

private:
  void pointcloudCallback(const sensor_msgs::msg::PointCloud2::SharedPtr msg);

  std::vector<BoundingBox> extractClusters(
      const pcl::PointCloud<pcl::PointXYZ>::Ptr& cloud) const;

  void publishBoundingBoxes(const std::vector<BoundingBox>& boxes,
                             const std_msgs::msg::Header& header);

  rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr pc_sub_;
  rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr bbox_pub_;
  rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr filtered_pub_;

  double cluster_tolerance_{0.3};
  int min_cluster_size_{10};
  int max_cluster_size_{10000};
  double voxel_leaf_size_{0.05};
  int next_id_{0};
};

}  // namespace module_4_obstacle
