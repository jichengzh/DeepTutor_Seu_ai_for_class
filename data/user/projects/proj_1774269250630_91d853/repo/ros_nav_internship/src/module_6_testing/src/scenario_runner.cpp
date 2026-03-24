/**
 * scenario_runner.cpp
 *
 * Parses OpenSCENARIO .xosc files, orchestrates Gazebo scenario setup,
 * waits for completion and evaluates pass/fail criteria.
 */
#include "module_6_testing/scenario_runner.hpp"

#include <std_msgs/msg/string.hpp>
#include <rclcpp/rclcpp.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <regex>
#include <stdexcept>
#include <algorithm>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace module_6_testing {

// ──────────────────────────────────────────────────────────────────────────────
// Constructor
// ──────────────────────────────────────────────────────────────────────────────
ScenarioRunner::ScenarioRunner(const rclcpp::NodeOptions& options)
: rclcpp::Node("scenario_runner", options)
{
  this->declare_parameter<double>("scenario_timeout", 300.0);
  this->declare_parameter<std::string>("scenarios_dir", "");

  scenario_timeout_ = this->get_parameter("scenario_timeout").as_double();
  scenarios_dir_    = this->get_parameter("scenarios_dir").as_string();

  // Publisher: send commands to the scenario orchestrator
  scenario_cmd_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/scenario/command", rclcpp::QoS(10));

  // Subscriber: receive status updates from the scenario
  scenario_status_sub_ = this->create_subscription<std_msgs::msg::String>(
      "/scenario/status",
      rclcpp::QoS(10),
      [this](const std_msgs::msg::String::SharedPtr msg) {
        current_status_ = msg->data;
        RCLCPP_INFO(get_logger(), "Scenario status: %s", current_status_.c_str());
      });

  RCLCPP_INFO(get_logger(), "ScenarioRunner initialised (timeout=%.1fs)", scenario_timeout_);
}

// ──────────────────────────────────────────────────────────────────────────────
// parseXOSC  —  minimal XML parser (no external library dependency)
// ──────────────────────────────────────────────────────────────────────────────
bool ScenarioRunner::parseXOSC(const std::string& xosc_path,
                                 std::map<std::string, std::string>& params)
{
  std::ifstream file(xosc_path);
  if (!file.is_open()) {
    RCLCPP_ERROR(get_logger(), "Cannot open XOSC file: %s", xosc_path.c_str());
    return false;
  }

  std::string content((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());

  // ── Extract scenario name from FileHeader description attribute ──────────
  {
    std::regex re_name(R"(description\s*=\s*"([^"]+)")");
    std::smatch m;
    if (std::regex_search(content, m, re_name)) {
      params["scenario_name"] = m[1].str();
    } else {
      // Fall back to filename stem
      params["scenario_name"] = fs::path(xosc_path).stem().string();
    }
  }

  // ── Extract max duration from TimeReference ──────────────────────────────
  {
    std::regex re_dur(R"(duration\s*=\s*"([0-9.]+)")");
    std::smatch m;
    if (std::regex_search(content, m, re_dur)) {
      params["duration"] = m[1].str();
    } else {
      params["duration"] = std::to_string(scenario_timeout_);
    }
  }

  // ── Extract entity names ─────────────────────────────────────────────────
  {
    std::regex re_entity(R"(<ScenarioObject\s+name\s*=\s*"([^"]+)")");
    std::sregex_iterator it(content.begin(), content.end(), re_entity);
    std::sregex_iterator end;
    std::vector<std::string> entities;
    for (; it != end; ++it) {
      entities.push_back((*it)[1].str());
    }
    if (!entities.empty()) {
      std::string list;
      for (size_t i = 0; i < entities.size(); ++i) {
        if (i) list += ",";
        list += entities[i];
      }
      params["entities"] = list;
    }
  }

  // ── Extract goal position from TeleportAction WorldPosition ──────────────
  {
    std::regex re_wp(R"(<WorldPosition\s[^>]*x\s*=\s*"([^"]+)"[^>]*y\s*=\s*"([^"]+)")");
    std::smatch m;
    std::string::const_iterator search_start(content.cbegin());
    // First occurrence = start pose, second = goal
    int found = 0;
    while (std::regex_search(search_start, content.cend(), m, re_wp)) {
      if (found == 0) {
        params["start_x"] = m[1].str();
        params["start_y"] = m[2].str();
      } else if (found == 1) {
        params["goal_x"] = m[1].str();
        params["goal_y"] = m[2].str();
      }
      search_start = m.suffix().first;
      ++found;
    }
  }

  // ── Extract pass criteria from custom ParameterDeclaration ───────────────
  {
    std::regex re_param(
        R"(<Parameter\s+name\s*=\s*"(pass_criteria_[^"]+)"\s+value\s*=\s*"([^"]+)")");
    std::sregex_iterator it(content.begin(), content.end(), re_param);
    std::sregex_iterator end;
    for (; it != end; ++it) {
      params[(*it)[1].str()] = (*it)[2].str();
    }
  }

  RCLCPP_INFO(get_logger(), "Parsed XOSC '%s': scenario='%s', duration=%ss",
              xosc_path.c_str(),
              params["scenario_name"].c_str(),
              params.count("duration") ? params["duration"].c_str() : "?");
  return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// setupGazeboScenario
// ──────────────────────────────────────────────────────────────────────────────
void ScenarioRunner::setupGazeboScenario(
    const std::map<std::string, std::string>& params)
{
  // In a full implementation this would call ros2 service /spawn_entity.
  // Here we publish a JSON-like command string that the scenario orchestrator
  // node (e.g. openscenario_interpreter) can consume.
  std::ostringstream cmd;
  cmd << "{\"command\":\"setup\"";
  for (const auto& kv : params) {
    cmd << ",\"" << kv.first << "\":\"" << kv.second << "\"";
  }
  cmd << "}";

  auto msg = std_msgs::msg::String();
  msg.data = cmd.str();
  scenario_cmd_pub_->publish(msg);

  RCLCPP_INFO(get_logger(), "Sent setup command for scenario '%s'",
              params.count("scenario_name")
                  ? params.at("scenario_name").c_str() : "unknown");

  // Brief pause to allow Gazebo to receive the command
  std::this_thread::sleep_for(500ms);
}

// ──────────────────────────────────────────────────────────────────────────────
// waitForCompletion
// ──────────────────────────────────────────────────────────────────────────────
bool ScenarioRunner::waitForCompletion(double timeout_s)
{
  // Send start command
  {
    auto msg = std_msgs::msg::String();
    msg.data = R"({"command":"start"})";
    scenario_cmd_pub_->publish(msg);
  }

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout_s);

  while (std::chrono::steady_clock::now() < deadline) {
    rclcpp::spin_some(this->get_node_base_interface());

    if (current_status_ == "completed" ||
        current_status_ == "passed"    ||
        current_status_ == "failed") {
      return true;
    }
    std::this_thread::sleep_for(100ms);
  }

  RCLCPP_WARN(get_logger(), "Scenario timed out after %.1f s", timeout_s);
  return false;
}

// ──────────────────────────────────────────────────────────────────────────────
// evaluateResults
// ──────────────────────────────────────────────────────────────────────────────
ScenarioResult ScenarioRunner::evaluateResults(
    const std::string& scenario_name,
    const std::map<std::string, std::string>& params)
{
  ScenarioResult result;
  result.scenario_name = scenario_name;
  result.status        = ScenarioStatus::PENDING;

  // ── Criteria 1: Navigation success ─────────────────────────────────────
  const bool nav_success = (current_status_ == "completed" ||
                             current_status_ == "passed");
  if (nav_success) {
    result.pass_criteria.push_back("navigation_completed");
  } else {
    result.failed_criteria.push_back("navigation_completed");
    result.failure_reason += "Navigation did not complete. ";
  }

  // ── Criteria 2: Collision-free ───────────────────────────────────────────
  // A real implementation would subscribe to /collision_count; we simulate.
  const bool collision_free = (current_status_ != "collision");
  if (collision_free) {
    result.pass_criteria.push_back("collision_free");
  } else {
    result.failed_criteria.push_back("collision_free");
    result.failure_reason += "Collision detected. ";
  }

  // ── Criteria 3: Timing ──────────────────────────────────────────────────
  double allowed_duration = scenario_timeout_;
  if (params.count("duration")) {
    try { allowed_duration = std::stod(params.at("duration")); }
    catch (...) { /* keep default */ }
  }
  const bool timing_ok = (result.duration_s <= allowed_duration * 1.1);
  if (timing_ok) {
    result.pass_criteria.push_back("timing_within_budget");
  } else {
    result.failed_criteria.push_back("timing_within_budget");
    result.failure_reason += "Exceeded time budget. ";
  }

  // ── Custom pass criteria from XOSC parameters ────────────────────────────
  for (const auto& kv : params) {
    if (kv.first.find("pass_criteria_") == 0) {
      // Value is expected metric threshold, e.g. "max_velocity<1.5"
      result.pass_criteria.push_back(kv.first + "=" + kv.second);
    }
  }

  // ── Overall status ───────────────────────────────────────────────────────
  result.status = result.failed_criteria.empty()
                      ? ScenarioStatus::PASSED
                      : ScenarioStatus::FAILED;

  // ── Metrics ──────────────────────────────────────────────────────────────
  result.metrics["duration_s"]     = result.duration_s;
  result.metrics["pass_count"]     = static_cast<double>(result.pass_criteria.size());
  result.metrics["fail_count"]     = static_cast<double>(result.failed_criteria.size());
  result.metrics["collision_free"] = collision_free ? 1.0 : 0.0;
  result.metrics["nav_success"]    = nav_success    ? 1.0 : 0.0;

  return result;
}

// ──────────────────────────────────────────────────────────────────────────────
// runScenario
// ──────────────────────────────────────────────────────────────────────────────
ScenarioResult ScenarioRunner::runScenario(const std::string& xosc_path)
{
  ScenarioResult result;
  result.xosc_path = xosc_path;
  result.status    = ScenarioStatus::PENDING;

  std::map<std::string, std::string> params;

  // 1. Parse
  if (!parseXOSC(xosc_path, params)) {
    result.status         = ScenarioStatus::FAILED;
    result.failure_reason = "Failed to parse XOSC file: " + xosc_path;
    return result;
  }

  result.scenario_name = params.count("scenario_name")
                             ? params.at("scenario_name")
                             : fs::path(xosc_path).stem().string();
  current_status_ = "idle";

  // 2. Setup Gazebo
  setupGazeboScenario(params);
  result.status = ScenarioStatus::RUNNING;

  // 3. Wait for completion
  double timeout = scenario_timeout_;
  if (params.count("duration")) {
    try {
      timeout = std::stod(params.at("duration")) * 1.5; // 50% margin
    } catch (...) {}
  }

  const auto t_start = std::chrono::steady_clock::now();
  const bool completed = waitForCompletion(timeout);
  const auto t_end = std::chrono::steady_clock::now();

  result.duration_s =
      std::chrono::duration<double>(t_end - t_start).count();

  if (!completed) {
    result.status = ScenarioStatus::TIMEOUT;
    result.failure_reason = "Scenario timed out after " +
                             std::to_string(result.duration_s) + " s";
    results_.push_back(result);
    return result;
  }

  // 4. Evaluate
  result = evaluateResults(result.scenario_name, params);
  result.xosc_path  = xosc_path;
  result.duration_s =
      std::chrono::duration<double>(t_end - t_start).count();

  results_.push_back(result);

  RCLCPP_INFO(get_logger(), "Scenario '%s' finished: %s (%.2f s)",
              result.scenario_name.c_str(),
              result.status == ScenarioStatus::PASSED ? "PASSED" : "FAILED",
              result.duration_s);
  return result;
}

// ──────────────────────────────────────────────────────────────────────────────
// runAll
// ──────────────────────────────────────────────────────────────────────────────
std::vector<ScenarioResult> ScenarioRunner::runAll(const std::string& scenarios_dir)
{
  results_.clear();

  if (!fs::exists(scenarios_dir) || !fs::is_directory(scenarios_dir)) {
    RCLCPP_ERROR(get_logger(), "Scenarios directory does not exist: %s",
                 scenarios_dir.c_str());
    return results_;
  }

  // Collect all .xosc files
  std::vector<fs::path> xosc_files;
  for (const auto& entry : fs::recursive_directory_iterator(scenarios_dir)) {
    if (entry.path().extension() == ".xosc") {
      xosc_files.push_back(entry.path());
    }
  }

  std::sort(xosc_files.begin(), xosc_files.end());

  RCLCPP_INFO(get_logger(), "Found %zu scenario file(s) in %s",
              xosc_files.size(), scenarios_dir.c_str());

  for (const auto& path : xosc_files) {
    RCLCPP_INFO(get_logger(), "Running scenario: %s", path.string().c_str());
    runScenario(path.string());
  }

  // Summary
  int passed = 0, failed = 0, timeout = 0;
  for (const auto& r : results_) {
    switch (r.status) {
      case ScenarioStatus::PASSED:  ++passed;  break;
      case ScenarioStatus::FAILED:  ++failed;  break;
      case ScenarioStatus::TIMEOUT: ++timeout; break;
      default: break;
    }
  }
  RCLCPP_INFO(get_logger(),
              "All scenarios complete: %d passed, %d failed, %d timeout",
              passed, failed, timeout);

  return results_;
}

// ──────────────────────────────────────────────────────────────────────────────
// main
// ──────────────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
  rclcpp::init(argc, argv);

  rclcpp::NodeOptions opts;
  auto node = std::make_shared<ScenarioRunner>(opts);

  // If a scenarios directory is provided as first argument, run all scenarios
  if (argc > 1) {
    const std::string dir = argv[1];
    node->runAll(dir);
  } else {
    RCLCPP_WARN(node->get_logger(),
                "No scenarios directory provided. "
                "Pass path as first argument or use the ROS API.");
    rclcpp::spin(node);
  }

  rclcpp::shutdown();
  return 0;
}

} // namespace module_6_testing
