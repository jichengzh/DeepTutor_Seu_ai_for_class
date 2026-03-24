cmake_minimum_required(VERSION 3.8)
project(module_2_planning)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ────────────────────────────────────────────────────────────
# find_package dependencies
# ────────────────────────────────────────────────────────────
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(nav2_core REQUIRED)
find_package(nav2_costmap_2d REQUIRED)
find_package(nav2_util REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(visualization_msgs REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)
find_package(pluginlib REQUIRED)
find_package(dwb_core REQUIRED)
find_package(dwb_msgs REQUIRED)
find_package(nav2_msgs REQUIRED)

# ────────────────────────────────────────────────────────────
# Common dependency list helper
# ────────────────────────────────────────────────────────────
set(COMMON_DEPS
  rclcpp
  rclcpp_lifecycle
  nav2_core
  nav2_costmap_2d
  nav2_util
  nav_msgs
  geometry_msgs
  visualization_msgs
  tf2_ros
  tf2_geometry_msgs
  pluginlib
  dwb_core
  dwb_msgs
  nav2_msgs
)

# ────────────────────────────────────────────────────────────
# Include directories
# ────────────────────────────────────────────────────────────
include_directories(include)

# ────────────────────────────────────────────────────────────
# 1. hybrid_astar_planner  (nav2 GlobalPlanner plugin)
# ────────────────────────────────────────────────────────────
add_library(hybrid_astar_planner SHARED
  src/hybrid_astar_planner.cpp
)
target_include_directories(hybrid_astar_planner PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(hybrid_astar_planner ${COMMON_DEPS})
pluginlib_export_plugin_description_file(nav2_core hybrid_astar_planner_plugin.xml)

# ────────────────────────────────────────────────────────────
# 2. dynamic_obstacle_predictor  (composable node)
# ────────────────────────────────────────────────────────────
add_library(dynamic_obstacle_predictor SHARED
  src/dynamic_obstacle_predictor.cpp
)
target_include_directories(dynamic_obstacle_predictor PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(dynamic_obstacle_predictor ${COMMON_DEPS})

# ────────────────────────────────────────────────────────────
# 3. dwb_predictor_plugin  (DWB TrajectoryCritic plugin)
# ────────────────────────────────────────────────────────────
add_library(dwb_predictor_plugin SHARED
  src/dwb_predictor_plugin.cpp
)
target_include_directories(dwb_predictor_plugin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(dwb_predictor_plugin ${COMMON_DEPS})
pluginlib_export_plugin_description_file(dwb_core dwb_predictor_plugin.xml)

# ────────────────────────────────────────────────────────────
# 4. path_searcher  (standalone executable node)
# ────────────────────────────────────────────────────────────
add_executable(path_searcher
  src/path_searcher.cpp
)
target_include_directories(path_searcher PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(path_searcher ${COMMON_DEPS})
target_link_libraries(path_searcher hybrid_astar_planner)

# ────────────────────────────────────────────────────────────
# 5. trajectory_optimizer  (standalone executable node)
# ────────────────────────────────────────────────────────────
add_executable(trajectory_optimizer
  src/trajectory_optimizer.cpp
)
target_include_directories(trajectory_optimizer PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(trajectory_optimizer ${COMMON_DEPS})

# ────────────────────────────────────────────────────────────
# 6. velocity_clipper  (standalone executable node)
# ────────────────────────────────────────────────────────────
add_executable(velocity_clipper
  src/velocity_clipper.cpp
)
target_include_directories(velocity_clipper PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(velocity_clipper ${COMMON_DEPS})

# ────────────────────────────────────────────────────────────
# GTest targets
# ────────────────────────────────────────────────────────────
if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)

  ament_lint_auto_find_test_dependencies()

  ament_add_gtest(test_hybrid_astar
    test/test_hybrid_astar.cpp
  )
  target_include_directories(test_hybrid_astar PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  )
  ament_target_dependencies(test_hybrid_astar ${COMMON_DEPS})
  target_link_libraries(test_hybrid_astar hybrid_astar_planner)

  ament_add_gtest(test_trajectory_opt
    test/test_trajectory_opt.cpp
  )
  target_include_directories(test_trajectory_opt PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  )
  ament_target_dependencies(test_trajectory_opt ${COMMON_DEPS})
endif()

# ────────────────────────────────────────────────────────────
# Install
# ────────────────────────────────────────────────────────────
install(TARGETS
  hybrid_astar_planner
  dynamic_obstacle_predictor
  dwb_predictor_plugin
  path_searcher
  trajectory_optimizer
  velocity_clipper
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY include/
  DESTINATION include/
)

install(DIRECTORY launch config
  DESTINATION share/${PROJECT_NAME}/
)

install(FILES
  hybrid_astar_planner_plugin.xml
  dwb_predictor_plugin.xml
  DESTINATION share/${PROJECT_NAME}/
)

ament_package()
