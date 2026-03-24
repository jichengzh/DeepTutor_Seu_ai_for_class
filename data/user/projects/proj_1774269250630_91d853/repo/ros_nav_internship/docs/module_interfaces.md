# Module Interfaces Reference

This document specifies all ROS2 topics, services, and actions published, subscribed, or provided by each module. It serves as the authoritative interface contract between modules.

**Conventions:**
- `→` means the module **publishes** this interface
- `←` means the module **subscribes to** this interface
- `[S]` = Service, `[A]` = Action, `[T]` = Topic

---

## Table of Contents

1. [Module 1 — Localization](#module-1--localization)
2. [Module 2 — Path Planning](#module-2--path-planning)
3. [Module 3 — Semantic Mapping](#module-3--semantic-mapping)
4. [Module 4 — Obstacle Avoidance](#module-4--obstacle-avoidance)
5. [Module 5 — Multi-Robot Coordination](#module-5--multi-robot-coordination)
6. [Module 6 — Testing](#module-6--testing)
7. [Cross-Module Interface Matrix](#cross-module-interface-matrix)
8. [Custom Message Definitions](#custom-message-definitions)

---

## Module 1 — Localization

### Node: `ekf_node`

| Direction | Type | Name | Message Type | Rate | Description |
|-----------|------|------|-------------|------|-------------|
| ← | [T] | `/imu/data` | `sensor_msgs/Imu` | 100 Hz | Raw IMU (accelerometer + gyroscope) |
| ← | [T] | `/wheel/odometry` | `nav_msgs/Odometry` | 50 Hz | Raw wheel encoder odometry |
| → | [T] | `/odometry/filtered` | `nav_msgs/Odometry` | 50 Hz | EKF-fused odometry output |
| → | [T] | `/tf` | `tf2_msgs/TFMessage` | 50 Hz | odom → base_link transform |
| → | [T] | `/diagnostics` | `diagnostic_msgs/DiagnosticArray` | 1 Hz | EKF health diagnostics |

### Node: `amcl`

| Direction | Type | Name | Message Type | Rate | Description |
|-----------|------|------|-------------|------|-------------|
| ← | [T] | `/map` | `nav_msgs/OccupancyGrid` | latched | Static map from Module 3 |
| ← | [T] | `/scan` | `sensor_msgs/LaserScan` | 10 Hz | LiDAR scan for particle weight update |
| ← | [T] | `/odometry/filtered` | `nav_msgs/Odometry` | 50 Hz | Fused odom for particle propagation |
| ← | [T] | `/initialpose` | `geometry_msgs/PoseWithCovarianceStamped` | on-demand | Manual initial pose override |
| → | [T] | `/amcl_pose` | `geometry_msgs/PoseWithCovarianceStamped` | 10 Hz | Global pose estimate with covariance |
| → | [T] | `/particle_cloud` | `nav2_msgs/ParticleCloud` | 2 Hz | Current particle set (for RViz visualisation) |
| → | [T] | `/tf` | `tf2_msgs/TFMessage` | 10 Hz | map → odom transform |
| → | [S] | `/amcl/get_state` | `lifecycle_msgs/GetState` | on-demand | Lifecycle state query |
| ← | [S] | `/amcl/change_state` | `lifecycle_msgs/ChangeState` | on-demand | Lifecycle state transition |

---

## Module 2 — Path Planning

### Node: `planner_server`

| Direction | Type | Name | Message Type | Rate | Description |
|-----------|------|------|-------------|------|-------------|
| ← | [T] | `/map` | `nav_msgs/OccupancyGrid` | latched | Static map for global planning |
| ← | [T] | `/costmap/global_costmap/costmap` | `nav_msgs/OccupancyGrid` | 1 Hz | Inflated global costmap |
| → | [T] | `/plan` | `nav_msgs/Path` | on-demand | Computed global path |
| → | [A] | `/compute_path_to_pose` | `nav2_msgs/action/ComputePathToPose` | on-demand | Global path planning action |
| → | [A] | `/compute_path_through_poses` | `nav2_msgs/action/ComputePathThroughPoses` | on-demand | Multi-waypoint global planning |
| → | [S] | `/planner_server/get_state` | `lifecycle_msgs/GetState` | on-demand | Lifecycle state query |

### Node: `controller_server`

| Direction | Type | Name | Message Type | Rate | Description |
|-----------|------|------|-------------|------|-------------|
| ← | [T] | `/odometry/filtered` | `nav_msgs/Odometry` | 50 Hz | Robot pose and velocity |
| ← | [T] | `/costmap/local_costmap/costmap` | `nav_msgs/OccupancyGrid` | 5 Hz | Local costmap with dynamic obstacles |
| ← | [T] | `/plan` | `nav_msgs/Path` | on-demand | Global path to follow |
| → | [T] | `/cmd_vel` | `geometry_msgs/Twist` | 20 Hz | Velocity command to robot |
| → | [T] | `/local_plan` | `nav_msgs/Path` | 10 Hz | Local trajectory currently being tracked |
| → | [A] | `/follow_path` | `nav2_msgs/action/FollowPath` | on-demand | Path following action |

### Node: `bt_navigator`

| Direction | Type | Name | Message Type | Rate | Description |
|-----------|------|------|-------------|------|-------------|
| ← | [T] | `/amcl_pose` | `geometry_msgs/PoseWithCovarianceStamped` | 10 Hz | Current robot pose |
| → | [A] | `/navigate_to_pose` | `nav2_msgs/action/NavigateToPose` | on-demand | High-level navigation action |
| → | [A] | `/navigate_through_poses` | `nav2_msgs/action/NavigateThroughPoses` | on-demand | Multi-goal navigation action |
| → | [T] | `/navigation_result` | `std_msgs/String` | on-demand | Human-readable result message |

### Node: `costmap_node` (global and local instances)

| Direction | Type | Name | Message Type | Rate | Description |
|-----------|------|------|-------------|------|-------------|
| ← | [T] | `/scan` | `sensor_msgs/LaserScan` | 10 Hz | Sensor input for obstacle layer |
| ← | [T] | `/costmap_updates` | `map_msgs/OccupancyGridUpdate` | 5 Hz | Dynamic updates from Module 4 |
| → | [T] | `/costmap/global_costmap/costmap` | `nav_msgs/OccupancyGrid` | 1 Hz | Global costmap |
| → | [T] | `/costmap/local_costmap/costmap` | `nav_msgs/OccupancyGrid` | 5 Hz | Local costmap |
| → | [S] | `/clear_costmaps` | `nav2_msgs/srv/ClearCostmapExceptRegion` | on-demand | Clear stale obstacle data |

---

## Module 3 — Semantic Mapping

### Node: `slam_toolbox_node`

| Direction | Type | Name | Message Type | Rate | Description |
|-----------|------|------|-------------|------|-------------|
| ← | [T] | `/scan` | `sensor_msgs/LaserScan` | 10 Hz | LiDAR scan for SLAM |
| ← | [T] | `/odometry/filtered` | `nav_msgs/Odometry` | 50 Hz | Odometry for scan matching |
| ← | [T] | `/tf` | `tf2_msgs/TFMessage` | — | Transform tree lookup |
| → | [T] | `/map` | `nav_msgs/OccupancyGrid` | 1 Hz | Built occupancy grid map |
| → | [T] | `/map_metadata` | `nav_msgs/MapMetaData` | 1 Hz | Map dimensions and resolution |
| → | [T] | `/tf` | `tf2_msgs/TFMessage` | 1 Hz | map → odom transform |
| → | [S] | `/slam_toolbox/serialize_map` | `slam_toolbox/srv/SerializePoseGraph` | on-demand | Save pose graph to file |
| → | [S] | `/slam_toolbox/deserialize_map` | `slam_toolbox/srv/DeserializePoseGraph` | on-demand | Load pose graph from file |
| → | [S] | `/slam_toolbox/save_map` | `nav2_msgs/srv/SaveMap` | on-demand | Save map as PGM/YAML |

### Node: `semantic_annotator_node`

| Direction | Type | Name | Message Type | Rate | Description |
|-----------|------|------|-------------|------|-------------|
| ← | [T] | `/camera/image_raw` | `sensor_msgs/Image` | 10 Hz | RGB camera frames |
| ← | [T] | `/camera/depth/image_raw` | `sensor_msgs/Image` | 10 Hz | Depth frames for 3D projection |
| ← | [T] | `/camera/camera_info` | `sensor_msgs/CameraInfo` | 10 Hz | Camera intrinsics |
| ← | [T] | `/tf` | `tf2_msgs/TFMessage` | — | Transform: camera_link → map |
| → | [T] | `/semantic_map` | `ros_nav_msgs/SemanticMap` | 1 Hz | Map annotated with object landmarks |
| → | [T] | `/semantic_markers` | `visualization_msgs/MarkerArray` | 1 Hz | RViz markers for detected objects |
| → | [T] | `/detection_image` | `sensor_msgs/Image` | 5 Hz | Camera image with bounding box overlays |
| → | [S] | `/semantic_annotator/get_landmarks` | `ros_nav_msgs/srv/GetLandmarks` | on-demand | Query all detected landmarks |

---

## Module 4 — Obstacle Avoidance

### Node: `obstacle_detector_node`

| Direction | Type | Name | Message Type | Rate | Description |
|-----------|------|------|-------------|------|-------------|
| ← | [T] | `/scan` | `sensor_msgs/LaserScan` | 10 Hz | LiDAR for obstacle detection |
| ← | [T] | `/camera/depth/image_raw` | `sensor_msgs/Image` | 10 Hz | Depth for close-range detection |
| ← | [T] | `/odometry/filtered` | `nav_msgs/Odometry` | 50 Hz | Robot motion for dynamic classification |
| ← | [T] | `/map` | `nav_msgs/OccupancyGrid` | latched | Static map to classify static vs. dynamic |
| → | [T] | `/dynamic_obstacles` | `visualization_msgs/MarkerArray` | 10 Hz | Dynamic obstacle bounding boxes (RViz) |
| → | [T] | `/tracked_obstacles` | `ros_nav_msgs/TrackedObstacleArray` | 10 Hz | Tracked obstacle positions and velocities |
| → | [S] | `/obstacle_detector/get_obstacles` | `ros_nav_msgs/srv/GetObstacles` | on-demand | Query current obstacle list |

### Node: `costmap_updater_node`

| Direction | Type | Name | Message Type | Rate | Description |
|-----------|------|------|-------------|------|-------------|
| ← | [T] | `/tracked_obstacles` | `ros_nav_msgs/TrackedObstacleArray` | 10 Hz | Obstacle positions from detector |
| → | [T] | `/costmap_updates` | `map_msgs/OccupancyGridUpdate` | 5 Hz | Incremental costmap patches |
| → | [S] | `/costmap_updater/clear_dynamic` | `std_srvs/Trigger` | on-demand | Clear all dynamic obstacle cells |

---

## Module 5 — Multi-Robot Coordination

Topics in this module are **namespaced per robot** using the prefix `/robot_N/` where N is the robot index (0-based).

### Node: `fleet_manager_node`

| Direction | Type | Name | Message Type | Rate | Description |
|-----------|------|------|-------------|------|-------------|
| ← | [T] | `/robot_N/odom` | `nav_msgs/Odometry` | 20 Hz | Per-robot odometry (N robots) |
| ← | [T] | `/robot_N/status` | `ros_nav_msgs/RobotStatus` | 1 Hz | Per-robot health heartbeat |
| → | [T] | `/fleet/robot_states` | `ros_nav_msgs/FleetState` | 1 Hz | Aggregated fleet state |
| → | [T] | `/fleet/task_assignments` | `ros_nav_msgs/TaskAssignmentArray` | on-demand | Task-to-robot assignments |
| → | [S] | `/fleet/add_task` | `ros_nav_msgs/srv/AddTask` | on-demand | Add a new task to the queue |
| → | [S] | `/fleet/cancel_task` | `ros_nav_msgs/srv/CancelTask` | on-demand | Cancel a queued or active task |
| → | [S] | `/fleet/get_fleet_state` | `ros_nav_msgs/srv/GetFleetState` | on-demand | Query full fleet state |
| → | [A] | `/fleet/execute_mission` | `ros_nav_msgs/action/ExecuteMission` | on-demand | Execute a multi-robot mission |

### Node: `task_allocator_node`

| Direction | Type | Name | Message Type | Rate | Description |
|-----------|------|------|-------------|------|-------------|
| ← | [T] | `/fleet/robot_states` | `ros_nav_msgs/FleetState` | 1 Hz | Fleet state for bid computation |
| ← | [T] | `/fleet/task_queue` | `ros_nav_msgs/TaskArray` | on-demand | Incoming task list |
| → | [T] | `/fleet/task_assignments` | `ros_nav_msgs/TaskAssignmentArray` | on-demand | Computed assignments |

### Node: `conflict_resolver_node`

| Direction | Type | Name | Message Type | Rate | Description |
|-----------|------|------|-------------|------|-------------|
| ← | [T] | `/robot_N/odom` | `nav_msgs/Odometry` | 20 Hz | Per-robot odometry for path prediction |
| ← | [T] | `/robot_N/local_plan` | `nav_msgs/Path` | 10 Hz | Per-robot planned local trajectory |
| → | [T] | `/robot_N/cmd_vel_adjusted` | `geometry_msgs/Twist` | 20 Hz | Velocity-adjusted commands to prevent collision |
| → | [T] | `/fleet/conflicts` | `ros_nav_msgs/ConflictArray` | on-demand | Detected and resolved conflict events |

### Per-Robot Navigation Stack (namespaced)

Each robot `/robot_N/` exposes the full Nav2 action interface:

| Direction | Type | Name | Message Type | Description |
|-----------|------|------|-------------|-------------|
| ← | [A] | `/robot_N/navigate_to_pose` | `nav2_msgs/action/NavigateToPose` | Send navigation goal to robot N |
| ← | [A] | `/robot_N/navigate_through_poses` | `nav2_msgs/action/NavigateThroughPoses` | Multi-waypoint goal for robot N |
| → | [T] | `/robot_N/cmd_vel` | `geometry_msgs/Twist` | Velocity commands from Nav2 |
| → | [T] | `/robot_N/amcl_pose` | `geometry_msgs/PoseWithCovarianceStamped` | Per-robot pose estimate |

---

## Module 6 — Testing

The testing module does not publish or subscribe to topics during normal operation. It uses the following interfaces **during test execution**:

### Topics consumed in tests (read-only assertions)

| Topic | Type | Used in test |
|-------|------|-------------|
| `/amcl_pose` | `geometry_msgs/PoseWithCovarianceStamped` | `test_localization.py` |
| `/odometry/filtered` | `nav_msgs/Odometry` | `test_localization.py` |
| `/plan` | `nav_msgs/Path` | `test_planning.py` |
| `/cmd_vel` | `geometry_msgs/Twist` | `test_planning.py`, `test_obstacle.py` |
| `/map` | `nav_msgs/OccupancyGrid` | `test_mapping.py` |
| `/semantic_markers` | `visualization_msgs/MarkerArray` | `test_mapping.py` |
| `/dynamic_obstacles` | `visualization_msgs/MarkerArray` | `test_obstacle.py` |
| `/fleet/task_assignments` | `ros_nav_msgs/TaskAssignmentArray` | `test_multi_robot.py` |
| `/robot_N/odom` | `nav_msgs/Odometry` | `test_multi_robot.py` |

### Actions called in tests

| Action | Type | Used in test |
|--------|------|-------------|
| `/navigate_to_pose` | `nav2_msgs/action/NavigateToPose` | `test_planning.py` |
| `/fleet/execute_mission` | `ros_nav_msgs/action/ExecuteMission` | `test_multi_robot.py` |

### Services called in tests

| Service | Type | Used in test |
|---------|------|-------------|
| `/clear_costmaps` | `nav2_msgs/srv/ClearCostmapExceptRegion` | `test_planning.py` |
| `/slam_toolbox/save_map` | `nav2_msgs/srv/SaveMap` | `test_mapping.py` |
| `/semantic_annotator/get_landmarks` | `ros_nav_msgs/srv/GetLandmarks` | `test_mapping.py` |
| `/fleet/add_task` | `ros_nav_msgs/srv/AddTask` | `test_multi_robot.py` |

---

## Cross-Module Interface Matrix

The table below shows which module **produces** (rows) and which module **consumes** (columns) each major interface.

| Interface | M1 Loc | M2 Plan | M3 Map | M4 Obs | M5 Fleet | M6 Test |
|-----------|--------|---------|--------|--------|---------|---------|
| `/odometry/filtered` | **pub** | sub | sub | sub | sub | sub |
| `/amcl_pose` | **pub** | sub | — | — | sub | sub |
| `/tf` (map→odom) | **pub** | sub | sub | sub | sub | — |
| `/map` | sub | sub | **pub** | sub | sub | sub |
| `/plan` | — | **pub** | — | — | — | sub |
| `/cmd_vel` | — | **pub** | — | — | sub | sub |
| `/local_plan` | — | **pub** | — | — | sub | — |
| `/semantic_map` | — | — | **pub** | — | — | sub |
| `/costmap_updates` | — | sub | — | **pub** | — | — |
| `/dynamic_obstacles` | — | — | — | **pub** | — | sub |
| `/tracked_obstacles` | — | — | — | **pub** | sub | — |
| `/fleet/task_assignments` | — | — | — | — | **pub** | sub |
| `/fleet/conflicts` | — | — | — | — | **pub** | sub |

---

## Custom Message Definitions

The package `ros_nav_msgs` defines the following custom message types used in the interfaces above.

### `ros_nav_msgs/SemanticLandmark.msg`

```
string      label           # Object class name (e.g., "door", "shelf")
float32     confidence      # Detection confidence [0.0, 1.0]
geometry_msgs/Point position  # Position in map frame
builtin_interfaces/Time first_seen
builtin_interfaces/Time last_seen
uint32      detection_count
```

### `ros_nav_msgs/SemanticMap.msg`

```
std_msgs/Header             header
nav_msgs/OccupancyGrid      grid
SemanticLandmark[]          landmarks
```

### `ros_nav_msgs/TrackedObstacle.msg`

```
uint32              id
string              type          # "static" | "dynamic"
geometry_msgs/Pose  pose
geometry_msgs/Twist velocity
float32             radius
float32             confidence
builtin_interfaces/Time last_seen
```

### `ros_nav_msgs/TrackedObstacleArray.msg`

```
std_msgs/Header         header
TrackedObstacle[]       obstacles
```

### `ros_nav_msgs/RobotStatus.msg`

```
uint32      robot_id
string      state         # "idle" | "navigating" | "charging" | "fault"
float32     battery_level # [0.0, 1.0]
string      current_task_id
geometry_msgs/PoseStamped pose
builtin_interfaces/Time timestamp
```

### `ros_nav_msgs/FleetState.msg`

```
std_msgs/Header     header
RobotStatus[]       robots
uint32              active_task_count
uint32              pending_task_count
```

### `ros_nav_msgs/Task.msg`

```
string              task_id
string              type          # "navigate" | "pick" | "place" | "patrol"
geometry_msgs/Pose  target_pose
int32               priority      # Higher = more urgent
float32             deadline_sec  # Seconds from now, -1 = no deadline
string              assigned_robot_id  # Empty if unassigned
```

### `ros_nav_msgs/TaskAssignmentArray.msg`

```
std_msgs/Header     header
Task[]              assignments
```

### `ros_nav_msgs/ConflictArray.msg`

```
std_msgs/Header     header
uint32[]            robot_id_a
uint32[]            robot_id_b
geometry_msgs/Point[] conflict_point
float32[]           time_to_conflict
string[]            resolution_applied
```

### `ros_nav_msgs/srv/AddTask.srv`

```
Task task
---
bool    success
string  message
string  task_id
```

### `ros_nav_msgs/srv/CancelTask.srv`

```
string  task_id
---
bool    success
string  message
```

### `ros_nav_msgs/srv/GetFleetState.srv`

```
---
FleetState state
```

### `ros_nav_msgs/srv/GetLandmarks.srv`

```
string  label_filter   # Optional: filter by class name. Empty = return all.
---
SemanticLandmark[]  landmarks
```

### `ros_nav_msgs/srv/GetObstacles.srv`

```
string  type_filter    # "static" | "dynamic" | "" (all)
---
TrackedObstacleArray obstacles
```

### `ros_nav_msgs/action/ExecuteMission.action`

```
# Goal
Task[]      tasks
bool        return_to_base
---
# Result
bool        success
uint32      tasks_completed
uint32      tasks_failed
float32     total_duration_sec
---
# Feedback
uint32      tasks_completed
uint32      tasks_remaining
RobotStatus[] robot_states
```
