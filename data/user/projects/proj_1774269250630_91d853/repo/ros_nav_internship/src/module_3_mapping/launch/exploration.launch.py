"""
exploration.launch.py

Launches the full autonomous frontier exploration stack:
  1. slam_toolbox (async mapping mode)
  2. Nav2 navigation stack (bringup)
  3. frontier_explorer (this package's C++ node)

Usage
─────
  ros2 launch module_3_mapping exploration.launch.py [use_sim_time:=true] [map_yaml:=<path>]

Arguments
─────────
  use_sim_time      (bool, default: false) — use /clock
  world             (str)  — Gazebo world file (only meaningful in sim)
  frontier_params   (str)  — path to frontier_params.yaml; defaults to
                             share/module_3_mapping/config/frontier_params.yaml
  slam_params       (str)  — path to slam_toolbox mapper params YAML
  rviz              (bool, default: false) — launch RViz2 for visualisation
"""

import os
from pathlib import Path

from ament_index_python.packages import get_package_share_directory

from launch import LaunchDescription
from launch.actions import (
    DeclareLaunchArgument,
    GroupAction,
    IncludeLaunchDescription,
    LogInfo,
    OpaqueFunction,
)
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import (
    LaunchConfiguration,
    PathJoinSubstitution,
    PythonExpression,
)
from launch_ros.actions import Node, PushRosNamespace
from launch_ros.substitutions import FindPackageShare


def generate_launch_description() -> LaunchDescription:

    pkg_share = get_package_share_directory("module_3_mapping")

    # ── declare arguments ─────────────────────────────────────────────────────
    declare_use_sim_time = DeclareLaunchArgument(
        "use_sim_time", default_value="false",
        description="Use simulation (Gazebo) clock.")

    declare_frontier_params = DeclareLaunchArgument(
        "frontier_params",
        default_value=os.path.join(pkg_share, "config", "frontier_params.yaml"),
        description="Path to frontier_explorer parameter file.")

    declare_slam_params = DeclareLaunchArgument(
        "slam_params",
        default_value=os.path.join(
            get_package_share_directory("slam_toolbox"),
            "config", "mapper_params_online_async.yaml"),
        description="Path to slam_toolbox mapper parameters.")

    declare_rviz = DeclareLaunchArgument(
        "rviz", default_value="false",
        description="Launch RViz2 for visualisation.")

    declare_namespace = DeclareLaunchArgument(
        "namespace", default_value="",
        description="Top-level ROS namespace.")

    # ── convenient substitutions ──────────────────────────────────────────────
    use_sim_time    = LaunchConfiguration("use_sim_time")
    frontier_params = LaunchConfiguration("frontier_params")
    slam_params     = LaunchConfiguration("slam_params")
    rviz            = LaunchConfiguration("rviz")
    namespace       = LaunchConfiguration("namespace")

    # ── slam_toolbox (async) ──────────────────────────────────────────────────
    slam_toolbox_node = Node(
        package="slam_toolbox",
        executable="async_slam_toolbox_node",
        name="slam_toolbox",
        output="screen",
        parameters=[
            slam_params,
            {"use_sim_time": use_sim_time},
        ],
        remappings=[
            ("/scan", "/scan"),
            ("/map", "/map"),
        ],
    )

    # ── Nav2 bringup (navigation only — no SLAM) ──────────────────────────────
    nav2_bringup_dir = get_package_share_directory("nav2_bringup")
    nav2_launch = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            os.path.join(nav2_bringup_dir, "launch", "navigation_launch.py")
        ),
        launch_arguments={
            "use_sim_time": use_sim_time,
            "use_composition": "False",
        }.items(),
    )

    # ── frontier_explorer ─────────────────────────────────────────────────────
    frontier_explorer_node = Node(
        package="module_3_mapping",
        executable="frontier_explorer",
        name="frontier_explorer",
        output="screen",
        parameters=[
            frontier_params,
            {"use_sim_time": use_sim_time},
        ],
        remappings=[
            ("/map",         "/map"),
            ("/goal_pose",   "/goal_pose"),
        ],
    )

    # ── RViz2 (optional) ──────────────────────────────────────────────────────
    rviz_config = os.path.join(pkg_share, "config", "exploration.rviz")
    rviz_node = Node(
        package="rviz2",
        executable="rviz2",
        name="rviz2",
        arguments=["-d", rviz_config] if Path(rviz_config).exists() else [],
        condition=IfCondition(rviz),
        output="screen",
    )

    # ── info log ──────────────────────────────────────────────────────────────
    log_start = LogInfo(msg="[exploration.launch] Starting autonomous frontier exploration stack.")

    return LaunchDescription([
        # Arguments
        declare_use_sim_time,
        declare_frontier_params,
        declare_slam_params,
        declare_rviz,
        declare_namespace,
        # Info
        log_start,
        # Nodes
        slam_toolbox_node,
        nav2_launch,
        frontier_explorer_node,
        rviz_node,
    ])
