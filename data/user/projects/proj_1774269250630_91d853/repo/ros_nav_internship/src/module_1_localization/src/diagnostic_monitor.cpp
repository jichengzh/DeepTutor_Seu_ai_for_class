/**
 * @file diagnostic_monitor.cpp
 * @brief diagnostic_updater-based localization quality monitor.
 *
 * Provides a standalone DiagnosticMonitor class that publishes
 * localization quality metrics to /diagnostics using the standard
 * ROS2 diagnostic_updater framework.
 *
 * Metrics published:
 *  - Localisation frequency (Hz)
 *  - Number of AMCL particles
 *  - Pose covariance trace
 *  - Time since last pose update
 *  - Status: OK / WARN / ERROR based on thresholds
 *
 * This file compiles as part of the localization_lifecycle executable.
 */

#include <chrono>
#include <cmath>
#include <string>

#include <rclcpp/rclcpp.hpp>
#include <diagnostic_updater/diagnostic_updater.hpp>
#include <geometry_msgs/msg/pose_with_covariance_stamped.hpp>

namespace module_1_localization {

// ============================================================
// DiagnosticMonitor class
// ============================================================

class DiagnosticMonitor {
public:
  /**
   * @brief Construct monitor attached to a given node.
   *
   * @param node              Owning node (for clock and logger).
   * @param updater           Shared diagnostic_updater::Updater.
   * @param min_update_hz     Minimum acceptable localisation frequency.
   * @param max_cov_trace     Maximum acceptable covariance trace.
   * @param stale_timeout_s   Seconds before declaring localisation stale.
   */
  explicit DiagnosticMonitor(
    rclcpp::Node * node,
    std::shared_ptr<diagnostic_updater::Updater> updater,
    double min_update_hz  = 5.0,
    double max_cov_trace  = 1.0,
    double stale_timeout_s = 2.0)
  : node_(node),
    updater_(updater),
    min_update_hz_(min_update_hz),
    max_cov_trace_(max_cov_trace),
    stale_timeout_s_(stale_timeout_s)
  {
    updater_->add("Localization Frequency",
      [this](diagnostic_updater::DiagnosticStatusWrapper & stat) {
        checkFrequency(stat);
      });

    updater_->add("Pose Covariance",
      [this](diagnostic_updater::DiagnosticStatusWrapper & stat) {
        checkCovariance(stat);
      });

    updater_->add("Localisation Freshness",
      [this](diagnostic_updater::DiagnosticStatusWrapper & stat) {
        checkFreshness(stat);
      });

    updater_->add("AMCL Particle Count",
      [this](diagnostic_updater::DiagnosticStatusWrapper & stat) {
        checkParticleCount(stat);
      });

    RCLCPP_INFO(node_->get_logger(),
      "DiagnosticMonitor: initialised (min_hz=%.1f max_cov=%.2f timeout=%.1fs)",
      min_update_hz_, max_cov_trace_, stale_timeout_s_);
  }

  // --------------------------------------------------------
  // Called by the localization pipeline on each pose update
  // --------------------------------------------------------

  /**
   * @brief Record a new pose update from the localisation system.
   *
   * @param pose            Latest pose estimate.
   * @param particle_count  Current AMCL particle count (-1 if unknown).
   */
  void onPoseUpdate(
    const geometry_msgs::msg::PoseWithCovarianceStamped & pose,
    int particle_count = -1)
  {
    last_update_time_ = node_->now();

    // Compute covariance trace (x, y, yaw diagonal elements)
    latest_cov_trace_ =
      pose.pose.covariance[0] +   // xx
      pose.pose.covariance[7] +   // yy
      pose.pose.covariance[35];   // yaw-yaw

    // Update frequency estimate using exponential moving average
    if (last_update_time_valid_) {
      const double dt = (node_->now() - prev_update_time_).seconds();
      if (dt > 0.0) {
        const double instant_hz = 1.0 / dt;
        // EMA with alpha = 0.3
        current_hz_ = 0.3 * instant_hz + 0.7 * current_hz_;
      }
    }
    prev_update_time_       = last_update_time_;
    last_update_time_valid_ = true;
    ++update_count_;

    if (particle_count >= 0) {
      latest_particle_count_ = particle_count;
    }
  }

private:
  // --------------------------------------------------------
  // Diagnostic callbacks
  // --------------------------------------------------------

  void checkFrequency(diagnostic_updater::DiagnosticStatusWrapper & stat)
  {
    stat.add("Update frequency (Hz)", current_hz_);
    stat.add("Total updates",         update_count_);

    if (!last_update_time_valid_) {
      stat.summary(diagnostic_msgs::msg::DiagnosticStatus::WARN,
        "No pose updates received yet");
    } else if (current_hz_ < min_update_hz_) {
      stat.summaryf(diagnostic_msgs::msg::DiagnosticStatus::WARN,
        "Low localisation frequency: %.1f Hz (min %.1f Hz)",
        current_hz_, min_update_hz_);
    } else {
      stat.summaryf(diagnostic_msgs::msg::DiagnosticStatus::OK,
        "Frequency OK: %.1f Hz", current_hz_);
    }
  }

  void checkCovariance(diagnostic_updater::DiagnosticStatusWrapper & stat)
  {
    stat.add("Covariance trace", latest_cov_trace_);
    stat.add("Max allowed trace", max_cov_trace_);

    if (latest_cov_trace_ < 0.0) {
      stat.summary(diagnostic_msgs::msg::DiagnosticStatus::WARN,
        "Covariance not yet available");
    } else if (latest_cov_trace_ > max_cov_trace_) {
      stat.summaryf(diagnostic_msgs::msg::DiagnosticStatus::WARN,
        "High pose uncertainty (trace=%.3f > %.3f)",
        latest_cov_trace_, max_cov_trace_);
    } else {
      stat.summaryf(diagnostic_msgs::msg::DiagnosticStatus::OK,
        "Covariance OK (trace=%.3f)", latest_cov_trace_);
    }
  }

  void checkFreshness(diagnostic_updater::DiagnosticStatusWrapper & stat)
  {
    if (!last_update_time_valid_) {
      stat.summary(diagnostic_msgs::msg::DiagnosticStatus::WARN,
        "No pose updates received");
      return;
    }

    const double age_s = (node_->now() - last_update_time_).seconds();
    stat.add("Age of last update (s)", age_s);

    if (age_s > stale_timeout_s_) {
      stat.summaryf(diagnostic_msgs::msg::DiagnosticStatus::ERROR,
        "Localisation stale: last update %.1f s ago (timeout %.1f s)",
        age_s, stale_timeout_s_);
    } else {
      stat.summaryf(diagnostic_msgs::msg::DiagnosticStatus::OK,
        "Localisation fresh (%.2f s ago)", age_s);
    }
  }

  void checkParticleCount(diagnostic_updater::DiagnosticStatusWrapper & stat)
  {
    stat.add("AMCL particle count", latest_particle_count_);

    if (latest_particle_count_ < 0) {
      stat.summary(diagnostic_msgs::msg::DiagnosticStatus::OK,
        "Particle count not monitored");
    } else if (latest_particle_count_ < 100) {
      stat.summaryf(diagnostic_msgs::msg::DiagnosticStatus::WARN,
        "Low AMCL particle count: %d", latest_particle_count_);
    } else {
      stat.summaryf(diagnostic_msgs::msg::DiagnosticStatus::OK,
        "Particle count OK: %d", latest_particle_count_);
    }
  }

  // --------------------------------------------------------
  // Members
  // --------------------------------------------------------
  rclcpp::Node *                                     node_;
  std::shared_ptr<diagnostic_updater::Updater>      updater_;

  double   min_update_hz_;
  double   max_cov_trace_;
  double   stale_timeout_s_;

  double   current_hz_            {0.0};
  double   latest_cov_trace_      {-1.0};
  uint64_t update_count_          {0};
  int      latest_particle_count_ {-1};

  rclcpp::Time last_update_time_;
  rclcpp::Time prev_update_time_;
  bool         last_update_time_valid_ {false};
};

}  // namespace module_1_localization
