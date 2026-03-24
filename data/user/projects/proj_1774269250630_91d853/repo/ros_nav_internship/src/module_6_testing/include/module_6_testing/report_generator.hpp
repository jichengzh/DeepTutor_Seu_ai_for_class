#pragma once
#include <string>
#include <vector>
#include <map>
#include <chrono>

namespace module_6_testing {

/// Mirrors the Python TestCase dataclass for C++ consumers.
struct ReportTestCase {
  std::string name;
  std::string classname;
  double time_s{0.0};
  std::string status;          // "passed" | "failed" | "error" | "skipped"
  std::string failure_message; // empty if passed
  std::string system_out;
};

/// Mirrors the Python TestSuite dataclass for C++ consumers.
struct ReportTestSuite {
  std::string name;
  int tests{0};
  int failures{0};
  int errors{0};
  double time_s{0.0};
  std::vector<ReportTestCase> test_cases;
};

/// Aggregated report handed off to the IEEE 829 XML writer.
struct AggregatedReport {
  std::string report_id;
  std::chrono::system_clock::time_point generated_at;
  std::vector<ReportTestSuite> suites;
  std::map<std::string, double> coverage_metrics; // e.g. functional_pct, safety_pct
  int total_tests{0};
  int total_failures{0};
  int total_errors{0};
  double total_time_s{0.0};
};

/**
 * @brief Thin C++ façade around the Python report_generator.py.
 *
 * Typical usage:
 * @code
 *   ReportGenerator gen;
 *   gen.addGtestXml("/tmp/gtest_results.xml");
 *   gen.addPytestXml("/tmp/pytest_results.xml");
 *   gen.addScenarioResultsDir("/tmp/scenario_results");
 *   gen.generateIeee829("/tmp/test_report.xml");
 * @endcode
 */
class ReportGenerator {
public:
  ReportGenerator() = default;

  /// Register a GTest JUnit-XML output file to aggregate.
  void addGtestXml(const std::string& path);

  /// Register a pytest JUnit-XML output file to aggregate.
  void addPytestXml(const std::string& path);

  /// Register a directory containing OpenSCENARIO result JSON files.
  void addScenarioResultsDir(const std::string& dir);

  /**
   * @brief Aggregate all registered inputs and write an IEEE 829 XML report.
   * @param output_path Destination .xml file path.
   * @return true on success.
   */
  bool generateIeee829(const std::string& output_path);

  /// Return the last aggregated report data (populated after generateIeee829).
  const AggregatedReport& getReport() const { return report_; }

private:
  /// Parse a JUnit-style XML file into ReportTestSuites.
  std::vector<ReportTestSuite> parseJUnitXml(const std::string& path);

  /// Compute coverage metrics from all loaded suites.
  std::map<std::string, double> computeCoverage(
      const std::vector<ReportTestSuite>& suites);

  /// Write the IEEE 829 XML document.
  bool writeIeee829Xml(const AggregatedReport& report,
                        const std::string& output_path);

  std::vector<std::string> gtest_xml_paths_;
  std::vector<std::string> pytest_xml_paths_;
  std::vector<std::string> scenario_result_dirs_;
  AggregatedReport report_;
};

} // namespace module_6_testing
