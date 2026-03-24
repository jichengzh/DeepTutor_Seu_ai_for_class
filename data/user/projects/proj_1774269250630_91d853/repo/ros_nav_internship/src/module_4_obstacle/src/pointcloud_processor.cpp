#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>
#include <diagnostic_updater/diagnostic_updater.hpp>
#include <diagnostic_updater/publisher.hpp>

#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/filters/passthrough.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/filters/extract_indices.h>
#include <pcl/segmentation/sac_segmentation.h>
#include <pcl/ModelCoefficients.h>
#include <pcl/sample_consensus/method_types.h>
#include <pcl/sample_consensus/model_types.h>

#include <chrono>
#include <string>

namespace module_4_obstacle {

// ─────────────────────────────────────────────────────────────────────────────
class PointcloudProcessor : public rclcpp::Node
{
public:
  explicit PointcloudProcessor(const rclcpp::NodeOptions& options)
  : Node("pointcloud_processor", options),
    diagnostics_(this)
  {
    // ── Parameters ────────────────────────────────────────────────────────
    declare_parameter("roi_x_min",           -10.0);
    declare_parameter("roi_x_max",            10.0);
    declare_parameter("roi_y_min",           -10.0);
    declare_parameter("roi_y_max",            10.0);
    declare_parameter("roi_z_min",           -0.3);
    declare_parameter("roi_z_max",            3.0);
    declare_parameter("height_filter_min",    0.1);   // above ground
    declare_parameter("height_filter_max",    2.5);
    declare_parameter("ransac_distance_thresh", 0.05);
    declare_parameter("ransac_max_iterations",  100);
    declare_parameter("voxel_leaf_size",        0.05);

    roi_x_min_           = get_parameter("roi_x_min").as_double();
    roi_x_max_           = get_parameter("roi_x_max").as_double();
    roi_y_min_           = get_parameter("roi_y_min").as_double();
    roi_y_max_           = get_parameter("roi_y_max").as_double();
    roi_z_min_           = get_parameter("roi_z_min").as_double();
    roi_z_max_           = get_parameter("roi_z_max").as_double();
    height_filter_min_   = get_parameter("height_filter_min").as_double();
    height_filter_max_   = get_parameter("height_filter_max").as_double();
    ransac_dist_thresh_  = get_parameter("ransac_distance_thresh").as_double();
    ransac_max_iter_     = get_parameter("ransac_max_iterations").as_int();
    voxel_leaf_size_     = get_parameter("voxel_leaf_size").as_double();

    // ── Publisher / Subscriber ─────────────────────────────────────────────
    sub_ = create_subscription<sensor_msgs::msg::PointCloud2>(
      "/pointcloud", rclcpp::SensorDataQoS(),
      std::bind(&PointcloudProcessor::callback, this, std::placeholders::_1));

    pub_ = create_publisher<sensor_msgs::msg::PointCloud2>(
      "/processed_pointcloud", rclcpp::SensorDataQoS());

    // ── Diagnostics ───────────────────────────────────────────────────────
    diagnostics_.setHardwareID("pointcloud_processor");
    diagnostics_.add("Processing Status",
      this, &PointcloudProcessor::produceDiagnostics);

    diag_timer_ = create_wall_timer(
      std::chrono::seconds(1),
      [this]() { diagnostics_.force_update(); });

    RCLCPP_INFO(get_logger(), "PointcloudProcessor ready.");
  }

private:
  // ── Processing pipeline ──────────────────────────────────────────────────
  void callback(const sensor_msgs::msg::PointCloud2::SharedPtr msg)
  {
    const auto t_start = std::chrono::steady_clock::now();

    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZ>);
    pcl::fromROSMsg(*msg, *cloud);
    last_input_count_ = static_cast<int>(cloud->size());

    if (cloud->empty()) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000, "Empty input cloud");
      return;
    }

    // ── Step 1: ROI crop (X axis) ─────────────────────────────────────────
    pcl::PointCloud<pcl::PointXYZ>::Ptr roi_cloud(new pcl::PointCloud<pcl::PointXYZ>);
    {
      pcl::PassThrough<pcl::PointXYZ> pass;
      pass.setInputCloud(cloud);
      pass.setFilterFieldName("x");
      pass.setFilterLimits(static_cast<float>(roi_x_min_),
                            static_cast<float>(roi_x_max_));
      pass.filter(*roi_cloud);
    }
    {
      pcl::PointCloud<pcl::PointXYZ>::Ptr tmp(new pcl::PointCloud<pcl::PointXYZ>);
      pcl::PassThrough<pcl::PointXYZ> pass;
      pass.setInputCloud(roi_cloud);
      pass.setFilterFieldName("y");
      pass.setFilterLimits(static_cast<float>(roi_y_min_),
                            static_cast<float>(roi_y_max_));
      pass.filter(*tmp);
      roi_cloud = tmp;
    }
    {
      pcl::PointCloud<pcl::PointXYZ>::Ptr tmp(new pcl::PointCloud<pcl::PointXYZ>);
      pcl::PassThrough<pcl::PointXYZ> pass;
      pass.setInputCloud(roi_cloud);
      pass.setFilterFieldName("z");
      pass.setFilterLimits(static_cast<float>(roi_z_min_),
                            static_cast<float>(roi_z_max_));
      pass.filter(*tmp);
      roi_cloud = tmp;
    }

    if (roi_cloud->empty()) {
      publishCloud(roi_cloud, msg->header);
      return;
    }

    // ── Step 2: Ground removal via RANSAC plane fitting ───────────────────
    pcl::PointCloud<pcl::PointXYZ>::Ptr no_ground(new pcl::PointCloud<pcl::PointXYZ>);
    {
      pcl::ModelCoefficients::Ptr coeffs(new pcl::ModelCoefficients);
      pcl::PointIndices::Ptr inliers(new pcl::PointIndices);

      pcl::SACSegmentation<pcl::PointXYZ> seg;
      seg.setOptimizeCoefficients(true);
      seg.setModelType(pcl::SACMODEL_PLANE);
      seg.setMethodType(pcl::SAC_RANSAC);
      seg.setDistanceThreshold(ransac_dist_thresh_);
      seg.setMaxIterations(ransac_max_iter_);
      seg.setInputCloud(roi_cloud);
      seg.segment(*inliers, *coeffs);

      if (inliers->indices.empty()) {
        // No ground plane found; continue with the full ROI cloud
        no_ground = roi_cloud;
      } else {
        // Keep only points NOT classified as ground
        pcl::ExtractIndices<pcl::PointXYZ> extract;
        extract.setInputCloud(roi_cloud);
        extract.setIndices(inliers);
        extract.setNegative(true);   // true = remove ground
        extract.filter(*no_ground);
      }
    }

    // ── Step 3: Height filter ─────────────────────────────────────────────
    pcl::PointCloud<pcl::PointXYZ>::Ptr height_filtered(
        new pcl::PointCloud<pcl::PointXYZ>);
    {
      pcl::PassThrough<pcl::PointXYZ> pass;
      pass.setInputCloud(no_ground);
      pass.setFilterFieldName("z");
      pass.setFilterLimits(static_cast<float>(height_filter_min_),
                            static_cast<float>(height_filter_max_));
      pass.filter(*height_filtered);
    }

    // ── Step 4: VoxelGrid downsampling ────────────────────────────────────
    pcl::PointCloud<pcl::PointXYZ>::Ptr downsampled(
        new pcl::PointCloud<pcl::PointXYZ>);
    {
      const float leaf = static_cast<float>(voxel_leaf_size_);
      pcl::VoxelGrid<pcl::PointXYZ> vg;
      vg.setInputCloud(height_filtered);
      vg.setLeafSize(leaf, leaf, leaf);
      vg.filter(*downsampled);
    }

    last_output_count_ = static_cast<int>(downsampled->size());

    // ── Measure latency ───────────────────────────────────────────────────
    const auto t_end = std::chrono::steady_clock::now();
    last_latency_ms_ = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    publishCloud(downsampled, msg->header);
  }

  void publishCloud(const pcl::PointCloud<pcl::PointXYZ>::Ptr& cloud,
                    const std_msgs::msg::Header& header)
  {
    sensor_msgs::msg::PointCloud2 out;
    pcl::toROSMsg(*cloud, out);
    out.header = header;
    pub_->publish(out);
  }

  // ── Diagnostics callback ─────────────────────────────────────────────────
  void produceDiagnostics(diagnostic_updater::DiagnosticStatusWrapper& stat)
  {
    stat.add("Input point count",      last_input_count_);
    stat.add("Output point count",     last_output_count_);
    stat.add("Processing latency (ms)", last_latency_ms_);

    if (last_latency_ms_ > 100.0) {
      stat.summary(diagnostic_msgs::msg::DiagnosticStatus::WARN,
                   "Processing latency is high (> 100 ms)");
    } else if (last_input_count_ == 0) {
      stat.summary(diagnostic_msgs::msg::DiagnosticStatus::WARN,
                   "No input points received");
    } else {
      stat.summary(diagnostic_msgs::msg::DiagnosticStatus::OK,
                   "Operating normally");
    }
  }

  // ── Members ───────────────────────────────────────────────────────────────
  rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr sub_;
  rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr    pub_;

  diagnostic_updater::Updater diagnostics_;
  rclcpp::TimerBase::SharedPtr diag_timer_;

  // Processing parameters
  double roi_x_min_{-10.0};
  double roi_x_max_{ 10.0};
  double roi_y_min_{-10.0};
  double roi_y_max_{ 10.0};
  double roi_z_min_{ -0.3};
  double roi_z_max_{  3.0};
  double height_filter_min_{0.1};
  double height_filter_max_{2.5};
  double ransac_dist_thresh_{0.05};
  int    ransac_max_iter_{100};
  double voxel_leaf_size_{0.05};

  // Diagnostics state
  int    last_input_count_{0};
  int    last_output_count_{0};
  double last_latency_ms_{0.0};
};

}  // namespace module_4_obstacle

// ── Main ─────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::NodeOptions options;
  auto node = std::make_shared<module_4_obstacle::PointcloudProcessor>(options);
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
