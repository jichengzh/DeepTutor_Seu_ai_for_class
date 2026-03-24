#include "module_4_obstacle/obstacle_ekf.hpp"

#include <rclcpp/rclcpp.hpp>
#include <visualization_msgs/msg/marker_array.hpp>
#include <visualization_msgs/msg/marker.hpp>
#include <geometry_msgs/msg/point.hpp>

#include <Eigen/Dense>
#include <map>
#include <vector>
#include <limits>
#include <cmath>
#include <chrono>

namespace module_4_obstacle {

// ─────────────────────────────────────────────────────────────────────────────
class ObstacleKfTracker : public rclcpp::Node
{
public:
  explicit ObstacleKfTracker(const rclcpp::NodeOptions& options)
  : Node("obstacle_kf_tracker", options)
  {
    // ── Parameters ────────────────────────────────────────────────────────
    declare_parameter("max_association_dist",  1.5);
    declare_parameter("max_age",               1.0);   // seconds
    declare_parameter("process_noise_std",     0.1);
    declare_parameter("measurement_noise_std", 0.2);
    declare_parameter("publish_rate_hz",       10.0);

    max_association_dist_   = get_parameter("max_association_dist").as_double();
    max_age_                = get_parameter("max_age").as_double();
    process_noise_std_      = get_parameter("process_noise_std").as_double();
    measurement_noise_std_  = get_parameter("measurement_noise_std").as_double();
    const double rate_hz    = get_parameter("publish_rate_hz").as_double();

    // ── Pub / Sub ─────────────────────────────────────────────────────────
    cluster_sub_ = create_subscription<visualization_msgs::msg::MarkerArray>(
      "/obstacle_bboxes", 10,
      std::bind(&ObstacleKfTracker::clustersCallback, this, std::placeholders::_1));

    tracked_pub_ = create_publisher<visualization_msgs::msg::MarkerArray>(
      "/tracked_obstacles", 10);

    // ── Predict + publish timer ───────────────────────────────────────────
    const auto period = std::chrono::duration<double>(1.0 / rate_hz);
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::nanoseconds>(period),
      std::bind(&ObstacleKfTracker::timerCallback, this));

    last_tick_ = now();

    RCLCPP_INFO(get_logger(),
      "ObstacleKfTracker: max_assoc=%.2f  max_age=%.2f  rate=%.1f Hz",
      max_association_dist_, max_age_, rate_hz);
  }

private:
  // ── Types ─────────────────────────────────────────────────────────────────
  struct Track {
    ObstacleEKF ekf;
    int         id;
    bool        updated_this_cycle{false};
  };

  // ── Timer: predict all tracks and publish ─────────────────────────────────
  void timerCallback()
  {
    const rclcpp::Time now_time = now();
    const double dt = (now_time - last_tick_).seconds();
    last_tick_ = now_time;

    // Predict all tracks forward
    for (auto& [id, track] : tracks_) {
      track.ekf.predict(dt);
    }

    // Remove stale tracks
    pruneStale();

    // Publish
    publishTracked(now_time);
  }

  // ── Cluster callback: update tracks from new detections ──────────────────
  void clustersCallback(const visualization_msgs::msg::MarkerArray::SharedPtr msg)
  {
    // Collect detections (skip DELETE markers)
    std::vector<Eigen::Vector3d> detections;
    for (const auto& m : msg->markers) {
      if (m.action == visualization_msgs::msg::Marker::DELETEALL ||
          m.action == visualization_msgs::msg::Marker::DELETE) {
        continue;
      }
      detections.emplace_back(m.pose.position.x,
                               m.pose.position.y,
                               m.pose.position.z);
    }

    // Reset update flags
    for (auto& [id, track] : tracks_) {
      track.updated_this_cycle = false;
    }

    // ── Nearest-neighbour data association ────────────────────────────────
    std::vector<bool> detection_matched(detections.size(), false);

    for (auto& [id, track] : tracks_) {
      const Eigen::Vector3d track_pos = track.ekf.getPosition();
      double best_dist = std::numeric_limits<double>::max();
      int    best_det  = -1;

      for (size_t di = 0; di < detections.size(); ++di) {
        if (detection_matched[di]) continue;
        const double dist = (detections[di] - track_pos).norm();
        if (dist < best_dist) {
          best_dist = dist;
          best_det  = static_cast<int>(di);
        }
      }

      if (best_det >= 0 && best_dist <= max_association_dist_) {
        track.ekf.update(detections[best_det]);
        track.updated_this_cycle    = true;
        detection_matched[best_det] = true;
      }
    }

    // ── Create new tracks for unmatched detections ────────────────────────
    for (size_t di = 0; di < detections.size(); ++di) {
      if (!detection_matched[di]) {
        Track new_track{
          ObstacleEKF(detections[di], process_noise_std_, measurement_noise_std_),
          next_id_++,
          true
        };
        tracks_.emplace(new_track.id, std::move(new_track));
      }
    }
  }

  // ── Remove tracks not updated for longer than max_age ─────────────────────
  void pruneStale()
  {
    for (auto it = tracks_.begin(); it != tracks_.end(); ) {
      if (it->second.ekf.getTimeSinceUpdate() > max_age_) {
        it = tracks_.erase(it);
      } else {
        ++it;
      }
    }
  }

  // ── Publish tracked obstacles as MarkerArray ──────────────────────────────
  void publishTracked(const rclcpp::Time& stamp)
  {
    visualization_msgs::msg::MarkerArray ma;

    // DELETEALL first
    visualization_msgs::msg::Marker del;
    del.header.stamp    = stamp;
    del.header.frame_id = "map";
    del.ns     = "tracked";
    del.action = visualization_msgs::msg::Marker::DELETEALL;
    ma.markers.push_back(del);

    for (const auto& [id, track] : tracks_) {
      const Eigen::Vector3d pos = track.ekf.getPosition();
      const Eigen::Vector3d vel = track.ekf.getVelocity();
      const double speed        = vel.norm();

      // ── Sphere marker for obstacle body ──────────────────────────────────
      visualization_msgs::msg::Marker sphere;
      sphere.header.stamp    = stamp;
      sphere.header.frame_id = "map";
      sphere.ns              = "tracked";
      sphere.id              = id;
      sphere.type            = visualization_msgs::msg::Marker::SPHERE;
      sphere.action          = visualization_msgs::msg::Marker::ADD;

      sphere.pose.position.x    = pos.x();
      sphere.pose.position.y    = pos.y();
      sphere.pose.position.z    = pos.z();
      sphere.pose.orientation.w = 1.0;

      sphere.scale.x = 0.6;
      sphere.scale.y = 0.6;
      sphere.scale.z = 0.6;

      // Encode speed in scale.z so ObstacleLayerExt can read it
      sphere.scale.z = std::max(0.1, speed);

      // Colour: orange
      sphere.color.r = 1.0f;
      sphere.color.g = 0.5f;
      sphere.color.b = 0.0f;
      sphere.color.a = 0.8f;

      sphere.lifetime.sec     = 0;
      sphere.lifetime.nanosec = 500'000'000u;

      ma.markers.push_back(sphere);

      // ── Arrow marker for velocity ─────────────────────────────────────────
      if (speed > 0.05) {
        visualization_msgs::msg::Marker arrow;
        arrow.header = sphere.header;
        arrow.ns     = "velocities";
        arrow.id     = id;
        arrow.type   = visualization_msgs::msg::Marker::ARROW;
        arrow.action = visualization_msgs::msg::Marker::ADD;

        geometry_msgs::msg::Point start, end;
        start.x = pos.x();
        start.y = pos.y();
        start.z = pos.z();
        end.x   = pos.x() + vel.x();
        end.y   = pos.y() + vel.y();
        end.z   = pos.z() + vel.z();

        arrow.points.push_back(start);
        arrow.points.push_back(end);

        arrow.scale.x = 0.05;  // shaft diameter
        arrow.scale.y = 0.1;   // head diameter
        arrow.scale.z = 0.1;   // head length

        arrow.color.r = 1.0f;
        arrow.color.g = 1.0f;
        arrow.color.b = 0.0f;
        arrow.color.a = 1.0f;

        arrow.lifetime = sphere.lifetime;
        ma.markers.push_back(arrow);
      }
    }

    tracked_pub_->publish(ma);
  }

  // ── Members ───────────────────────────────────────────────────────────────
  rclcpp::Subscription<visualization_msgs::msg::MarkerArray>::SharedPtr cluster_sub_;
  rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr    tracked_pub_;
  rclcpp::TimerBase::SharedPtr timer_;

  std::map<int, Track> tracks_;
  int next_id_{0};
  rclcpp::Time last_tick_;

  double max_association_dist_{1.5};
  double max_age_{1.0};
  double process_noise_std_{0.1};
  double measurement_noise_std_{0.2};
};

}  // namespace module_4_obstacle

// ── Main ─────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::NodeOptions options;
  auto node = std::make_shared<module_4_obstacle::ObstacleKfTracker>(options);
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
