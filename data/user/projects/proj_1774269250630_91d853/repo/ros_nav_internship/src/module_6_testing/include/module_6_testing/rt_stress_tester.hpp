#pragma once
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/laser_scan.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <vector>
#include <atomic>
#include <mutex>
#include <map>
#include <algorithm>

namespace module_6_testing {

struct LatencyStats {
  double p50_ms{0.0};
  double p95_ms{0.0};
  double p99_ms{0.0};
  double max_ms{0.0};
  double mean_ms{0.0};
  double std_ms{0.0};
  size_t sample_count{0};
};

class RTStressTester : public rclcpp::Node {
public:
  explicit RTStressTester(const rclcpp::NodeOptions& options);

  void runStressTest(double inject_hz, double duration_s);
  LatencyStats getStats() const;

private:
  void injectSyntheticData();
  void laserCallback(const sensor_msgs::msg::LaserScan::SharedPtr msg);
  void imuCallback(const sensor_msgs::msg::Imu::SharedPtr msg);
  void computeStats();

  rclcpp::Publisher<sensor_msgs::msg::LaserScan>::SharedPtr laser_pub_;
  rclcpp::Publisher<sensor_msgs::msg::Imu>::SharedPtr imu_pub_;
  rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr odom_pub_;
  rclcpp::Subscription<sensor_msgs::msg::LaserScan>::SharedPtr laser_echo_sub_;
  rclcpp::TimerBase::SharedPtr inject_timer_;

  mutable std::mutex stats_mutex_;
  std::vector<double> latency_samples_;
  std::map<uint64_t, rclcpp::Time> send_times_;
  std::atomic<bool> testing_{false};
  double inject_hz_{100.0};
  double duration_s_{30.0};
  uint64_t seq_counter_{0};
};

} // namespace module_6_testing
