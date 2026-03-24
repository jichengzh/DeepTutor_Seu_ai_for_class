cmake_minimum_required(VERSION 3.8)
project(module_5_multi_robot)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ─── Find packages ────────────────────────────────────────────────────────────
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(nav2_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(visualization_msgs REQUIRED)
find_package(rosbag2_cpp REQUIRED)

# ─── Include directories ──────────────────────────────────────────────────────
include_directories(include)

# ─── Common dependencies list ─────────────────────────────────────────────────
set(COMMON_DEPS
  rclcpp
  rclcpp_action
  nav_msgs
  nav2_msgs
  geometry_msgs
  tf2_ros
  tf2
  tf2_geometry_msgs
  std_msgs
  visualization_msgs
  rosbag2_cpp
)

# ─── cbba_allocator ───────────────────────────────────────────────────────────
add_executable(cbba_allocator
  src/cbba_allocator.cpp
)
ament_target_dependencies(cbba_allocator ${COMMON_DEPS})
target_include_directories(cbba_allocator PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

# ─── fleet_executor ───────────────────────────────────────────────────────────
add_executable(fleet_executor
  src/fleet_executor.cpp
)
ament_target_dependencies(fleet_executor ${COMMON_DEPS})
target_include_directories(fleet_executor PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

# ─── cross_robot_tf ───────────────────────────────────────────────────────────
add_executable(cross_robot_tf
  src/cross_robot_tf.cpp
)
ament_target_dependencies(cross_robot_tf ${COMMON_DEPS})
target_include_directories(cross_robot_tf PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

# ─── rstc_preplanner ──────────────────────────────────────────────────────────
add_executable(rstc_preplanner
  src/rstc_preplanner.cpp
)
ament_target_dependencies(rstc_preplanner ${COMMON_DEPS})
target_include_directories(rstc_preplanner PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

# ─── conflict_resolver ────────────────────────────────────────────────────────
add_executable(conflict_resolver
  src/conflict_resolver.cpp
)
ament_target_dependencies(conflict_resolver ${COMMON_DEPS})
target_include_directories(conflict_resolver PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

# ─── Install executables ──────────────────────────────────────────────────────
install(TARGETS
  cbba_allocator
  fleet_executor
  cross_robot_tf
  rstc_preplanner
  conflict_resolver
  DESTINATION lib/${PROJECT_NAME}
)

# ─── Install headers ──────────────────────────────────────────────────────────
install(DIRECTORY include/
  DESTINATION include
)

# ─── Install scripts ──────────────────────────────────────────────────────────
install(PROGRAMS
  scripts/task_logger.py
  scripts/fleet_bag_parser.py
  DESTINATION lib/${PROJECT_NAME}
)

# ─── Install launch files ─────────────────────────────────────────────────────
install(DIRECTORY launch/
  DESTINATION share/${PROJECT_NAME}/launch
)

# ─── Install config files ─────────────────────────────────────────────────────
install(DIRECTORY config/
  DESTINATION share/${PROJECT_NAME}/config
)

# ─── Tests ────────────────────────────────────────────────────────────────────
if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()

  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_cbba_allocation
    test/test_cbba_allocation.cpp
    src/cbba_allocator.cpp
  )
  ament_target_dependencies(test_cbba_allocation ${COMMON_DEPS})
  target_include_directories(test_cbba_allocation PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  )

  ament_add_gtest(test_conflict_resolution
    test/test_conflict_resolution.cpp
    src/conflict_resolver.cpp
  )
  ament_target_dependencies(test_conflict_resolution ${COMMON_DEPS})
  target_include_directories(test_conflict_resolution PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  )
endif()

ament_package()
