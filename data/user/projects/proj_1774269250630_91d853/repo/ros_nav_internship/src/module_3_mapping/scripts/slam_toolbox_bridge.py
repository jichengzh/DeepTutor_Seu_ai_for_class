#!/usr/bin/env python3
"""
slam_toolbox_bridge.py — Combine SLAM Toolbox maps with semantic metadata.

This ROS 2 node:
  1. Calls the slam_toolbox SerializePoseGraph service to save the current map.
  2. Reads semantic metadata from a MapMetadataStore SQLite database.
  3. Writes semantic annotations into the map YAML sidecar file.
  4. Exports a combined package:
       <output_dir>/
         <map_name>.pgm        — occupancy grid (already written by slam_toolbox)
         <map_name>.yaml       — map metadata + semantic annotations section
         <map_name>_semantic.json — full semantic objects export

Parameters
──────────
  db_path              (str)  path to the SQLite metadata database
  output_dir           (str)  directory to write the map package
  map_name             (str)  base name used for all output files
  slam_serialize_topic (str)  slam_toolbox SerializePoseGraph service name
  auto_export_interval (float) if > 0, export on a timer [seconds]; 0 = manual

Usage (manual trigger via ROS 2 service)
─────────────────────────────────────────
  ros2 service call /slam_toolbox_bridge/export std_srvs/srv/Trigger {}
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Optional

import rclpy
import rclpy.duration
from rclpy.node import Node

from std_srvs.srv import Trigger

# Import the metadata store from the same package
# When installed via ament, the package root is on sys.path.
try:
    from module_3_mapping.scripts.map_metadata_store import (
        MapMetadataStore,
        SemanticObject,
    )
except ImportError:
    # Fallback for direct execution / unit tests
    import sys
    sys.path.insert(0, str(Path(__file__).parent))
    from map_metadata_store import MapMetadataStore, SemanticObject  # type: ignore

# slam_toolbox service types — guard import so the node still loads if the
# slam_toolbox_msgs package is not present in the build environment.
try:
    from slam_toolbox.srv import SerializePoseGraph  # type: ignore
    SLAM_TOOLBOX_AVAILABLE = True
except ImportError:
    SLAM_TOOLBOX_AVAILABLE = False


class SlamToolboxBridge(Node):
    """
    Bridge between SLAM Toolbox and the semantic metadata store.

    On each export cycle the node:
      1. Requests slam_toolbox to serialise its pose graph to a .pgm/.yaml pair.
      2. Patches the .yaml to include a 'semantic_objects' section.
      3. Exports the semantic database to a side-car .json file.
    """

    def __init__(self) -> None:
        super().__init__("slam_toolbox_bridge")

        # ── parameters ────────────────────────────────────────────────────────
        self.declare_parameter("db_path",              "/tmp/semantic_map.db")
        self.declare_parameter("output_dir",           "/tmp/map_export")
        self.declare_parameter("map_name",             "exported_map")
        self.declare_parameter("slam_serialize_topic", "/slam_toolbox/serialize_map")
        self.declare_parameter("auto_export_interval",  0.0)  # seconds; 0 = off

        self._db_path    = self.get_parameter("db_path").value
        self._output_dir = Path(self.get_parameter("output_dir").value)
        self._map_name   = self.get_parameter("map_name").value
        self._slam_topic = self.get_parameter("slam_serialize_topic").value
        interval         = float(self.get_parameter("auto_export_interval").value)

        self._output_dir.mkdir(parents=True, exist_ok=True)

        # ── slam_toolbox client ───────────────────────────────────────────────
        self._slam_client: Optional[rclpy.client.Client] = None
        if SLAM_TOOLBOX_AVAILABLE:
            self._slam_client = self.create_client(
                SerializePoseGraph, self._slam_topic)
            self.get_logger().info(
                f"slam_toolbox SerializePoseGraph client → '{self._slam_topic}'")
        else:
            self.get_logger().warn(
                "slam_toolbox_msgs not available — map serialisation disabled.")

        # ── manual export service ─────────────────────────────────────────────
        self._export_srv = self.create_service(
            Trigger, "~/export", self._export_callback)
        self.get_logger().info("Export service advertised at '~/export'.")

        # ── optional timer ────────────────────────────────────────────────────
        if interval > 0.0:
            self._timer = self.create_timer(interval, self._timer_callback)
            self.get_logger().info(
                f"Auto-export enabled every {interval:.1f} seconds.")

        self.get_logger().info(
            f"SlamToolboxBridge ready.  db='{self._db_path}' out='{self._output_dir}'")

    # ─────────────────────────────────────────────────────────────────────────
    # Callbacks
    # ─────────────────────────────────────────────────────────────────────────

    def _export_callback(self,
                          request: Trigger.Request,   # noqa: ARG002
                          response: Trigger.Response) -> Trigger.Response:
        success, message = self.run_export()
        response.success = success
        response.message = message
        return response

    def _timer_callback(self) -> None:
        self.run_export()

    # ─────────────────────────────────────────────────────────────────────────
    # Core export logic
    # ─────────────────────────────────────────────────────────────────────────

    def run_export(self) -> tuple[bool, str]:
        """
        Execute the full export pipeline.

        Returns (success: bool, message: str).
        """
        self.get_logger().info("Starting map export cycle …")

        pgm_path  = self._output_dir / f"{self._map_name}.pgm"
        yaml_path = self._output_dir / f"{self._map_name}.yaml"
        json_path = self._output_dir / f"{self._map_name}_semantic.json"

        # ── Step 1: serialise SLAM pose graph ─────────────────────────────────
        slam_ok = self._call_slam_serialize(str(self._output_dir / self._map_name))
        if not slam_ok:
            self.get_logger().warn(
                "SLAM serialisation failed or unavailable — "
                "continuing with semantic-only export.")

        # ── Step 2: export semantic objects to JSON ───────────────────────────
        n_objects = self._export_semantic_json(str(json_path))
        self.get_logger().info(f"Exported {n_objects} semantic objects → {json_path}")

        # ── Step 3: patch the YAML sidecar ────────────────────────────────────
        if yaml_path.exists():
            self._patch_yaml(str(yaml_path), str(json_path), n_objects)
            self.get_logger().info(f"Patched YAML → {yaml_path}")
        else:
            # Create a minimal YAML if slam_toolbox did not produce one
            self._create_minimal_yaml(str(yaml_path), str(pgm_path), str(json_path))
            self.get_logger().info(f"Created minimal YAML → {yaml_path}")

        msg = (f"Export complete: {n_objects} semantic objects, "
               f"pgm={'yes' if pgm_path.exists() else 'no'}, "
               f"yaml={'yes' if yaml_path.exists() else 'no'}, "
               f"json=yes")
        self.get_logger().info(msg)
        return True, msg

    # ─────────────────────────────────────────────────────────────────────────
    # SLAM Toolbox: SerializePoseGraph
    # ─────────────────────────────────────────────────────────────────────────

    def _call_slam_serialize(self, output_prefix: str) -> bool:
        """
        Send a SerializePoseGraph request to slam_toolbox.

        Returns True on success, False on failure / timeout.
        """
        if self._slam_client is None:
            return False

        if not self._slam_client.wait_for_service(timeout_sec=2.0):
            self.get_logger().warn(
                f"SerializePoseGraph service '{self._slam_topic}' not available.")
            return False

        request = SerializePoseGraph.Request()
        request.filename = output_prefix

        future = self._slam_client.call_async(request)
        rclpy.spin_until_future_complete(self, future, timeout_sec=10.0)

        if future.result() is not None:
            self.get_logger().info(
                f"SerializePoseGraph succeeded → '{output_prefix}.*'")
            return True

        self.get_logger().error("SerializePoseGraph call timed out or failed.")
        return False

    # ─────────────────────────────────────────────────────────────────────────
    # Semantic export
    # ─────────────────────────────────────────────────────────────────────────

    def _export_semantic_json(self, json_path: str) -> int:
        """
        Read all semantic objects from the SQLite store and write them to JSON.

        Returns the number of exported objects.
        """
        if not Path(self._db_path).exists():
            self.get_logger().warn(
                f"Database '{self._db_path}' does not exist — writing empty JSON.")
            Path(json_path).write_text("[]", encoding="utf-8")
            return 0

        with MapMetadataStore(self._db_path) as store:
            n = store.export_to_json(json_path)
        return n

    # ─────────────────────────────────────────────────────────────────────────
    # YAML patching
    # ─────────────────────────────────────────────────────────────────────────

    def _patch_yaml(self, yaml_path: str, json_path: str, n_objects: int) -> None:
        """
        Append a 'semantic_metadata' section to an existing map YAML file.

        We avoid a heavy YAML library dependency by appending a formatted block.
        """
        semantic_block = self._build_semantic_yaml_block(json_path, n_objects)

        with open(yaml_path, "r", encoding="utf-8") as fh:
            original = fh.read()

        # Remove any previously patched semantic block to avoid duplication
        split_marker = "\n# --- semantic_metadata (auto-generated) ---\n"
        if split_marker in original:
            original = original.split(split_marker)[0]

        with open(yaml_path, "w", encoding="utf-8") as fh:
            fh.write(original)
            fh.write(split_marker)
            fh.write(semantic_block)

    def _create_minimal_yaml(self, yaml_path: str,
                              pgm_path: str, json_path: str) -> None:
        """Create a minimal ROS 2 map YAML if slam_toolbox did not produce one."""
        content = (
            f"image: {Path(pgm_path).name}\n"
            f"resolution: 0.05\n"
            f"origin: [0.0, 0.0, 0.0]\n"
            f"occupied_thresh: 0.65\n"
            f"free_thresh: 0.196\n"
            f"negate: 0\n"
        )
        with open(yaml_path, "w", encoding="utf-8") as fh:
            fh.write(content)

        n = self._count_json_objects(json_path)
        self._patch_yaml(yaml_path, json_path, n)

    @staticmethod
    def _build_semantic_yaml_block(json_path: str, n_objects: int) -> str:
        """Return a YAML-formatted block describing the semantic export."""
        timestamp = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        return (
            f"semantic_metadata:\n"
            f"  json_file: {Path(json_path).name}\n"
            f"  object_count: {n_objects}\n"
            f"  export_timestamp: \"{timestamp}\"\n"
        )

    @staticmethod
    def _count_json_objects(json_path: str) -> int:
        try:
            with open(json_path, "r", encoding="utf-8") as fh:
                data = json.load(fh)
            return len(data) if isinstance(data, list) else 0
        except Exception:
            return 0


# ─────────────────────────────────────────────────────────────────────────────
# Entrypoint
# ─────────────────────────────────────────────────────────────────────────────

def main(args=None) -> None:
    rclpy.init(args=args)
    node = SlamToolboxBridge()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
