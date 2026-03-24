#pragma once
/**
 * semantic_annotator.hpp
 *
 * C++ interface header for the SemanticAnnotator concept.
 * The primary implementation is in scripts/semantic_annotator.py (Python/YOLOv8).
 * This header exposes data structures that can be shared with the C++ merger.
 */

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/camera_info.hpp>
#include <visualization_msgs/msg/marker_array.hpp>
#include <visualization_msgs/msg/marker.hpp>
#include <geometry_msgs/msg/point.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <tf2_ros/buffer.h>
#include <tf2_ros/transform_listener.h>
#include <string>
#include <vector>
#include <memory>

namespace module_3_mapping {

/// A single 2D detection bounding box (pixel space).
struct Detection2D {
  int class_id{-1};
  std::string class_name;
  float confidence{0.0f};
  int x_min{0};
  int y_min{0};
  int x_max{0};
  int y_max{0};
};

/// A 3D annotated object projected into world frame.
struct Annotation3D {
  int id{-1};
  std::string label;
  float confidence{0.0f};
  geometry_msgs::msg::Point world_position;
  std::string frame_id;
  double timestamp{0.0};
};

/**
 * SemanticAnnotatorBase
 *
 * Abstract base class for semantic annotators. Derived classes can be
 * constructed with different detection backends (YOLOv8, ground truth, mock).
 * The Python implementation (SemanticAnnotator) in scripts/semantic_annotator.py
 * mirrors this interface via rclpy.
 */
class SemanticAnnotatorBase : public rclcpp::Node {
public:
  explicit SemanticAnnotatorBase(
    const std::string& node_name,
    const rclcpp::NodeOptions& options);

  virtual ~SemanticAnnotatorBase() = default;

  /// Run detection on a raw OpenCV BGR image (uint8 matrix).
  /// Returns a list of 2D detections.
  virtual std::vector<Detection2D> detect(
    const std::vector<uint8_t>& image_data,
    int width, int height, int channels) = 0;

  /// Backproject a pixel (u,v) with measured depth [metres] to world frame.
  /// Returns false if the TF lookup fails.
  bool backprojectToWorld(
    double u, double v, double depth_m,
    const sensor_msgs::msg::CameraInfo& info,
    const std::string& target_frame,
    geometry_msgs::msg::Point& world_point) const;

protected:
  void publishAnnotations(const std::vector<Annotation3D>& annotations);

  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr image_sub_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr depth_sub_;
  rclcpp::Subscription<sensor_msgs::msg::CameraInfo>::SharedPtr camera_info_sub_;
  rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr annotation_pub_;

  std::shared_ptr<tf2_ros::Buffer>            tf_buffer_;
  std::shared_ptr<tf2_ros::TransformListener> tf_listener_;

  // Latest camera info (filled on first message)
  sensor_msgs::msg::CameraInfo::SharedPtr camera_info_;

  // Parameters
  double confidence_threshold_{0.5};
  std::string world_frame_{"map"};
};

} // namespace module_3_mapping
