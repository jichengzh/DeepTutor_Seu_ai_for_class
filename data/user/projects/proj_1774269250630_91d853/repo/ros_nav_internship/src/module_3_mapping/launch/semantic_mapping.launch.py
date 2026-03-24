"""
semantic_mapping.launch.py

Launches the semantic mapping pipeline:
  1. semantic_annotator.py   — YOLOv8 image annotation node
  2. semantic_map_merger     — C++ node that merges two annotation streams
  3. slam_toolbox_bridge.py  — exports combined SLAM + semantic map package

Usage
─────
  ros2 launch module_3_mapping semantic_mapping.launch.py [use_sim_time:=true]

Arguments
─────────
  use_sim_time            (bool,  default: false)
  yolov8_params           (str)   path to yolov8_params.yaml
  model_path              (str)   path to YOLOv8 .pt file
  db_path                 (str)   SQLite database path for metadata store
  output_dir              (str)   export output directory
  map_name                (str)   base name of exported map files
  auto_export_interval    (float) bridge auto-export interval in seconds; 0=off
  semantic_topic_a        (str)   first annotation stream topic
  semantic_topic_b        (str)   second annotation stream topic (may equal _a)
  rviz                    (bool,  default: false)
"""

import os
from pathlib import Path

from ament_index_python.packages import get_package_share_directory

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, LogInfo
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description() -> LaunchDescription:

    pkg_share = get_package_share_directory("module_3_mapping")
    yolov8_default_params = os.path.join(pkg_share, "config", "yolov8_params.yaml")

    # ── arguments ─────────────────────────────────────────────────────────────
    declare_use_sim_time = DeclareLaunchArgument(
        "use_sim_time", default_value="false",
        description="Use simulation clock.")

    declare_yolov8_params = DeclareLaunchArgument(
        "yolov8_params",
        default_value=yolov8_default_params,
        description="Path to YOLOv8 annotator parameter file.")

    declare_model_path = DeclareLaunchArgument(
        "model_path",
        default_value=os.path.join(pkg_share, "models", "yolov8n.pt"),
        description="Path to YOLOv8 model weights.")

    declare_db_path = DeclareLaunchArgument(
        "db_path",
        default_value="/tmp/semantic_map.db",
        description="SQLite metadata database path.")

    declare_output_dir = DeclareLaunchArgument(
        "output_dir",
        default_value="/tmp/map_export",
        description="Directory for exported map packages.")

    declare_map_name = DeclareLaunchArgument(
        "map_name",
        default_value="semantic_map",
        description="Base name for all exported map files.")

    declare_auto_export = DeclareLaunchArgument(
        "auto_export_interval",
        default_value="0.0",
        description="Auto-export every N seconds (0 = disabled).")

    declare_topic_a = DeclareLaunchArgument(
        "semantic_topic_a",
        default_value="/semantic_annotations",
        description="First semantic annotation stream topic.")

    declare_topic_b = DeclareLaunchArgument(
        "semantic_topic_b",
        default_value="/semantic_annotations",
        description="Second semantic annotation stream topic.")

    declare_rviz = DeclareLaunchArgument(
        "rviz", default_value="false",
        description="Launch RViz2.")

    # ── substitutions ──────────────────────────────────────────────────────────
    use_sim_time         = LaunchConfiguration("use_sim_time")
    yolov8_params        = LaunchConfiguration("yolov8_params")
    model_path           = LaunchConfiguration("model_path")
    db_path              = LaunchConfiguration("db_path")
    output_dir           = LaunchConfiguration("output_dir")
    map_name             = LaunchConfiguration("map_name")
    auto_export_interval = LaunchConfiguration("auto_export_interval")
    semantic_topic_a     = LaunchConfiguration("semantic_topic_a")
    semantic_topic_b     = LaunchConfiguration("semantic_topic_b")
    rviz                 = LaunchConfiguration("rviz")

    # ── semantic_annotator (Python) ───────────────────────────────────────────
    semantic_annotator_node = Node(
        package="module_3_mapping",
        executable="semantic_annotator.py",
        name="semantic_annotator",
        output="screen",
        parameters=[
            yolov8_params,
            {
                "use_sim_time": use_sim_time,
                "model_path":   model_path,
            },
        ],
    )

    # ── semantic_map_merger (C++) ─────────────────────────────────────────────
    semantic_map_merger_node = Node(
        package="module_3_mapping",
        executable="semantic_map_merger",
        name="semantic_map_merger",
        output="screen",
        parameters=[{
            "use_sim_time":     use_sim_time,
            "semantic_topic_a": semantic_topic_a,
            "semantic_topic_b": semantic_topic_b,
            "merged_topic":     "/semantic_map_merged",
            "iou_threshold":    0.5,
        }],
    )

    # ── slam_toolbox_bridge (Python) ──────────────────────────────────────────
    slam_bridge_node = Node(
        package="module_3_mapping",
        executable="slam_toolbox_bridge.py",
        name="slam_toolbox_bridge",
        output="screen",
        parameters=[{
            "use_sim_time":          use_sim_time,
            "db_path":               db_path,
            "output_dir":            output_dir,
            "map_name":              map_name,
            "auto_export_interval":  auto_export_interval,
        }],
    )

    # ── RViz2 (optional) ──────────────────────────────────────────────────────
    rviz_config = os.path.join(pkg_share, "config", "semantic_mapping.rviz")
    rviz_node = Node(
        package="rviz2",
        executable="rviz2",
        name="rviz2",
        arguments=["-d", rviz_config] if Path(rviz_config).exists() else [],
        condition=IfCondition(rviz),
        output="screen",
    )

    log_start = LogInfo(
        msg="[semantic_mapping.launch] Starting semantic mapping pipeline.")

    return LaunchDescription([
        # Arguments
        declare_use_sim_time,
        declare_yolov8_params,
        declare_model_path,
        declare_db_path,
        declare_output_dir,
        declare_map_name,
        declare_auto_export,
        declare_topic_a,
        declare_topic_b,
        declare_rviz,
        # Info
        log_start,
        # Nodes
        semantic_annotator_node,
        semantic_map_merger_node,
        slam_bridge_node,
        rviz_node,
    ])
