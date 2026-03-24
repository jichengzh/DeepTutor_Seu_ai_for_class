cmake_minimum_required(VERSION 3.8)
project(module_3_mapping)

# Default to C++17
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ── find dependencies ──────────────────────────────────────────────────────────
find_package(ament_cmake REQUIRED)
find_package(ament_cmake_python REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(visualization_msgs REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)
find_package(nav2_costmap_2d REQUIRED)
find_package(nav2_msgs REQUIRED)

# ── include directories ────────────────────────────────────────────────────────
include_directories(include)

# ── frontier_explorer (C++) ────────────────────────────────────────────────────
add_executable(frontier_explorer
  src/frontier_explorer.cpp
)

ament_target_dependencies(frontier_explorer
  rclcpp
  rclcpp_action
  nav_msgs
  geometry_msgs
  std_msgs
  sensor_msgs
  visualization_msgs
  tf2_ros
  tf2_geometry_msgs
  nav2_costmap_2d
  nav2_msgs
)

# ── semantic_map_merger (C++) ──────────────────────────────────────────────────
add_executable(semantic_map_merger
  src/semantic_map_merger.cpp
)

ament_target_dependencies(semantic_map_merger
  rclcpp
  nav_msgs
  geometry_msgs
  std_msgs
  visualization_msgs
  tf2_ros
  tf2_geometry_msgs
)

# ── Python package (scripts/) ──────────────────────────────────────────────────
ament_python_install_package(${PROJECT_NAME})

# ── install targets ────────────────────────────────────────────────────────────
install(TARGETS
  frontier_explorer
  semantic_map_merger
  DESTINATION lib/${PROJECT_NAME}
)

install(PROGRAMS
  scripts/semantic_annotator.py
  scripts/map_metadata_store.py
  scripts/slam_toolbox_bridge.py
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
  models/
  DESTINATION share/${PROJECT_NAME}/models
)

# ── tests ──────────────────────────────────────────────────────────────────────
if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_cmake_pytest REQUIRED)

  ament_lint_auto_find_test_dependencies()

  # C++ GTest
  ament_add_gtest(test_frontier_detection
    test/test_frontier_detection.cpp
    src/frontier_explorer.cpp
  )
  ament_target_dependencies(test_frontier_detection
    rclcpp
    rclcpp_action
    nav_msgs
    geometry_msgs
    std_msgs
    sensor_msgs
    visualization_msgs
    tf2_ros
    tf2_geometry_msgs
    nav2_costmap_2d
    nav2_msgs
  )
  target_include_directories(test_frontier_detection PRIVATE include)

  # Python pytest
  ament_add_pytest_test(test_semantic_merger
    test/test_semantic_merger.py
    TIMEOUT 60
  )
endif()

ament_package()
