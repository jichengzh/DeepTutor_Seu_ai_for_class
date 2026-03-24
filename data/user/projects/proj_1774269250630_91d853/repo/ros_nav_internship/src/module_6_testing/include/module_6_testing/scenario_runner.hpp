#pragma once
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <string>
#include <vector>
#include <map>
#include <functional>

namespace module_6_testing {

enum class ScenarioStatus { PENDING, RUNNING, PASSED, FAILED, TIMEOUT };

struct ScenarioResult {
  std::string scenario_name;
  std::string xosc_path;
  ScenarioStatus status;
  double duration_s{0.0};
  std::string failure_reason;
  std::map<std::string, double> metrics;
  std::vector<std::string> pass_criteria;
  std::vector<std::string> failed_criteria;
};

class ScenarioRunner : public rclcpp::Node {
public:
  explicit ScenarioRunner(const rclcpp::NodeOptions& options);

  ScenarioResult runScenario(const std::string& xosc_path);
  std::vector<ScenarioResult> runAll(const std::string& scenarios_dir);

private:
  bool parseXOSC(const std::string& xosc_path,
                  std::map<std::string, std::string>& params);
  void setupGazeboScenario(const std::map<std::string, std::string>& params);
  bool waitForCompletion(double timeout_s);
  ScenarioResult evaluateResults(const std::string& scenario_name,
                                  const std::map<std::string, std::string>& params);

  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr scenario_cmd_pub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr scenario_status_sub_;

  double scenario_timeout_{300.0};
  std::string scenarios_dir_;
  std::vector<ScenarioResult> results_;
  std::string current_status_{"idle"};
};

} // namespace module_6_testing
