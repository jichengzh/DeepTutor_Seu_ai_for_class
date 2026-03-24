"""
localization.launch.py
======================
Complete ROS2 launch file for the module_1_localization stack.

Launches:
  1. nav2_amcl            – AMCL particle-filter localization node
  2. robot_localization   – EKF sensor fusion (odom + IMU)
  3. tf_aligner           – Static TF broadcaster (base_link -> laser/imu)
  4. sensor_synchronizer  – ApproximateTime sensor synchronizer
  5. localization_lifecycle – Full lifecycle localization manager

All nodes are configured via YAML parameter files under
share/module_1_localization/config/.
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import (
    DeclareLaunchArgument,
    GroupAction,
    IncludeLaunchDescription,
    LogInfo,
)
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node, SetParameter
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    # ----------------------------------------------------------------
    # Package directories
    # ----------------------------------------------------------------
    pkg_dir = get_package_share_directory("module_1_localization")
    config_dir = os.path.join(pkg_dir, "config")

    # ----------------------------------------------------------------
    # Launch arguments
    # ----------------------------------------------------------------
    use_sim_time_arg = DeclareLaunchArgument(
        "use_sim_time",
        default_value="false",
        description="Use simulation (Gazebo) clock if true",
    )
    amcl_params_arg = DeclareLaunchArgument(
        "amcl_params",
        default_value=os.path.join(config_dir, "amcl_params.yaml"),
        description="Full path to AMCL parameter YAML file",
    )
    ekf_params_arg = DeclareLaunchArgument(
        "ekf_params",
        default_value=os.path.join(config_dir, "ekf_params.yaml"),
        description="Full path to robot_localization EKF parameter YAML file",
    )
    sync_params_arg = DeclareLaunchArgument(
        "sync_params",
        default_value=os.path.join(config_dir, "sensor_sync_params.yaml"),
        description="Full path to sensor synchronizer parameter YAML file",
    )
    map_topic_arg = DeclareLaunchArgument(
        "map_topic",
        default_value="map",
        description="Topic on which the occupancy grid map is published",
    )
    scan_topic_arg = DeclareLaunchArgument(
        "scan_topic",
        default_value="scan",
        description="Topic on which LaserScan messages are published",
    )
    log_level_arg = DeclareLaunchArgument(
        "log_level",
        default_value="info",
        description="ROS2 logging level (debug|info|warn|error|fatal)",
    )

    # Convenience substitutions
    use_sim_time = LaunchConfiguration("use_sim_time")
    amcl_params  = LaunchConfiguration("amcl_params")
    ekf_params   = LaunchConfiguration("ekf_params")
    sync_params  = LaunchConfiguration("sync_params")

    # ----------------------------------------------------------------
    # 1. AMCL
    # ----------------------------------------------------------------
    amcl_node = Node(
        package="nav2_amcl",
        executable="amcl",
        name="amcl",
        output="screen",
        respawn=True,
        respawn_delay=2.0,
        parameters=[
            amcl_params,
            {"use_sim_time": use_sim_time},
        ],
        arguments=["--ros-args", "--log-level", LaunchConfiguration("log_level")],
    )

    # ----------------------------------------------------------------
    # 2. robot_localization EKF
    # ----------------------------------------------------------------
    ekf_node = Node(
        package="robot_localization",
        executable="ekf_node",
        name="ekf_filter_node",
        output="screen",
        parameters=[
            ekf_params,
            {"use_sim_time": use_sim_time},
        ],
        remappings=[
            ("odometry/filtered", "odometry/filtered"),
            ("accel/filtered",    "accel/filtered"),
        ],
    )

    # ----------------------------------------------------------------
    # 3. TF Aligner
    # ----------------------------------------------------------------
    tf_aligner_node = Node(
        package="module_1_localization",
        executable="tf_aligner",
        name="tf_aligner",
        output="screen",
        parameters=[
            {
                "use_sim_time":   use_sim_time,
                "parent_frame":   "base_link",
                "laser_frame":    "laser",
                "imu_frame":      "imu_link",
                "laser_x":        0.0,
                "laser_y":        0.0,
                "laser_z":        0.18,
                "laser_roll":     0.0,
                "laser_pitch":    0.0,
                "laser_yaw":      0.0,
                "imu_x":          0.0,
                "imu_y":          0.0,
                "imu_z":          0.1,
                "imu_roll":       0.0,
                "imu_pitch":      0.0,
                "imu_yaw":        0.0,
                "broadcast_rate": 1.0,
            }
        ],
    )

    # ----------------------------------------------------------------
    # 4. Sensor Synchronizer
    # ----------------------------------------------------------------
    sensor_sync_node = Node(
        package="module_1_localization",
        executable="sensor_synchronizer",
        name="sensor_synchronizer",
        output="screen",
        parameters=[
            sync_params,
            {"use_sim_time": use_sim_time},
        ],
    )

    # ----------------------------------------------------------------
    # 5. Localization Lifecycle Node
    # ----------------------------------------------------------------
    lifecycle_node = Node(
        package="module_1_localization",
        executable="localization_lifecycle",
        name="localization_lifecycle",
        output="screen",
        parameters=[
            {
                "use_sim_time":  use_sim_time,
                "global_frame":  "map",
                "odom_frame":    "odom",
                "base_frame":    "base_footprint",
                "scan_topic":    LaunchConfiguration("scan_topic"),
                "imu_topic":     "imu/data",
                "odom_topic":    "odom",
                "update_rate":   10.0,
            }
        ],
    )

    # ----------------------------------------------------------------
    # Nav2 Lifecycle Manager for AMCL
    # ----------------------------------------------------------------
    lifecycle_manager = Node(
        package="nav2_lifecycle_manager",
        executable="lifecycle_manager",
        name="lifecycle_manager_localization",
        output="screen",
        parameters=[
            {
                "use_sim_time":   use_sim_time,
                "autostart":      True,
                "node_names":     ["amcl"],
                "bond_timeout":   4.0,
            }
        ],
    )

    # ----------------------------------------------------------------
    # Assemble LaunchDescription
    # ----------------------------------------------------------------
    return LaunchDescription([
        # Arguments
        use_sim_time_arg,
        amcl_params_arg,
        ekf_params_arg,
        sync_params_arg,
        map_topic_arg,
        scan_topic_arg,
        log_level_arg,

        # Nodes
        LogInfo(msg="[module_1_localization] Starting localization stack..."),
        tf_aligner_node,
        sensor_sync_node,
        amcl_node,
        ekf_node,
        lifecycle_node,
        lifecycle_manager,
    ])
