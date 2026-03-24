cmake_minimum_required(VERSION 3.8)
project(module_1_localization)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# Find dependencies
# ============================================================
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(nav2_util REQUIRED)
find_package(nav2_msgs REQUIRED)
find_package(nav2_costmap_2d REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)
find_package(message_filters REQUIRED)
find_package(robot_localization REQUIRED)
find_package(diagnostic_updater REQUIRED)
find_package(nav2_amcl REQUIRED)

set(COMMON_DEPS
  rclcpp
  rclcpp_lifecycle
  nav2_util
  nav2_msgs
  nav2_costmap_2d
  sensor_msgs
  nav_msgs
  geometry_msgs
  tf2_ros
  tf2_geometry_msgs
  message_filters
  robot_localization
  diagnostic_updater
)

# ============================================================
# Include directories
# ============================================================
include_directories(include)

# ============================================================
# sensor_synchronizer library/node
# ============================================================
add_executable(sensor_synchronizer
  src/sensor_synchronizer.cpp
)
ament_target_dependencies(sensor_synchronizer ${COMMON_DEPS})
target_include_directories(sensor_synchronizer PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

# ============================================================
# tf_aligner library/node
# ============================================================
add_executable(tf_aligner
  src/tf_aligner.cpp
)
ament_target_dependencies(tf_aligner ${COMMON_DEPS})
target_include_directories(tf_aligner PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

# ============================================================
# amcl_likelihood_field node
# ============================================================
add_executable(amcl_likelihood_field
  src/amcl_likelihood_field.cpp
)
ament_target_dependencies(amcl_likelihood_field ${COMMON_DEPS})
target_include_directories(amcl_likelihood_field PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

# ============================================================
# localization_lifecycle node
# ============================================================
add_executable(localization_lifecycle
  src/localization_lifecycle.cpp
  src/data_preprocessor.cpp
  src/pose_estimator.cpp
  src/diagnostic_monitor.cpp
)
ament_target_dependencies(localization_lifecycle ${COMMON_DEPS})
target_include_directories(localization_lifecycle PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

# ============================================================
# Install targets
# ============================================================
install(TARGETS
  sensor_synchronizer
  tf_aligner
  amcl_likelihood_field
  localization_lifecycle
  DESTINATION lib/${PROJECT_NAME}
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

# ============================================================
# Testing
# ============================================================
if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)

  # Suppress copyright/cpplint for this package
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()

  # test_sensor_sync
  ament_add_gtest(test_sensor_sync
    test/test_sensor_sync.cpp
    src/sensor_synchronizer.cpp
  )
  ament_target_dependencies(test_sensor_sync ${COMMON_DEPS})
  target_include_directories(test_sensor_sync PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  )

  # test_tf_aligner
  ament_add_gtest(test_tf_aligner
    test/test_tf_aligner.cpp
    src/tf_aligner.cpp
  )
  ament_target_dependencies(test_tf_aligner ${COMMON_DEPS})
  target_include_directories(test_tf_aligner PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  )
endif()

ament_package()
