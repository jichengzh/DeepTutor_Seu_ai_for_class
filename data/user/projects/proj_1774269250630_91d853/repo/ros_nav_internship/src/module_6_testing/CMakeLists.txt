cmake_minimum_required(VERSION 3.8)
project(module_6_testing)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ─── Dependencies ───────────────────────────────────────────────────────────
find_package(ament_cmake REQUIRED)
find_package(ament_cmake_python REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(std_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(diagnostic_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(visualization_msgs REQUIRED)

# ─── Include dirs ────────────────────────────────────────────────────────────
include_directories(include)

# ─── scenario_runner ────────────────────────────────────────────────────────
add_executable(scenario_runner
  src/scenario_runner.cpp
)
ament_target_dependencies(scenario_runner
  rclcpp
  rclcpp_lifecycle
  std_msgs
  nav_msgs
  geometry_msgs
)
target_include_directories(scenario_runner PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

# ─── rt_stress_tester ───────────────────────────────────────────────────────
add_executable(rt_stress_tester
  src/rt_stress_tester.cpp
)
ament_target_dependencies(rt_stress_tester
  rclcpp
  rclcpp_lifecycle
  std_msgs
  nav_msgs
  sensor_msgs
)
target_include_directories(rt_stress_tester PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

# ─── safety_boundary_checker ────────────────────────────────────────────────
add_executable(safety_boundary_checker
  src/safety_boundary_checker.cpp
)
ament_target_dependencies(safety_boundary_checker
  rclcpp
  rclcpp_lifecycle
  std_msgs
  geometry_msgs
  diagnostic_msgs
  visualization_msgs
)
target_include_directories(safety_boundary_checker PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

# ─── Python scripts ─────────────────────────────────────────────────────────
ament_python_install_package(scripts)

# ─── Install executables ────────────────────────────────────────────────────
install(TARGETS
  scenario_runner
  rt_stress_tester
  safety_boundary_checker
  DESTINATION lib/${PROJECT_NAME}
)

install(PROGRAMS
  scripts/report_generator.py
  scripts/perf_analyzer.py
  scripts/run_all_tests.sh
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY
  include/
  DESTINATION include
)

install(DIRECTORY
  launch/
  DESTINATION share/${PROJECT_NAME}/launch
)

install(DIRECTORY
  config/
  DESTINATION share/${PROJECT_NAME}/config
)

install(DIRECTORY
  scenarios/
  DESTINATION share/${PROJECT_NAME}/scenarios
)

install(DIRECTORY
  results/
  DESTINATION share/${PROJECT_NAME}/results
)

# ─── GTest with lcov coverage ────────────────────────────────────────────────
if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()

  # Coverage flags
  set(COVERAGE_FLAGS -fprofile-arcs -ftest-coverage)

  # ── unit test: scenario_runner ──
  ament_add_gtest(test_scenario_runner
    test/test_scenario_runner.cpp
  )
  if(TARGET test_scenario_runner)
    ament_target_dependencies(test_scenario_runner rclcpp std_msgs nav_msgs geometry_msgs)
    target_include_directories(test_scenario_runner PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
    target_compile_options(test_scenario_runner PRIVATE ${COVERAGE_FLAGS})
    target_link_options(test_scenario_runner PRIVATE ${COVERAGE_FLAGS})
  endif()

  # ── unit test: rt_stress_tester ──
  ament_add_gtest(test_rt_stress_tester
    test/test_rt_stress_tester.cpp
  )
  if(TARGET test_rt_stress_tester)
    ament_target_dependencies(test_rt_stress_tester rclcpp sensor_msgs nav_msgs)
    target_include_directories(test_rt_stress_tester PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
    target_compile_options(test_rt_stress_tester PRIVATE ${COVERAGE_FLAGS})
    target_link_options(test_rt_stress_tester PRIVATE ${COVERAGE_FLAGS})
  endif()

  # ── unit test: safety_boundary_checker ──
  ament_add_gtest(test_safety_boundary_checker
    test/test_safety_boundary_checker.cpp
  )
  if(TARGET test_safety_boundary_checker)
    ament_target_dependencies(test_safety_boundary_checker rclcpp geometry_msgs diagnostic_msgs visualization_msgs)
    target_include_directories(test_safety_boundary_checker PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
    target_compile_options(test_safety_boundary_checker PRIVATE ${COVERAGE_FLAGS})
    target_link_options(test_safety_boundary_checker PRIVATE ${COVERAGE_FLAGS})
  endif()
endif()

ament_package()
