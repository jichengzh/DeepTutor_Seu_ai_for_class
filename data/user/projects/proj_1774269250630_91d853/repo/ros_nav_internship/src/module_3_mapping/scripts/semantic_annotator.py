#!/usr/bin/env python3
"""
semantic_annotator.py — YOLOv8 ROS2 adapter for semantic annotation.

Subscribes to camera images, runs YOLOv8 inference, back-projects
detected bounding boxes to world coordinates using depth + TF,
and publishes semantic annotations as MarkerArray.

Topics (configurable via parameters)
─────────────────────────────────────
  Subscriptions:
    /camera/image_raw         (sensor_msgs/Image)
    /camera/depth/image_raw   (sensor_msgs/Image)  — 32FC1 or 16UC1
    /camera/camera_info       (sensor_msgs/CameraInfo)
  Publications:
    /semantic_annotations     (visualization_msgs/MarkerArray)

Parameters
──────────
  model_path           (str)   path to YOLOv8 .pt model file
  confidence_threshold (float) minimum detection confidence [0..1]
  classes_filter       (list)  YOLO class IDs to keep (empty = all)
  camera_topic         (str)   RGB image topic
  depth_topic          (str)   depth image topic
  camera_info_topic    (str)   camera info topic
  world_frame          (str)   TF target frame (default: "map")
  annotation_topic     (str)   output MarkerArray topic
  marker_lifetime_sec  (float) lifetime of each published marker
"""

import rclpy
from rclpy.node import Node
from rclpy.duration import Duration

from sensor_msgs.msg import Image, CameraInfo
from visualization_msgs.msg import MarkerArray, Marker
from geometry_msgs.msg import Point

import numpy as np
import cv2
from cv_bridge import CvBridge

import tf2_ros
import tf2_geometry_msgs  # noqa: F401  (registers TF2 conversions)
from geometry_msgs.msg import PointStamped

# Use try/except for ultralytics import for CI environments
try:
    from ultralytics import YOLO
    YOLO_AVAILABLE = True
except ImportError:
    YOLO_AVAILABLE = False


class SemanticAnnotator(Node):
    """
    YOLOv8-based semantic annotator.

    Processes RGB+depth image pairs to produce 3D semantic annotations
    published as visualization_msgs/MarkerArray messages.
    """

    def __init__(self) -> None:
        super().__init__("semantic_annotator")

        # ── parameter declarations ────────────────────────────────────────────
        self.declare_parameter("model_path",           "yolov8n.pt")
        self.declare_parameter("confidence_threshold",  0.5)
        self.declare_parameter("classes_filter",        [])          # empty = all
        self.declare_parameter("camera_topic",         "/camera/image_raw")
        self.declare_parameter("depth_topic",          "/camera/depth/image_raw")
        self.declare_parameter("camera_info_topic",    "/camera/camera_info")
        self.declare_parameter("world_frame",          "map")
        self.declare_parameter("annotation_topic",     "/semantic_annotations")
        self.declare_parameter("marker_lifetime_sec",   5.0)

        # ── retrieve parameters ───────────────────────────────────────────────
        self._model_path   = self.get_parameter("model_path").value
        self._conf_thr     = float(self.get_parameter("confidence_threshold").value)
        self._cls_filter   = list(self.get_parameter("classes_filter").value)
        self._world_frame  = self.get_parameter("world_frame").value
        self._marker_life  = float(self.get_parameter("marker_lifetime_sec").value)

        cam_topic   = self.get_parameter("camera_topic").value
        depth_topic = self.get_parameter("depth_topic").value
        info_topic  = self.get_parameter("camera_info_topic").value
        ann_topic   = self.get_parameter("annotation_topic").value

        # ── internal state ────────────────────────────────────────────────────
        self._bridge         = CvBridge()
        self._camera_info    = None       # type: CameraInfo | None
        self._latest_depth   = None       # type: np.ndarray | None
        self._marker_counter = 0          # global marker ID counter

        # ── YOLOv8 model ─────────────────────────────────────────────────────
        self._model = None
        if YOLO_AVAILABLE:
            try:
                self._model = YOLO(self._model_path)
                self.get_logger().info(
                    f"YOLOv8 model loaded from '{self._model_path}'")
            except Exception as exc:
                self.get_logger().error(
                    f"Failed to load YOLOv8 model: {exc}")
        else:
            self.get_logger().warn(
                "ultralytics not installed — running in passthrough (no detections) mode.")

        # ── TF2 ──────────────────────────────────────────────────────────────
        self._tf_buffer   = tf2_ros.Buffer()
        self._tf_listener = tf2_ros.TransformListener(self._tf_buffer, self)

        # ── subscriptions ─────────────────────────────────────────────────────
        self._camera_info_sub = self.create_subscription(
            CameraInfo, info_topic, self._camera_info_callback, 10)

        self._depth_sub = self.create_subscription(
            Image, depth_topic, self._depth_callback, 10)

        self._image_sub = self.create_subscription(
            Image, cam_topic, self.image_callback, 10)

        # ── publisher ─────────────────────────────────────────────────────────
        self._annotation_pub = self.create_publisher(MarkerArray, ann_topic, 10)

        self.get_logger().info(
            f"SemanticAnnotator ready. conf={self._conf_thr} world_frame='{self._world_frame}'")

    # ─────────────────────────────────────────────────────────────────────────
    # Callbacks
    # ─────────────────────────────────────────────────────────────────────────

    def _camera_info_callback(self, msg: CameraInfo) -> None:
        """Cache the first received CameraInfo message."""
        if self._camera_info is None:
            self._camera_info = msg
            self.get_logger().info(
                f"CameraInfo cached: {msg.width}x{msg.height} "
                f"fx={msg.k[0]:.2f} fy={msg.k[4]:.2f}")

    def _depth_callback(self, msg: Image) -> None:
        """Convert incoming depth image to a float32 numpy array (metres)."""
        try:
            if msg.encoding == "16UC1":
                raw = self._bridge.imgmsg_to_cv2(msg, desired_encoding="16UC1")
                self._latest_depth = raw.astype(np.float32) / 1000.0  # mm → m
            else:
                self._latest_depth = self._bridge.imgmsg_to_cv2(
                    msg, desired_encoding="32FC1")
        except Exception as exc:
            self.get_logger().warn(f"Depth conversion failed: {exc}")

    def image_callback(self, msg: Image) -> None:
        """
        Main processing callback.

        1. Convert ROS Image → OpenCV BGR.
        2. Run YOLO inference.
        3. For each detection above threshold, compute 3D world position.
        4. Publish MarkerArray.
        """
        if self._camera_info is None:
            self.get_logger().debug("No CameraInfo yet — skipping frame.")
            return

        # Convert image
        try:
            bgr = self._bridge.imgmsg_to_cv2(msg, desired_encoding="bgr8")
        except Exception as exc:
            self.get_logger().warn(f"Image conversion failed: {exc}")
            return

        detections = self._run_inference(bgr)
        annotations = []

        for det in detections:
            cls_id  = int(det["class_id"])
            label   = det["class_name"]
            conf    = float(det["confidence"])
            x1, y1  = int(det["x_min"]), int(det["y_min"])
            x2, y2  = int(det["x_max"]), int(det["y_max"])

            # Filter by class if specified
            if self._cls_filter and cls_id not in self._cls_filter:
                continue

            # Bounding-box centre (pixel)
            u = (x1 + x2) / 2.0
            v = (y1 + y2) / 2.0

            # Sample depth at bounding-box centre
            depth_m = self._sample_depth(u, v)
            if depth_m <= 0.0 or not np.isfinite(depth_m):
                self.get_logger().debug(
                    f"Invalid depth {depth_m:.3f}m at ({u:.0f},{v:.0f}) for '{label}' — skipping.")
                continue

            # Back-project to world frame
            world_pt = self.backproject_to_world(
                u, v, depth_m, self._camera_info,
                msg.header.frame_id, self._world_frame)

            if world_pt is None:
                continue

            annotations.append({
                "label":    label,
                "class_id": cls_id,
                "confidence": conf,
                "world_position": world_pt,
                "frame_id": self._world_frame,
            })

        if annotations:
            self.publish_annotations(annotations, msg.header.stamp)

    # ─────────────────────────────────────────────────────────────────────────
    # Inference
    # ─────────────────────────────────────────────────────────────────────────

    def _run_inference(self, bgr: np.ndarray) -> list:
        """
        Run YOLO inference on a BGR image.

        Returns a list of dicts with keys:
          class_id, class_name, confidence, x_min, y_min, x_max, y_max
        """
        if self._model is None:
            return []

        results = self._model.predict(
            source=bgr,
            conf=self._conf_thr,
            verbose=False,
        )

        detections = []
        for result in results:
            if result.boxes is None:
                continue
            boxes  = result.boxes.xyxy.cpu().numpy()   # [N, 4]
            confs  = result.boxes.conf.cpu().numpy()   # [N]
            cls_ids = result.boxes.cls.cpu().numpy().astype(int)  # [N]
            names  = result.names                       # dict id→name

            for box, conf, cls_id in zip(boxes, confs, cls_ids):
                x1, y1, x2, y2 = box
                detections.append({
                    "class_id":   cls_id,
                    "class_name": names.get(cls_id, str(cls_id)),
                    "confidence": float(conf),
                    "x_min": float(x1),
                    "y_min": float(y1),
                    "x_max": float(x2),
                    "y_max": float(y2),
                })

        return detections

    # ─────────────────────────────────────────────────────────────────────────
    # Depth sampling
    # ─────────────────────────────────────────────────────────────────────────

    def _sample_depth(self, u: float, v: float) -> float:
        """
        Return depth at pixel (u, v) in metres.

        Uses a small 5x5 median window to reduce noise.
        Returns 0.0 if no valid depth image is available.
        """
        if self._latest_depth is None:
            return 0.0

        h, w = self._latest_depth.shape[:2]
        half = 2
        r0, r1 = max(0, int(v) - half), min(h, int(v) + half + 1)
        c0, c1 = max(0, int(u) - half), min(w, int(u) + half + 1)

        patch = self._latest_depth[r0:r1, c0:c1]
        valid = patch[np.isfinite(patch) & (patch > 0.0)]
        if valid.size == 0:
            return 0.0
        return float(np.median(valid))

    # ─────────────────────────────────────────────────────────────────────────
    # Back-projection
    # ─────────────────────────────────────────────────────────────────────────

    def backproject_to_world(
        self,
        u: float,
        v: float,
        depth_m: float,
        camera_info: CameraInfo,
        source_frame: str,
        target_frame: str,
    ):
        """
        Convert pixel (u, v) + depth_m to a 3D point in *target_frame*.

        Uses the pinhole camera model:
          X_cam = (u - cx) * depth / fx
          Y_cam = (v - cy) * depth / fy
          Z_cam = depth

        Then transforms the camera-frame point to *target_frame* via TF2.

        Returns a geometry_msgs/Point on success, or None on TF failure.
        """
        fx = camera_info.k[0]
        fy = camera_info.k[4]
        cx = camera_info.k[2]
        cy = camera_info.k[5]

        if fx == 0.0 or fy == 0.0:
            self.get_logger().warn("CameraInfo has zero focal length.")
            return None

        # Camera-frame 3D point
        x_cam = (u - cx) * depth_m / fx
        y_cam = (v - cy) * depth_m / fy
        z_cam = depth_m

        # Wrap in PointStamped for TF2
        pt_cam = PointStamped()
        pt_cam.header.frame_id = source_frame
        pt_cam.header.stamp    = self.get_clock().now().to_msg()
        pt_cam.point.x = x_cam
        pt_cam.point.y = y_cam
        pt_cam.point.z = z_cam

        try:
            pt_world = self._tf_buffer.transform(
                pt_cam, target_frame,
                timeout=rclpy.duration.Duration(seconds=0.1))
            return pt_world.point
        except (tf2_ros.LookupException,
                tf2_ros.ConnectivityException,
                tf2_ros.ExtrapolationException) as exc:
            self.get_logger().debug(
                f"TF transform {source_frame} → {target_frame} failed: {exc}")
            return None

    # ─────────────────────────────────────────────────────────────────────────
    # Publish annotations
    # ─────────────────────────────────────────────────────────────────────────

    def publish_annotations(self, annotations: list, stamp) -> None:
        """
        Build and publish a MarkerArray from a list of annotation dicts.

        Each annotation produces two markers:
          1. SPHERE  — visual indicator at the 3D position
          2. TEXT_VIEW_FACING — label above the sphere
        """
        array = MarkerArray()
        lifetime = Duration(seconds=int(self._marker_life),
                            nanoseconds=int((self._marker_life % 1) * 1e9))

        for ann in annotations:
            pos: Point = ann["world_position"]
            label: str = ann["label"]
            conf: float = ann["confidence"]
            base_id = self._marker_counter
            self._marker_counter += 2

            # ── sphere ───────────────────────────────────────────────────────
            sphere = Marker()
            sphere.header.frame_id = ann["frame_id"]
            sphere.header.stamp    = stamp
            sphere.ns              = "semantic_objects"
            sphere.id              = base_id
            sphere.type            = Marker.SPHERE
            sphere.action          = Marker.ADD
            sphere.pose.position   = pos
            sphere.pose.orientation.w = 1.0
            sphere.scale.x = sphere.scale.y = sphere.scale.z = 0.4
            # Colour: green tint, alpha proportional to confidence
            sphere.color.r = 0.1
            sphere.color.g = 0.8
            sphere.color.b = 0.2
            sphere.color.a = float(max(0.3, min(1.0, conf)))
            sphere.lifetime = lifetime
            array.markers.append(sphere)

            # ── text label ───────────────────────────────────────────────────
            text = Marker()
            text.header.frame_id = ann["frame_id"]
            text.header.stamp    = stamp
            text.ns              = "semantic_labels"
            text.id              = base_id + 1
            text.type            = Marker.TEXT_VIEW_FACING
            text.action          = Marker.ADD
            text.pose.position.x = pos.x
            text.pose.position.y = pos.y
            text.pose.position.z = pos.z + 0.35
            text.pose.orientation.w = 1.0
            text.scale.z    = 0.18
            text.color.r    = 1.0
            text.color.g    = 1.0
            text.color.b    = 1.0
            text.color.a    = 1.0
            text.text       = f"{label} ({conf:.0%})"
            text.lifetime   = lifetime
            array.markers.append(text)

        self._annotation_pub.publish(array)
        self.get_logger().debug(
            f"Published {len(annotations)} semantic annotation(s).")


# ─────────────────────────────────────────────────────────────────────────────
# Entrypoint
# ─────────────────────────────────────────────────────────────────────────────

def main(args=None) -> None:
    rclpy.init(args=args)
    node = SemanticAnnotator()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
