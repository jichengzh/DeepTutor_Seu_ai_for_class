"""
planning.launch.py — Launch the full Module 2 Planning stack.

Nodes started:
  1. path_searcher          — Hybrid A* planner (lifecycle node)
  2. dynamic_obstacle_predictor — Kalman-filter obstacle predictor
  3. trajectory_optimizer   — B-spline path smoother
  4. velocity_clipper       — Velocity/acceleration limiter

The DWB predictor critic is loaded inside the Nav2 controller_server;
its config is provided in dwb_params.yaml.
"""

import os
from launch import LaunchDescription
from launch.actions import (
    DeclareLaunchArgument,
    GroupAction,
    IncludeLaunchDescription,
    LogInfo,
)
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node, LifecycleNode
from launch_ros.substitutions import FindPackageShare
from launch.conditions import IfCondition


def generate_launch_description():
    pkg = FindPackageShare("module_2_planning")

    # ── Launch arguments ─────────────────────────────────────────────────────
    use_sim_time_arg = DeclareLaunchArgument(
        "use_sim_time",
        default_value="false",
        description="Use simulation (Gazebo) clock if true",
    )
    params_file_arg = DeclareLaunchArgument(
        "params_file",
        default_value=PathJoinSubstitution([pkg, "config", "hybrid_astar_params.yaml"]),
        description="Full path to the hybrid A* parameter file",
    )
    dwb_params_file_arg = DeclareLaunchArgument(
        "dwb_params_file",
        default_value=PathJoinSubstitution([pkg, "config", "dwb_params.yaml"]),
        description="Full path to DWB controller parameter file",
    )

    use_sim_time  = LaunchConfiguration("use_sim_time")
    params_file   = LaunchConfiguration("params_file")

    # ── path_searcher  (Lifecycle node) ──────────────────────────────────────
    path_searcher_node = LifecycleNode(
        package="module_2_planning",
        executable="path_searcher",
        name="path_searcher",
        namespace="",
        output="screen",
        parameters=[
            params_file,
            {"use_sim_time": use_sim_time},
        ],
    )

    # ── dynamic_obstacle_predictor ───────────────────────────────────────────
    predictor_node = Node(
        package="module_2_planning",
        executable="dynamic_obstacle_predictor",
        name="dynamic_obstacle_predictor",
        output="screen",
        parameters=[
            {
                "use_sim_time":      use_sim_time,
                "prediction_steps":  10,
                "prediction_dt":     0.1,
                "grid_resolution":   0.1,
                "grid_half_width":   10.0,
                "fixed_frame":       "map",
            }
        ],
    )

    # ── trajectory_optimizer ─────────────────────────────────────────────────
    optimizer_node = Node(
        package="module_2_planning",
        executable="trajectory_optimizer",
        name="trajectory_optimizer",
        output="screen",
        parameters=[
            {
                "use_sim_time":           use_sim_time,
                "interpolation_density":  5,
                "smoothing_weight":       0.8,
                "min_path_points":        3,
            }
        ],
    )

    # ── velocity_clipper ─────────────────────────────────────────────────────
    clipper_node = Node(
        package="module_2_planning",
        executable="velocity_clipper",
        name="velocity_clipper",
        output="screen",
        parameters=[
            {
                "use_sim_time":   use_sim_time,
                "v_max_linear":   1.0,
                "v_max_angular":  1.0,
                "a_max_linear":   0.5,
                "a_max_angular":  1.0,
            }
        ],
        remappings=[
            ("/cmd_vel_raw", "/cmd_vel_raw"),
            ("/cmd_vel",     "/cmd_vel"),
        ],
    )

    return LaunchDescription(
        [
            use_sim_time_arg,
            params_file_arg,
            dwb_params_file_arg,
            LogInfo(msg="Starting Module 2 Planning stack..."),
            path_searcher_node,
            predictor_node,
            optimizer_node,
            clipper_node,
        ]
    )
