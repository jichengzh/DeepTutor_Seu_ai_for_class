cmake_minimum_required(VERSION 3.8)
project(module_4_obstacle)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Find dependencies ────────────────────────────────────────────────────────
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(visualization_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)
find_package(pcl_ros REQUIRED)
find_package(pcl_conversions REQUIRED)
find_package(nav2_costmap_2d REQUIRED)
find_package(diagnostic_updater REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(PCL REQUIRED COMPONENTS common filters segmentation search kdtree)

# ── Common dependency list ───────────────────────────────────────────────────
set(COMMON_DEPS
  rclcpp
  sensor_msgs
  geometry_msgs
  visualization_msgs
  nav_msgs
  tf2_ros
  tf2_geometry_msgs
  pcl_ros
  pcl_conversions
  nav2_costmap_2d
  diagnostic_updater
)

include_directories(
  include
  ${EIGEN3_INCLUDE_DIRS}
  ${PCL_INCLUDE_DIRS}
)

# ── Shared library: obstacle_ekf ─────────────────────────────────────────────
add_library(obstacle_ekf SHARED
  src/obstacle_ekf.cpp
)
target_include_directories(obstacle_ekf PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
  ${EIGEN3_INCLUDE_DIRS}
)
ament_target_dependencies(obstacle_ekf rclcpp)
target_link_libraries(obstacle_ekf Eigen3::Eigen)

# ── Node: euclidean_cluster ──────────────────────────────────────────────────
add_executable(euclidean_cluster
  src/euclidean_cluster.cpp
)
target_include_directories(euclidean_cluster PRIVATE
  include
  ${PCL_INCLUDE_DIRS}
  ${EIGEN3_INCLUDE_DIRS}
)
ament_target_dependencies(euclidean_cluster ${COMMON_DEPS})
target_link_libraries(euclidean_cluster ${PCL_LIBRARIES})

# ── Plugin library: obstacle_layer_ext ──────────────────────────────────────
add_library(obstacle_layer_ext SHARED
  src/obstacle_layer_ext.cpp
)
target_include_directories(obstacle_layer_ext PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(obstacle_layer_ext ${COMMON_DEPS})

# ── Plugin library: teb_nonlinear_plugin ─────────────────────────────────────
add_library(teb_nonlinear_plugin SHARED
  src/teb_nonlinear_plugin.cpp
)
target_include_directories(teb_nonlinear_plugin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
  ${EIGEN3_INCLUDE_DIRS}
)
ament_target_dependencies(teb_nonlinear_plugin rclcpp nav2_costmap_2d)
target_link_libraries(teb_nonlinear_plugin Eigen3::Eigen)

# ── Node: pointcloud_processor ───────────────────────────────────────────────
add_executable(pointcloud_processor
  src/pointcloud_processor.cpp
)
target_include_directories(pointcloud_processor PRIVATE
  include
  ${PCL_INCLUDE_DIRS}
  ${EIGEN3_INCLUDE_DIRS}
)
ament_target_dependencies(pointcloud_processor ${COMMON_DEPS})
target_link_libraries(pointcloud_processor ${PCL_LIBRARIES})

# ── Node: obstacle_kf_tracker ────────────────────────────────────────────────
add_executable(obstacle_kf_tracker
  src/obstacle_kf_tracker.cpp
)
target_include_directories(obstacle_kf_tracker PRIVATE
  include
  ${EIGEN3_INCLUDE_DIRS}
)
ament_target_dependencies(obstacle_kf_tracker ${COMMON_DEPS})
target_link_libraries(obstacle_kf_tracker obstacle_ekf Eigen3::Eigen)

# ── Node: safety_guardian ────────────────────────────────────────────────────
add_executable(safety_guardian
  src/safety_guardian.cpp
)
target_include_directories(safety_guardian PRIVATE
  include
  ${EIGEN3_INCLUDE_DIRS}
)
ament_target_dependencies(safety_guardian ${COMMON_DEPS})
target_link_libraries(safety_guardian Eigen3::Eigen)

# ── GTest ────────────────────────────────────────────────────────────────────
if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_ekf_tracking
    test/test_ekf_tracking.cpp
  )
  target_include_directories(test_ekf_tracking PRIVATE
    include
    ${EIGEN3_INCLUDE_DIRS}
  )
  target_link_libraries(test_ekf_tracking obstacle_ekf Eigen3::Eigen)
  ament_target_dependencies(test_ekf_tracking rclcpp)

  ament_add_gtest(test_collision_cone
    test/test_collision_cone.cpp
  )
  target_include_directories(test_collision_cone PRIVATE
    include
    ${EIGEN3_INCLUDE_DIRS}
  )
  ament_target_dependencies(test_collision_cone rclcpp geometry_msgs visualization_msgs)
endif()

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS
  euclidean_cluster
  pointcloud_processor
  obstacle_kf_tracker
  safety_guardian
  DESTINATION lib/${PROJECT_NAME}
)

install(TARGETS
  obstacle_ekf
  obstacle_layer_ext
  teb_nonlinear_plugin
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(DIRECTORY include/
  DESTINATION include
)

install(DIRECTORY launch/
  DESTINATION share/${PROJECT_NAME}/launch
)

install(DIRECTORY config/
  DESTINATION share/${PROJECT_NAME}/config
)

ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_dependencies(${COMMON_DEPS} Eigen3)
ament_package()
