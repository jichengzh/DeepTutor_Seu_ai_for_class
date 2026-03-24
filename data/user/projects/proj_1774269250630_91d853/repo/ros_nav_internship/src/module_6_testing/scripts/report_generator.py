#!/usr/bin/env python3
"""
report_generator.py — IEEE 829 compliant test report generator.

Aggregates results from:
  - GTest XML output  (JUnit format produced by --gtest_output=xml)
  - pytest XML output (JUnit format produced by pytest --junitxml)
  - OpenSCENARIO scenario result JSON files
and generates a IEEE 829 standard test_report.xml.

Usage:
    python3 report_generator.py \
        --gtest-xml  results/gtest_results.xml  \
        --pytest-xml results/pytest_results.xml \
        --scenario-dir results/scenarios \
        --output results/test_report.xml
"""

import argparse
import json
import datetime
import xml.etree.ElementTree as ET
from xml.dom import minidom
from pathlib import Path
from dataclasses import dataclass, field
from typing import List, Dict, Optional
import sys
import os


# ─────────────────────────────────────────────────────────────────────────────
# Data classes
# ─────────────────────────────────────────────────────────────────────────────

@dataclass
class TestCase:
    name: str
    classname: str
    time: float
    status: str                     # "passed" | "failed" | "error" | "skipped"
    failure_message: Optional[str] = None
    system_out: Optional[str] = None


@dataclass
class TestSuite:
    name: str
    tests: int
    failures: int
    errors: int
    time: float
    test_cases: List[TestCase] = field(default_factory=list)


# ─────────────────────────────────────────────────────────────────────────────
# TestReportGenerator
# ─────────────────────────────────────────────────────────────────────────────

class TestReportGenerator:
    """Aggregates multiple test result sources and generates IEEE 829 XML."""

    def __init__(self):
        self.test_suites: List[TestSuite] = []

    # ──────────────────────────────────────────────────────────────────────
    # Private helpers
    # ──────────────────────────────────────────────────────────────────────

    def _parse_junit_xml(self, xml_path: str) -> List[TestSuite]:
        """Parse a JUnit/GTest/pytest XML file into TestSuite objects."""
        suites: List[TestSuite] = []

        if not xml_path or not Path(xml_path).exists():
            print(f"[WARN] JUnit XML not found: {xml_path}", file=sys.stderr)
            return suites

        try:
            tree = ET.parse(xml_path)
            root = tree.getroot()
        except ET.ParseError as exc:
            print(f"[ERROR] Cannot parse {xml_path}: {exc}", file=sys.stderr)
            return suites

        # Handle both <testsuites><testsuite> and bare <testsuite>
        suite_elements = []
        if root.tag == "testsuites":
            suite_elements = root.findall("testsuite")
        elif root.tag == "testsuite":
            suite_elements = [root]
        else:
            suite_elements = root.findall(".//testsuite")

        for suite_elem in suite_elements:
            suite = TestSuite(
                name=suite_elem.get("name", "unnamed"),
                tests=int(suite_elem.get("tests", 0)),
                failures=int(suite_elem.get("failures", 0)),
                errors=int(suite_elem.get("errors", 0)),
                time=float(suite_elem.get("time", 0.0)),
            )

            for tc_elem in suite_elem.findall("testcase"):
                failure_msg = None
                status = "passed"

                failure_elem = tc_elem.find("failure")
                error_elem   = tc_elem.find("error")
                skipped_elem = tc_elem.find("skipped")

                if failure_elem is not None:
                    status = "failed"
                    failure_msg = (
                        failure_elem.get("message", "")
                        or (failure_elem.text or "").strip()
                    )
                elif error_elem is not None:
                    status = "error"
                    failure_msg = (
                        error_elem.get("message", "")
                        or (error_elem.text or "").strip()
                    )
                elif skipped_elem is not None:
                    status = "skipped"

                system_out_elem = tc_elem.find("system-out")
                system_out = (
                    system_out_elem.text.strip()
                    if system_out_elem is not None and system_out_elem.text
                    else None
                )

                tc = TestCase(
                    name=tc_elem.get("name", "unnamed"),
                    classname=tc_elem.get("classname", suite.name),
                    time=float(tc_elem.get("time", 0.0)),
                    status=status,
                    failure_message=failure_msg,
                    system_out=system_out,
                )
                suite.test_cases.append(tc)

            suites.append(suite)

        return suites

    def _parse_scenario_results(self, scenario_dir: str) -> List[TestSuite]:
        """Parse OpenSCENARIO JSON result files into TestSuite objects."""
        suites: List[TestSuite] = []

        if not scenario_dir:
            return suites

        dir_path = Path(scenario_dir)
        if not dir_path.exists():
            print(f"[WARN] Scenario directory not found: {scenario_dir}",
                  file=sys.stderr)
            return suites

        json_files = sorted(dir_path.glob("**/*.json"))
        if not json_files:
            # Also look for XOSC-derived result files with .result suffix
            json_files = sorted(dir_path.glob("**/*.result"))

        suite = TestSuite(
            name="OpenSCENARIO",
            tests=0,
            failures=0,
            errors=0,
            time=0.0,
        )

        for jf in json_files:
            try:
                with open(jf) as fh:
                    data = json.load(fh)
            except (json.JSONDecodeError, OSError) as exc:
                print(f"[WARN] Cannot read {jf}: {exc}", file=sys.stderr)
                continue

            scenario_name = data.get("scenario_name",
                                     jf.stem.replace("_result", ""))
            status_raw    = data.get("status", "FAILED").upper()
            duration      = float(data.get("duration_s", 0.0))
            failure_reason= data.get("failure_reason", "")

            status = "passed" if status_raw in ("PASSED", "PASS") else "failed"
            tc = TestCase(
                name=scenario_name,
                classname="OpenSCENARIO",
                time=duration,
                status=status,
                failure_message=failure_reason if status != "passed" else None,
            )
            suite.tests    += 1
            suite.failures += 1 if status == "failed" else 0
            suite.time     += duration
            suite.test_cases.append(tc)

        if suite.tests > 0:
            suites.append(suite)

        return suites

    # ──────────────────────────────────────────────────────────────────────
    # Public API
    # ──────────────────────────────────────────────────────────────────────

    def aggregate_results(
        self,
        gtest_xml: str,
        pytest_xml: str,
        scenario_results: list,
    ) -> dict:
        """
        Aggregate all test results into a unified data structure.

        Parameters
        ----------
        gtest_xml         : path to GTest JUnit XML
        pytest_xml        : path to pytest JUnit XML
        scenario_results  : list of dicts with scenario result data OR
                            path string to a directory containing JSON files

        Returns
        -------
        dict with keys: suites, total_tests, total_failures, total_errors,
                        total_time, timestamp, coverage
        """
        all_suites: List[TestSuite] = []

        # GTest
        all_suites.extend(self._parse_junit_xml(gtest_xml))

        # pytest
        all_suites.extend(self._parse_junit_xml(pytest_xml))

        # OpenSCENARIO
        if isinstance(scenario_results, str):
            all_suites.extend(self._parse_scenario_results(scenario_results))
        elif isinstance(scenario_results, list):
            # Inline list of dicts
            if scenario_results:
                suite = TestSuite(
                    name="OpenSCENARIO_inline",
                    tests=len(scenario_results),
                    failures=0,
                    errors=0,
                    time=0.0,
                )
                for item in scenario_results:
                    status = (
                        "passed"
                        if str(item.get("status", "FAILED")).upper()
                           in ("PASSED", "PASS")
                        else "failed"
                    )
                    tc = TestCase(
                        name=item.get("scenario_name", "unknown"),
                        classname="OpenSCENARIO",
                        time=float(item.get("duration_s", 0.0)),
                        status=status,
                        failure_message=item.get("failure_reason"),
                    )
                    suite.time += tc.time
                    if status == "failed":
                        suite.failures += 1
                    suite.test_cases.append(tc)
                all_suites.append(suite)

        self.test_suites = all_suites

        total_tests    = sum(s.tests    for s in all_suites)
        total_failures = sum(s.failures for s in all_suites)
        total_errors   = sum(s.errors   for s in all_suites)
        total_time     = sum(s.time     for s in all_suites)

        aggregated = {
            "suites":         all_suites,
            "total_tests":    total_tests,
            "total_failures": total_failures,
            "total_errors":   total_errors,
            "total_time":     total_time,
            "timestamp":      datetime.datetime.utcnow().isoformat() + "Z",
        }
        aggregated["coverage"] = self.compute_coverage_metrics(aggregated)
        return aggregated

    def compute_coverage_metrics(self, results: dict) -> dict:
        """
        Compute functional, performance and safety coverage percentages.

        Coverage is defined as:
          functional_pct  = passed / total  (all suites)
          performance_pct = passed / total  (RT stress suite only)
          safety_pct      = passed / total  (safety / ASIL suites only)
          scenario_pct    = passed / total  (scenario suites only)
        """
        suites: List[TestSuite] = results.get("suites", [])

        def _pct(passed: int, total: int) -> float:
            return round(100.0 * passed / total, 2) if total > 0 else 0.0

        # Overall functional
        total    = results.get("total_tests", 0)
        failures = results.get("total_failures", 0)
        errors   = results.get("total_errors", 0)
        passed   = total - failures - errors

        # Per-category
        rt_passed = rt_total = 0
        safety_passed = safety_total = 0
        scenario_passed = scenario_total = 0

        for suite in suites:
            name_lc = suite.name.lower()
            for tc in suite.test_cases:
                tc_passed = tc.status == "passed"
                if "stress" in name_lc or "rt" in name_lc or "latency" in name_lc:
                    rt_total  += 1
                    rt_passed += 1 if tc_passed else 0
                if ("safety" in name_lc or "asil" in name_lc
                        or "boundary" in name_lc):
                    safety_total  += 1
                    safety_passed += 1 if tc_passed else 0
                if "scenario" in name_lc or "openscenario" in name_lc:
                    scenario_total  += 1
                    scenario_passed += 1 if tc_passed else 0

        return {
            "functional_pct":  _pct(passed, total),
            "performance_pct": _pct(rt_passed, rt_total),
            "safety_pct":      _pct(safety_passed, safety_total),
            "scenario_pct":    _pct(scenario_passed, scenario_total),
            "total_passed":    passed,
            "total_tests":     total,
        }

    def generate_ieee829_xml(self, aggregated: dict, output_path: str) -> None:
        """
        Generate an IEEE 829 compliant test_report.xml.

        IEEE 829-2008 Test Summary Report structure:
          <TestSummaryReport>
            <TestSummaryReportIdentifier/>
            <Summary>
              <TestItems/>
              <EnvironmentDescription/>
              <TestObjectives/>
            </Summary>
            <Variances/>
            <ComprehensiveAssessment/>
            <Results>
              <TestSuite> ... <TestCase/> ... </TestSuite>
            </Results>
            <CoverageMetrics/>
          </TestSummaryReport>
        """
        ts       = aggregated.get("timestamp", datetime.datetime.utcnow().isoformat())
        suites   = aggregated.get("suites", [])
        coverage = aggregated.get("coverage", {})

        # ── Root ────────────────────────────────────────────────────────────
        root = ET.Element("TestSummaryReport")
        root.set("xmlns:xsi",
                 "http://www.w3.org/2001/XMLSchema-instance")
        root.set("standard", "IEEE-829-2008")
        root.set("generated", ts)

        # ── Identifier ──────────────────────────────────────────────────────
        ident = ET.SubElement(root, "TestSummaryReportIdentifier")
        ET.SubElement(ident, "ReportID").text = (
            "TSR-" + ts.replace(":", "-").replace(".", "-")[:19]
        )
        ET.SubElement(ident, "Project").text  = "ROS2 Navigation Stack"
        ET.SubElement(ident, "Version").text  = "1.0.0"
        ET.SubElement(ident, "Date").text     = ts
        ET.SubElement(ident, "Author").text   = "module_6_testing automated pipeline"

        # ── Summary ─────────────────────────────────────────────────────────
        summary = ET.SubElement(root, "Summary")

        items = ET.SubElement(summary, "TestItems")
        ET.SubElement(items, "TotalTests").text    = str(aggregated.get("total_tests", 0))
        ET.SubElement(items, "TotalPassed").text   = str(coverage.get("total_passed", 0))
        ET.SubElement(items, "TotalFailures").text = str(aggregated.get("total_failures", 0))
        ET.SubElement(items, "TotalErrors").text   = str(aggregated.get("total_errors", 0))
        ET.SubElement(items, "TotalTime").text     = f"{aggregated.get('total_time', 0.0):.3f}s"

        env = ET.SubElement(summary, "EnvironmentDescription")
        ET.SubElement(env, "OS").text       = "Ubuntu 22.04 (Jammy)"
        ET.SubElement(env, "ROSVersion").text = "ROS2 Humble"
        ET.SubElement(env, "Simulator").text  = "Gazebo Classic 11"
        ET.SubElement(env, "AsilLevel").text  = "ASIL-B"

        obj = ET.SubElement(summary, "TestObjectives")
        for objective in [
            "Verify navigation stack functional correctness",
            "Validate real-time performance (p99 < 50 ms)",
            "Confirm ASIL-B safety boundary enforcement",
            "Demonstrate scenario coverage >= 90%",
        ]:
            ET.SubElement(obj, "Objective").text = objective

        # ── Variances ───────────────────────────────────────────────────────
        variances = ET.SubElement(root, "Variances")
        if aggregated.get("total_failures", 0) == 0:
            ET.SubElement(variances, "Variance").text = "None — all tests passed."
        else:
            for suite in suites:
                for tc in suite.test_cases:
                    if tc.status in ("failed", "error"):
                        v = ET.SubElement(variances, "Variance")
                        v.set("suite",  suite.name)
                        v.set("test",   tc.name)
                        v.set("status", tc.status)
                        if tc.failure_message:
                            v.text = tc.failure_message

        # ── Comprehensive Assessment ─────────────────────────────────────────
        assessment = ET.SubElement(root, "ComprehensiveAssessment")
        func_pct  = coverage.get("functional_pct", 0.0)
        safe_pct  = coverage.get("safety_pct", 0.0)
        overall   = "PASS" if (func_pct >= 90.0 and safe_pct >= 100.0) else "FAIL"
        ET.SubElement(assessment, "OverallVerdict").text = overall
        ET.SubElement(assessment, "FunctionalCoverage").text = f"{func_pct:.1f}%"
        ET.SubElement(assessment, "SafetyCoverage").text     = f"{safe_pct:.1f}%"
        ET.SubElement(assessment, "PerformanceCoverage").text = (
            f"{coverage.get('performance_pct', 0.0):.1f}%"
        )
        ET.SubElement(assessment, "ScenarioCoverage").text = (
            f"{coverage.get('scenario_pct', 0.0):.1f}%"
        )
        ET.SubElement(assessment, "Recommendation").text = (
            "System ready for integration testing."
            if overall == "PASS"
            else "Review failing tests before proceeding."
        )

        # ── Results ─────────────────────────────────────────────────────────
        results_elem = ET.SubElement(root, "Results")
        for suite in suites:
            suite_elem = ET.SubElement(results_elem, "TestSuite")
            suite_elem.set("name",     suite.name)
            suite_elem.set("tests",    str(suite.tests))
            suite_elem.set("failures", str(suite.failures))
            suite_elem.set("errors",   str(suite.errors))
            suite_elem.set("time",     f"{suite.time:.3f}")

            for tc in suite.test_cases:
                tc_elem = ET.SubElement(suite_elem, "TestCase")
                tc_elem.set("name",      tc.name)
                tc_elem.set("classname", tc.classname)
                tc_elem.set("time",      f"{tc.time:.3f}")
                tc_elem.set("status",    tc.status)

                if tc.failure_message:
                    fail_elem = ET.SubElement(
                        tc_elem,
                        "failure" if tc.status == "failed" else "error",
                    )
                    fail_elem.set("message", tc.failure_message[:200])
                    fail_elem.text = tc.failure_message

                if tc.system_out:
                    out_elem = ET.SubElement(tc_elem, "system-out")
                    out_elem.text = tc.system_out

        # ── Coverage Metrics ─────────────────────────────────────────────────
        cov_elem = ET.SubElement(root, "CoverageMetrics")
        for key, val in coverage.items():
            metric = ET.SubElement(cov_elem, "Metric")
            metric.set("name", key)
            metric.text = str(val)

        # ── Serialise with pretty-print ──────────────────────────────────────
        xml_str = ET.tostring(root, encoding="unicode", xml_declaration=False)
        pretty  = minidom.parseString(xml_str).toprettyxml(indent="  ")
        # Remove the extra XML declaration added by minidom
        lines = pretty.split("\n")
        if lines[0].startswith("<?xml"):
            lines[0] = '<?xml version="1.0" encoding="UTF-8"?>'
        pretty = "\n".join(lines)

        output = Path(output_path)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(pretty, encoding="utf-8")
        print(f"[INFO] IEEE 829 report written to: {output_path}")


# ─────────────────────────────────────────────────────────────────────────────
# main
# ─────────────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Generate an IEEE 829 compliant test report",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--gtest-xml",
                        required=True,
                        help="Path to GTest JUnit XML output file")
    parser.add_argument("--pytest-xml",
                        required=True,
                        help="Path to pytest JUnit XML output file")
    parser.add_argument("--scenario-dir",
                        default="",
                        help="Directory containing OpenSCENARIO JSON result files")
    parser.add_argument("--output",
                        required=True,
                        help="Output path for the IEEE 829 test_report.xml")
    parser.add_argument("--verbose", "-v",
                        action="store_true",
                        help="Print aggregated results to stdout")
    args = parser.parse_args()

    gen = TestReportGenerator()

    print(f"[INFO] Aggregating results …")
    aggregated = gen.aggregate_results(
        gtest_xml=args.gtest_xml,
        pytest_xml=args.pytest_xml,
        scenario_results=args.scenario_dir,
    )

    if args.verbose:
        coverage = aggregated.get("coverage", {})
        print(
            f"\n{'='*60}\n"
            f"  Total tests  : {aggregated['total_tests']}\n"
            f"  Passed       : {coverage.get('total_passed', 0)}\n"
            f"  Failures     : {aggregated['total_failures']}\n"
            f"  Errors       : {aggregated['total_errors']}\n"
            f"  Duration     : {aggregated['total_time']:.2f} s\n"
            f"  Func. cov.   : {coverage.get('functional_pct', 0):.1f}%\n"
            f"  Safety cov.  : {coverage.get('safety_pct', 0):.1f}%\n"
            f"  Perf. cov.   : {coverage.get('performance_pct', 0):.1f}%\n"
            f"  Scenario cov.: {coverage.get('scenario_pct', 0):.1f}%\n"
            f"{'='*60}\n"
        )

    gen.generate_ieee829_xml(aggregated, args.output)

    # Exit non-zero if there were failures
    sys.exit(1 if aggregated["total_failures"] > 0 else 0)


if __name__ == "__main__":
    main()
