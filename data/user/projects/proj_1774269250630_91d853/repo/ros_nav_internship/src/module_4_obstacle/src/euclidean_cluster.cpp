#include "module_4_obstacle/euclidean_cluster.hpp"

#include <pcl_conversions/pcl_conversions.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/filters/passthrough.h>
#include <pcl/segmentation/extract_clusters.h>
#include <pcl/search/kdtree.h>
#include <pcl/common/centroid.h>

#include <visualization_msgs/msg/marker.hpp>
#include <std_msgs/msg/color_rgba.hpp>
#include <builtin_interfaces/msg/duration.hpp>

#include <limits>
#include <cmath>

namespace module_4_obstacle {

// ─────────────────────────────────────────────────────────────────────────────
EuclideanCluster::EuclideanCluster(const rclcpp::NodeOptions& options)
: Node("euclidean_cluster", options)
{
  // ── Declare parameters ───────────────────────────────────────────────────
  declare_parameter("cluster_tolerance",  cluster_tolerance_);
  declare_parameter("min_cluster_size",   min_cluster_size_);
  declare_parameter("max_cluster_size",   max_cluster_size_);
  declare_parameter("voxel_leaf_size",    voxel_leaf_size_);

  // ── Load parameters ──────────────────────────────────────────────────────
  get_parameter("cluster_tolerance",  cluster_tolerance_);
  get_parameter("min_cluster_size",   min_cluster_size_);
  get_parameter("max_cluster_size",   max_cluster_size_);
  get_parameter("voxel_leaf_size",    voxel_leaf_size_);

  RCLCPP_INFO(get_logger(),
    "EuclideanCluster: tol=%.3f  min=%d  max=%d  leaf=%.3f",
    cluster_tolerance_, min_cluster_size_, max_cluster_size_, voxel_leaf_size_);

  // ── Pub / Sub ────────────────────────────────────────────────────────────
  pc_sub_ = create_subscription<sensor_msgs::msg::PointCloud2>(
    "/pointcloud", rclcpp::SensorDataQoS(),
    std::bind(&EuclideanCluster::pointcloudCallback, this, std::placeholders::_1));

  bbox_pub_ = create_publisher<visualization_msgs::msg::MarkerArray>(
    "/obstacle_bboxes", 10);

  filtered_pub_ = create_publisher<sensor_msgs::msg::PointCloud2>(
    "/filtered_pointcloud", rclcpp::SensorDataQoS());
}

// ─────────────────────────────────────────────────────────────────────────────
void EuclideanCluster::pointcloudCallback(
    const sensor_msgs::msg::PointCloud2::SharedPtr msg)
{
  // Convert ROS → PCL
  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZ>);
  pcl::fromROSMsg(*msg, *cloud);

  if (cloud->empty()) {
    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000, "Received empty point cloud");
    return;
  }

  // ── PassThrough filter: keep points between z = -0.5 and 2.0 m ──────────
  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_filtered(new pcl::PointCloud<pcl::PointXYZ>);
  pcl::PassThrough<pcl::PointXYZ> pass;
  pass.setInputCloud(cloud);
  pass.setFilterFieldName("z");
  pass.setFilterLimits(-0.5, 2.0);
  pass.filter(*cloud_filtered);

  // ── VoxelGrid downsampling ────────────────────────────────────────────────
  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_downsampled(new pcl::PointCloud<pcl::PointXYZ>);
  pcl::VoxelGrid<pcl::PointXYZ> vg;
  vg.setInputCloud(cloud_filtered);
  const float leaf = static_cast<float>(voxel_leaf_size_);
  vg.setLeafSize(leaf, leaf, leaf);
  vg.filter(*cloud_downsampled);

  if (cloud_downsampled->empty()) {
    return;
  }

  // ── Publish filtered cloud ────────────────────────────────────────────────
  if (filtered_pub_->get_subscription_count() > 0) {
    sensor_msgs::msg::PointCloud2 filtered_msg;
    pcl::toROSMsg(*cloud_downsampled, filtered_msg);
    filtered_msg.header = msg->header;
    filtered_pub_->publish(filtered_msg);
  }

  // ── Extract clusters ──────────────────────────────────────────────────────
  const std::vector<BoundingBox> boxes = extractClusters(cloud_downsampled);

  // ── Publish bounding boxes ────────────────────────────────────────────────
  publishBoundingBoxes(boxes, msg->header);
}

// ─────────────────────────────────────────────────────────────────────────────
std::vector<BoundingBox> EuclideanCluster::extractClusters(
    const pcl::PointCloud<pcl::PointXYZ>::Ptr& cloud) const
{
  // Build KD-tree
  pcl::search::KdTree<pcl::PointXYZ>::Ptr tree(
      new pcl::search::KdTree<pcl::PointXYZ>);
  tree->setInputCloud(cloud);

  // Euclidean cluster extraction
  std::vector<pcl::PointIndices> cluster_indices;
  pcl::EuclideanClusterExtraction<pcl::PointXYZ> ec;
  ec.setClusterTolerance(static_cast<double>(cluster_tolerance_));
  ec.setMinClusterSize(min_cluster_size_);
  ec.setMaxClusterSize(max_cluster_size_);
  ec.setSearchMethod(tree);
  ec.setInputCloud(cloud);
  ec.extract(cluster_indices);

  std::vector<BoundingBox> boxes;
  boxes.reserve(cluster_indices.size());

  int id_counter = 0;
  for (const auto& indices : cluster_indices) {
    // Compute axis-aligned bounding box from cluster points
    double xmin =  std::numeric_limits<double>::max();
    double xmax = -std::numeric_limits<double>::max();
    double ymin =  std::numeric_limits<double>::max();
    double ymax = -std::numeric_limits<double>::max();
    double zmin =  std::numeric_limits<double>::max();
    double zmax = -std::numeric_limits<double>::max();

    Eigen::Vector4f centroid;
    pcl::PointCloud<pcl::PointXYZ>::Ptr cluster_cloud(
        new pcl::PointCloud<pcl::PointXYZ>);
    cluster_cloud->reserve(indices.indices.size());

    for (int idx : indices.indices) {
      const auto& pt = cloud->points[idx];
      cluster_cloud->push_back(pt);
      if (pt.x < xmin) xmin = pt.x;
      if (pt.x > xmax) xmax = pt.x;
      if (pt.y < ymin) ymin = pt.y;
      if (pt.y > ymax) ymax = pt.y;
      if (pt.z < zmin) zmin = pt.z;
      if (pt.z > zmax) zmax = pt.z;
    }

    pcl::compute3DCentroid(*cluster_cloud, centroid);

    BoundingBox box;
    box.id = id_counter++;

    box.center.x = centroid[0];
    box.center.y = centroid[1];
    box.center.z = centroid[2];

    box.dimensions.x = std::max(0.1, xmax - xmin);
    box.dimensions.y = std::max(0.1, ymax - ymin);
    box.dimensions.z = std::max(0.1, zmax - zmin);

    box.confidence = 1.0;

    boxes.push_back(box);
  }

  return boxes;
}

// ─────────────────────────────────────────────────────────────────────────────
void EuclideanCluster::publishBoundingBoxes(
    const std::vector<BoundingBox>& boxes,
    const std_msgs::msg::Header& header)
{
  visualization_msgs::msg::MarkerArray marker_array;

  // First add DELETE_ALL marker to clear stale boxes from previous frame
  visualization_msgs::msg::Marker delete_marker;
  delete_marker.header = header;
  delete_marker.ns = "bounding_boxes";
  delete_marker.id = 0;
  delete_marker.action = visualization_msgs::msg::Marker::DELETEALL;
  marker_array.markers.push_back(delete_marker);

  for (const auto& box : boxes) {
    visualization_msgs::msg::Marker marker;
    marker.header  = header;
    marker.ns      = "bounding_boxes";
    marker.id      = box.id;
    marker.type    = visualization_msgs::msg::Marker::CUBE;
    marker.action  = visualization_msgs::msg::Marker::ADD;

    // Pose
    marker.pose.position    = box.center;
    marker.pose.orientation.w = 1.0;

    // Scale
    marker.scale.x = box.dimensions.x;
    marker.scale.y = box.dimensions.y;
    marker.scale.z = box.dimensions.z;

    // Color: semi-transparent cyan
    marker.color.r = 0.0f;
    marker.color.g = 0.8f;
    marker.color.b = 1.0f;
    marker.color.a = 0.5f;

    // Lifetime: 0.5 s (auto-expires if no new detection)
    marker.lifetime.sec     = 0;
    marker.lifetime.nanosec = 500'000'000u;

    marker_array.markers.push_back(marker);
  }

  bbox_pub_->publish(marker_array);
}

}  // namespace module_4_obstacle

// ── Main ─────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::NodeOptions options;
  auto node = std::make_shared<module_4_obstacle::EuclideanCluster>(options);
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
