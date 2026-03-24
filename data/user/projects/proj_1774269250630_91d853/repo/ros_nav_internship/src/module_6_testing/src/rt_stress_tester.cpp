/**
 * rt_stress_tester.cpp
 *
 * Injects high-frequency synthetic sensor data into the ROS2 graph and
 * measures end-to-end latency to verify real-time performance guarantees.
 */
#include "module_6_testing/rt_stress_tester.hpp"

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/laser_scan.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <nav_msgs/msg/odometry.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;

namespace module_6_testing {

// ──────────────────────────────────────────────────────────────────────────────
// Constructor
// ──────────────────────────────────────────────────────────────────────────────
RTStressTester::RTStressTester(const rclcpp::NodeOptions& options)
: rclcpp::Node("rt_stress_tester", options)
{
  this->declare_parameter<double>("inject_hz",  100.0);
  this->declare_parameter<double>("duration_s",  30.0);

  inject_hz_  = this->get_parameter("inject_hz").as_double();
  duration_s_ = this->get_parameter("duration_s").as_double();

  // ── Publishers ────────────────────────────────────────────────────────────
  laser_pub_ = this->create_publisher<sensor_msgs::msg::LaserScan>(
      "/stress/laser_scan", rclcpp::SensorDataQoS());

  imu_pub_ = this->create_publisher<sensor_msgs::msg::Imu>(
      "/stress/imu", rclcpp::SensorDataQoS());

  odom_pub_ = this->create_publisher<nav_msgs::msg::Odometry>(
      "/stress/odom", rclcpp::QoS(10));

  // ── Echo subscriber (latency measurement) ─────────────────────────────────
  laser_echo_sub_ = this->create_subscription<sensor_msgs::msg::LaserScan>(
      "/stress/laser_scan",
      rclcpp::SensorDataQoS(),
      std::bind(&RTStressTester::laserCallback, this, std::placeholders::_1));

  RCLCPP_INFO(get_logger(),
              "RTStressTester ready (inject_hz=%.1f, duration=%.1fs)",
              inject_hz_, duration_s_);
}

// ──────────────────────────────────────────────────────────────────────────────
// injectSyntheticData
// ──────────────────────────────────────────────────────────────────────────────
void RTStressTester::injectSyntheticData()
{
  if (!testing_.load()) { return; }

  const rclcpp::Time now = this->now();
  const uint64_t seq     = seq_counter_++;

  // ── LaserScan — 360 rays ─────────────────────────────────────────────────
  {
    sensor_msgs::msg::LaserScan scan;
    scan.header.stamp    = now;
    scan.header.frame_id = "stress_lidar";
    scan.angle_min       = -M_PI;
    scan.angle_max       =  M_PI;
    scan.angle_increment =  2.0 * M_PI / 360.0;
    scan.time_increment  =  0.0;
    scan.scan_time       =  1.0 / inject_hz_;
    scan.range_min       =  0.1f;
    scan.range_max       =  30.0f;
    scan.ranges.resize(360);
    scan.intensities.resize(360);
    for (int i = 0; i < 360; ++i) {
      // Synthetic range: sinusoid + noise
      scan.ranges[i]      = 5.0f + 2.0f * std::sin(i * M_PI / 180.0f);
      scan.intensities[i] = 100.0f;
    }
    // Encode sequence in the first two range values for round-trip tracking
    // (lossless for integers up to ~16M due to float precision)
    scan.ranges[0] = static_cast<float>(seq);

    {
      std::lock_guard<std::mutex> lock(stats_mutex_);
      send_times_[seq] = now;
    }

    laser_pub_->publish(scan);
  }

  // ── IMU ───────────────────────────────────────────────────────────────────
  {
    sensor_msgs::msg::Imu imu;
    imu.header.stamp            = now;
    imu.header.frame_id         = "stress_imu";
    imu.linear_acceleration.x  = 0.1 * std::sin(seq * 0.01);
    imu.linear_acceleration.y  = 0.0;
    imu.linear_acceleration.z  = 9.81;
    imu.angular_velocity.x     = 0.0;
    imu.angular_velocity.y     = 0.0;
    imu.angular_velocity.z     = 0.05 * std::cos(seq * 0.01);
    imu.orientation.w          = 1.0;
    imu.orientation.x          = 0.0;
    imu.orientation.y          = 0.0;
    imu.orientation.z          = 0.0;
    imu_pub_->publish(imu);
  }

  // ── Odometry ──────────────────────────────────────────────────────────────
  {
    nav_msgs::msg::Odometry odom;
    odom.header.stamp       = now;
    odom.header.frame_id    = "odom";
    odom.child_frame_id     = "base_link";
    odom.pose.pose.position.x  = 0.01 * static_cast<double>(seq);
    odom.pose.pose.position.y  = 0.0;
    odom.pose.pose.position.z  = 0.0;
    odom.pose.pose.orientation.w = 1.0;
    odom.twist.twist.linear.x   = 0.5;
    odom_pub_->publish(odom);
  }
}

// ──────────────────────────────────────────────────────────────────────────────
// laserCallback — measure round-trip latency
// ──────────────────────────────────────────────────────────────────────────────
void RTStressTester::laserCallback(
    const sensor_msgs::msg::LaserScan::SharedPtr msg)
{
  if (!testing_.load() || msg->ranges.empty()) { return; }

  const rclcpp::Time recv_time = this->now();
  const uint64_t seq = static_cast<uint64_t>(msg->ranges[0]);

  std::lock_guard<std::mutex> lock(stats_mutex_);
  auto it = send_times_.find(seq);
  if (it != send_times_.end()) {
    const double latency_ms =
        (recv_time - it->second).nanoseconds() / 1.0e6;
    if (latency_ms >= 0.0 && latency_ms < 10000.0) { // sanity guard
      latency_samples_.push_back(latency_ms);
    }
    send_times_.erase(it);
  }
}

// ──────────────────────────────────────────────────────────────────────────────
// runStressTest
// ──────────────────────────────────────────────────────────────────────────────
void RTStressTester::runStressTest(double inject_hz, double duration_s)
{
  inject_hz_  = inject_hz;
  duration_s_ = duration_s;
  seq_counter_ = 0;

  {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    latency_samples_.clear();
    send_times_.clear();
  }

  const auto period_ms = static_cast<int>(1000.0 / inject_hz_);
  RCLCPP_INFO(get_logger(),
              "Starting stress test: %.1f Hz for %.1f s (period=%d ms)",
              inject_hz_, duration_s_, period_ms);

  // Create a wall timer at the requested rate
  inject_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(period_ms),
      std::bind(&RTStressTester::injectSyntheticData, this));

  testing_.store(true);

  // Run for duration_s then stop
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::duration<double>(duration_s_);

  while (std::chrono::steady_clock::now() < deadline) {
    rclcpp::spin_some(this->get_node_base_interface());
    std::this_thread::sleep_for(10ms);
  }

  testing_.store(false);
  inject_timer_->cancel();
  inject_timer_.reset();

  computeStats();

  const auto stats = getStats();
  RCLCPP_INFO(get_logger(),
              "Stress test complete: %zu samples | "
              "p50=%.2f ms  p95=%.2f ms  p99=%.2f ms  max=%.2f ms",
              stats.sample_count,
              stats.p50_ms, stats.p95_ms, stats.p99_ms, stats.max_ms);
}

// ──────────────────────────────────────────────────────────────────────────────
// computeStats  — sorts samples and derives percentiles
// ──────────────────────────────────────────────────────────────────────────────
void RTStressTester::computeStats()
{
  std::lock_guard<std::mutex> lock(stats_mutex_);

  if (latency_samples_.empty()) {
    RCLCPP_WARN(get_logger(), "No latency samples collected.");
    return;
  }

  std::sort(latency_samples_.begin(), latency_samples_.end());
}

// ──────────────────────────────────────────────────────────────────────────────
// getStats
// ──────────────────────────────────────────────────────────────────────────────
LatencyStats RTStressTester::getStats() const
{
  std::lock_guard<std::mutex> lock(stats_mutex_);

  LatencyStats stats;
  if (latency_samples_.empty()) { return stats; }

  const size_t n = latency_samples_.size();
  stats.sample_count = n;

  // Must be sorted — computeStats() does this; safe because we hold the lock
  // and computeStats is called before getStats in runStressTest.
  std::vector<double> sorted = latency_samples_;
  std::sort(sorted.begin(), sorted.end());

  auto percentile = [&](double p) -> double {
    if (n == 1) return sorted[0];
    const double idx = p / 100.0 * static_cast<double>(n - 1);
    const size_t lo  = static_cast<size_t>(std::floor(idx));
    const size_t hi  = std::min(lo + 1, n - 1);
    const double frac = idx - static_cast<double>(lo);
    return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
  };

  stats.p50_ms = percentile(50.0);
  stats.p95_ms = percentile(95.0);
  stats.p99_ms = percentile(99.0);
  stats.max_ms = sorted.back();

  const double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
  stats.mean_ms    = sum / static_cast<double>(n);

  double sq_sum = 0.0;
  for (const double v : sorted) {
    sq_sum += (v - stats.mean_ms) * (v - stats.mean_ms);
  }
  stats.std_ms = std::sqrt(sq_sum / static_cast<double>(n));

  return stats;
}

// ──────────────────────────────────────────────────────────────────────────────
// main
// ──────────────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
  rclcpp::init(argc, argv);

  rclcpp::NodeOptions opts;
  auto node = std::make_shared<RTStressTester>(opts);

  // Parse optional command-line overrides: <inject_hz> <duration_s>
  double hz  = 100.0;
  double dur = 30.0;
  if (argc > 1) { try { hz  = std::stod(argv[1]); } catch (...) {} }
  if (argc > 2) { try { dur = std::stod(argv[2]); } catch (...) {} }

  node->runStressTest(hz, dur);

  rclcpp::shutdown();
  return 0;
}

} // namespace module_6_testing
