# System Architecture Overview

## 1. Introduction

The ROS2 Autonomous Navigation system is a modular, layered software architecture designed for mobile robot autonomy in structured indoor environments (e.g., warehouses, logistics floors). It is built on ROS2 Humble and leverages the Nav2 navigation stack as its core middleware, extending it with semantic awareness, multi-robot fleet management, and a comprehensive automated test harness.

The architecture follows the **separation of concerns** principle: each module publishes and subscribes through well-defined ROS2 interfaces (topics, services, actions), enabling independent development, testing, and replacement of individual subsystems.

---

## 2. High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                        External World                               │
│   Sensors: LiDAR, IMU, Wheel Encoders, RGB-D Camera                │
│   Actuators: Differential Drive / Mecanum Wheels                    │
└───────────┬─────────────────────────────────────────┬───────────────┘
            │  Raw Sensor Data                        │  Velocity Commands
            ▼                                         ▼
┌───────────────────────┐                 ┌───────────────────────────┐
│  Module 1             │                 │  Module 2                 │
│  LOCALIZATION         │◄────────────────│  PATH PLANNING            │
│  EKF + AMCL           │  /amcl_pose     │  A* Global + TEB Local    │
│  /odometry/filtered   │                 │  /cmd_vel                 │
└───────────┬───────────┘                 └───────────┬───────────────┘
            │  /tf (map→odom→base_link)               │  /plan (global path)
            │                                         │  /local_plan
            ▼                                         ▼
┌───────────────────────┐                 ┌───────────────────────────┐
│  Module 3             │                 │  Module 4                 │
│  SEMANTIC MAPPING     │────────────────►│  OBSTACLE AVOIDANCE       │
│  SLAM Toolbox +       │  /map           │  Dynamic Detection +      │
│  YOLOv8 Annotator     │                 │  Costmap Updates          │
│  /semantic_map        │                 │  /dynamic_obstacles       │
└───────────────────────┘                 └───────────────────────────┘
            │                                         │
            └─────────────────┬───────────────────────┘
                              ▼
            ┌─────────────────────────────┐
            │  Module 5                   │
            │  MULTI-ROBOT COORDINATION   │
            │  Fleet Manager              │
            │  Task Allocator             │
            │  Conflict Resolver          │
            └─────────────────────────────┘
                              │
                              ▼
            ┌─────────────────────────────┐
            │  Module 6                   │
            │  TESTING FRAMEWORK          │
            │  Unit + Integration Tests   │
            │  Simulation Regression      │
            └─────────────────────────────┘
```

---

## 3. Module-by-Module Architecture

### 3.1 Module 1 — Localization

**Purpose:** Produce a continuous, low-latency, fused pose estimate for the robot.

**Internal Architecture:**

```
/imu/data ──────────────────┐
                             ▼
/wheel/odometry ──────► [EKF Node] ──► /odometry/filtered
                             │
                        /tf odom→base_link

/map ───────────────────────┐
/scan ──────────────────────┤
/odometry/filtered ─────────┴──► [AMCL Node] ──► /amcl_pose
                                                  /particle_cloud
                                                  /tf map→odom
```

**Key Design Decisions:**
- EKF runs at 50 Hz, AMCL at 10 Hz. The EKF provides smooth continuous pose; AMCL corrects long-term drift.
- Covariance matrices are tuned for indoor differential-drive robots on flat floors. IMU yaw is treated as a measurement input rather than a control input to reduce heading error accumulation.
- `robot_localization` package is used for the EKF, with a custom wrapper node that adds diagnostic publishing and watchdog monitoring.

---

### 3.2 Module 2 — Path Planning

**Purpose:** Compute feasible, collision-free paths from the robot's current pose to a goal pose, and control the robot to follow them smoothly.

**Internal Architecture:**

```
/navigate_to_pose (action)
        │
        ▼
[BT Navigator]
        │
        ├──► [Planner Server] ──► [A* Plugin]
        │         │                    │
        │         │◄── /map            └──► /plan
        │
        └──► [Controller Server] ──► [TEB Plugin]
                  │                       │
                  │◄── /odometry/filtered  └──► /cmd_vel
                  │◄── /costmap/local
```

**Key Design Decisions:**
- The global planner uses a modified A* with a weighted Euclidean heuristic (weight=1.2) for slightly suboptimal but faster planning on large maps.
- TEB (Timed Elastic Band) is chosen as the local controller because it natively handles non-holonomic constraints and produces smooth, time-parameterised trajectories that respect acceleration limits.
- The behaviour tree (BT) orchestrator allows recovery behaviours (spin, back-up, wait) to be composed declaratively without modifying planner code.

---

### 3.3 Module 3 — Semantic Mapping

**Purpose:** Build a 2D occupancy grid map of the environment and annotate it with semantic object labels detected by a vision model.

**Internal Architecture:**

```
/scan ──────────────────────────────► [SLAM Toolbox Node]
/odometry/filtered ─────────────────►      │
                                           ├──► /map (OccupancyGrid)
                                           └──► /map_metadata

/camera/image_raw ──────────────────► [Semantic Annotator Node]
                                           │
                                           ├──► YOLOv8 Inference
                                           │    (torch, ultralytics)
                                           │
                                           ├──► 2D→3D Projection
                                           │    (depth + camera_info)
                                           │
                                           └──► /semantic_map (custom msg)
                                                /semantic_markers (MarkerArray)
```

**Key Design Decisions:**
- SLAM Toolbox is used in **online asynchronous** mode, which decouples map update frequency from scan rate, preventing navigation latency spikes during loop closures.
- YOLOv8n (nano) is used for inference to meet real-time constraints on CPU. GPU acceleration is used when available (CUDA device auto-detected at startup).
- Semantic detections are projected into the map frame using the depth image and camera intrinsics. Each landmark is stored with its map coordinates, class label, and detection confidence.
- The semantic map is persisted to an SQLite database via SQLAlchemy so it survives node restarts.

---

### 3.4 Module 4 — Obstacle Avoidance

**Purpose:** Detect static and dynamic obstacles not captured in the static map, update the Nav2 costmap in real time, and trigger replanning when the current path becomes infeasible.

**Internal Architecture:**

```
/scan ──────────────────────────────► [Obstacle Detector Node]
/camera/depth/image_raw ────────────►      │
                                           ├──► Segmentation
                                           │    (static vs. dynamic)
                                           │
                                           ├──► Dynamic Tracking
                                           │    (Kalman Filter per object)
                                           │
                                           └──► /dynamic_obstacles (MarkerArray)
                                                /tracked_obstacles (custom msg)

/dynamic_obstacles ─────────────────► [Costmap Updater Node]
                                           │
                                           └──► /costmap/obstacle_layer update
                                                /costmap_updates (OccupancyGrid)
```

**Key Design Decisions:**
- Static obstacles are added to the global costmap via the standard Nav2 obstacle layer. Dynamic obstacles are handled separately in the local costmap to avoid polluting the global plan.
- Each detected dynamic object is assigned a Kalman filter tracking its position and velocity. Objects unseen for more than 2 seconds are removed from the costmap.
- The costmap updater inflates each dynamic obstacle by a margin that scales with the object's tracked velocity (faster objects get larger inflation radii).

---

### 3.5 Module 5 — Multi-Robot Coordination

**Purpose:** Manage a fleet of N robots sharing a common environment, allocating tasks efficiently and preventing inter-robot collisions and deadlocks.

**Internal Architecture:**

```
[Fleet Manager Node] (lifecycle)
        │
        ├──► Spawns N robot instances (Gazebo + Nav2 per robot)
        │
        ├──► [Task Allocator]
        │         │
        │         │  Auction-Based Assignment
        │         │  (Hungarian algorithm for batch tasks)
        │         │
        │         └──► /fleet/task_assignments
        │
        └──► [Conflict Resolver]
                  │
                  │  Velocity Obstacle Method
                  │  (reads /robot_N/odom for all N robots)
                  │
                  └──► /robot_N/cmd_vel_adjusted (velocity corrections)

Per-Robot Stack (namespaced under /robot_N/):
  /robot_N/scan
  /robot_N/odom
  /robot_N/cmd_vel
  /robot_N/navigate_to_pose (action server)
  /robot_N/map         (shared, read-only)
```

**Key Design Decisions:**
- Each robot runs its own independent Nav2 stack in a dedicated ROS2 namespace (`/robot_0/`, `/robot_1/`, etc.), sharing only the static map.
- Task allocation uses a **sequential single-item auction**: tasks are auctioned one at a time in order of urgency, and each robot bids based on its estimated travel time. This is O(N*T) and scales well for fleets up to ~20 robots.
- Conflict resolution uses the **velocity obstacle (VO)** method: when two robots' predicted trajectories intersect within the lookahead horizon, the lower-priority robot reduces speed or yields. Priority is determined by task deadline.
- The fleet manager monitors each robot's last heartbeat timestamp. If a robot misses 3 consecutive heartbeats (3 seconds), it is marked as failed and its task is reallocated.

---

### 3.6 Module 6 — Testing Framework

**Purpose:** Provide automated regression testing for all five navigation modules, covering unit behaviour, inter-module integration, and full simulation-based end-to-end scenarios.

**Internal Architecture:**

```
pytest
  │
  ├── conftest.py  (shared fixtures)
  │       │
  │       ├── launch_ros_nodes()   — spins up required ROS2 nodes per test
  │       ├── gazebo_world()       — launches Gazebo with test world
  │       └── robot_state()        — provides robot pose/status helpers
  │
  ├── test_localization.py
  │       └── test_ekf_convergence, test_amcl_recovery, ...
  │
  ├── test_planning.py
  │       └── test_path_found, test_goal_reached, test_replanning, ...
  │
  ├── test_mapping.py
  │       └── test_map_completeness, test_semantic_label_accuracy, ...
  │
  ├── test_obstacle.py
  │       └── test_obstacle_detection_recall, test_costmap_inflation, ...
  │
  └── test_multi_robot.py
          └── test_no_collision_3robots, test_task_completion_rate, ...
```

**Key Design Decisions:**
- Tests use `pytest-ros` fixtures to manage ROS2 node lifecycle, avoiding the need for manual `rclpy.init()`/`shutdown()` calls in each test.
- Simulation tests use a dedicated lightweight Gazebo world (`test_world.world`) with known obstacle positions to make assertions deterministic.
- Test results are exported as JUnit XML for CI/CD integration (GitHub Actions, Jenkins).

---

## 4. Cross-Cutting Concerns

### 4.1 Coordinate Frames (TF Tree)

```
map
 └── odom          (published by: EKF node / AMCL)
      └── base_link  (published by: robot URDF / Gazebo)
           ├── laser_link   (LiDAR)
           ├── camera_link  (RGB-D Camera)
           └── imu_link     (IMU)
```

### 4.2 Time Synchronisation

All modules use `use_sim_time:=true` in simulation. The Gazebo clock is the authoritative time source, published on `/clock`. All nodes subscribe to `/clock` and use `rclpy.time.Time` rather than wall clock.

### 4.3 Parameter Management

All tunable parameters are externalised to YAML files in each module's `config/` directory and loaded at launch time. No hardcoded parameters exist in node source code. Parameters are declared with explicit types and default values using `declare_parameter()`.

### 4.4 Logging

All nodes use the ROS2 `get_logger()` interface with structured log levels (DEBUG, INFO, WARN, ERROR). Log output is directed to both the console and ROS2 bag files for post-run analysis.

### 4.5 Quality of Service (QoS)

| Topic Type | QoS Profile |
|------------|-------------|
| Sensor data (`/scan`, `/imu`) | Best Effort, volatile |
| Navigation output (`/cmd_vel`) | Reliable, volatile |
| Map (`/map`) | Reliable, transient local (latched) |
| TF | Best Effort, volatile |
| Diagnostics | Reliable, keep-last 10 |

---

## 5. Deployment Environments

| Environment | Configuration |
|-------------|---------------|
| Local development | Ubuntu 22.04, ROS2 Humble installed natively |
| Docker simulation | `docker-compose up humble_sim` |
| CI testing | `docker-compose up test_runner` |
| Physical robot | Same packages, `use_sim_time:=false`, real sensor drivers |

---

## 6. Performance Characteristics

| Metric | Target | Typical Achieved |
|--------|--------|-----------------|
| Localisation update rate | 50 Hz | 48–52 Hz |
| AMCL update rate | 10 Hz | 9–11 Hz |
| Global planning latency | < 500 ms | 80–300 ms |
| Local control loop rate | 20 Hz | 19–21 Hz |
| Map update rate (SLAM) | 5 Hz | 4–6 Hz |
| Obstacle detection latency | < 100 ms | 40–80 ms |
| Fleet task allocation (10 robots) | < 2 s | 0.3–0.8 s |
