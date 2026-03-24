"""
obstacle_avoidance.launch.py
----------------------------
Launches the full obstacle-avoidance pipeline for module_4_obstacle:

  1. pointcloud_processor  – ROI crop, ground removal, height filter
  2. euclidean_cluster     – cluster extraction, bounding-box detection
  3. obstacle_kf_tracker   – EKF-based track management
  4. safety_guardian       – velocity gating on safety violations

The launch file loads per-node config from the config/ directory and
supports a 'use_sim_time' launch argument.
"""

import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, GroupAction, LogInfo
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    pkg_share = FindPackageShare("module_4_obstacle")

    # ── Launch arguments ─────────────────────────────────────────────────────
    use_sim_time_arg = DeclareLaunchArgument(
        "use_sim_time",
        default_value="false",
        description="Use simulation (Gazebo) clock if true",
    )

    log_level_arg = DeclareLaunchArgument(
        "log_level",
        default_value="info",
        description="Logging level (debug, info, warn, error, fatal)",
    )

    # ── Config paths ─────────────────────────────────────────────────────────
    cluster_params   = PathJoinSubstitution([pkg_share, "config", "cluster_params.yaml"])
    ekf_params       = PathJoinSubstitution([pkg_share, "config", "ekf_params.yaml"])

    use_sim_time = LaunchConfiguration("use_sim_time")
    log_level    = LaunchConfiguration("log_level")

    # ── Nodes ────────────────────────────────────────────────────────────────
    pointcloud_processor_node = Node(
        package="module_4_obstacle",
        executable="pointcloud_processor",
        name="pointcloud_processor",
        output="screen",
        arguments=["--ros-args", "--log-level", log_level],
        parameters=[
            {"use_sim_time": use_sim_time},
            {
                "roi_x_min":              -8.0,
                "roi_x_max":               8.0,
                "roi_y_min":              -8.0,
                "roi_y_max":               8.0,
                "roi_z_min":              -0.3,
                "roi_z_max":               3.0,
                "height_filter_min":       0.1,
                "height_filter_max":       2.5,
                "ransac_distance_thresh":  0.05,
                "ransac_max_iterations":   100,
                "voxel_leaf_size":         0.05,
            },
        ],
        remappings=[
            ("/pointcloud", "/lidar/points_raw"),
        ],
    )

    euclidean_cluster_node = Node(
        package="module_4_obstacle",
        executable="euclidean_cluster",
        name="euclidean_cluster",
        output="screen",
        arguments=["--ros-args", "--log-level", log_level],
        parameters=[
            {"use_sim_time": use_sim_time},
            cluster_params,
        ],
        remappings=[
            ("/pointcloud", "/processed_pointcloud"),
        ],
    )

    obstacle_kf_tracker_node = Node(
        package="module_4_obstacle",
        executable="obstacle_kf_tracker",
        name="obstacle_kf_tracker",
        output="screen",
        arguments=["--ros-args", "--log-level", log_level],
        parameters=[
            {"use_sim_time": use_sim_time},
            ekf_params,
            {
                "max_association_dist":  1.5,
                "max_age":               1.0,
                "publish_rate_hz":       10.0,
            },
        ],
    )

    safety_guardian_node = Node(
        package="module_4_obstacle",
        executable="safety_guardian",
        name="safety_guardian",
        output="screen",
        arguments=["--ros-args", "--log-level", log_level],
        parameters=[
            {"use_sim_time": use_sim_time},
            {
                "safety_distance":    1.5,
                "backup_speed":      -0.1,
                "backup_duration_s":  0.5,
                "check_rate_hz":     20.0,
                "stop_on_violation":  True,
            },
        ],
        remappings=[
            ("/cmd_vel_in",  "/cmd_vel_nav"),   # input from planner
            ("/cmd_vel",     "/cmd_vel"),        # output to robot base
        ],
    )

    # ── LaunchDescription ────────────────────────────────────────────────────
    return LaunchDescription([
        use_sim_time_arg,
        log_level_arg,
        LogInfo(msg="[module_4_obstacle] Starting obstacle avoidance pipeline..."),
        pointcloud_processor_node,
        euclidean_cluster_node,
        obstacle_kf_tracker_node,
        safety_guardian_node,
    ])
