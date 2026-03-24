# ROS2 Autonomous Navigation Internship Project

A modular ROS2-based autonomous navigation framework for mobile robots, developed as part of a robotics engineering internship. The system implements the full navigation stack — from sensor fusion and localization through global/local planning, semantic mapping, obstacle avoidance, multi-robot coordination, and comprehensive automated testing.

---

## Table of Contents

1. [Project Overview](#project-overview)
2. [Environment Requirements](#environment-requirements)
3. [Installation](#installation)
4. [Running the Modules](#running-the-modules)
5. [Project Structure](#project-structure)
6. [Module Descriptions](#module-descriptions)
7. [Configuration](#configuration)
8. [Troubleshooting](#troubleshooting)

---

## Project Overview

This project provides a complete, simulation-ready autonomous navigation pipeline built on ROS2 Humble. It is structured into six independent but interoperable modules:

| # | Module | Responsibility |
|---|--------|----------------|
| 1 | `module_1_localization` | EKF sensor fusion, AMCL pose estimation |
| 2 | `module_2_planning` | A* global planner, DWA/TEB local planner |
| 3 | `module_3_mapping` | SLAM Toolbox, semantic map annotation via YOLOv8 |
| 4 | `module_4_obstacle` | Dynamic obstacle detection and avoidance |
| 5 | `module_5_multi_robot` | Fleet coordination, task allocation, conflict resolution |
| 6 | `module_6_testing` | Automated regression and integration test suite |

The system is designed for Gazebo simulation on Ubuntu 22.04 with ROS2 Humble, and can be deployed on physical TurtleBot4 or custom differential-drive platforms.

---

## Environment Requirements

| Component | Version |
|-----------|---------|
| OS | Ubuntu 22.04 LTS (Jammy Jellyfish) |
| ROS2 | Humble Hawksbill (LTS) |
| Python | 3.10.x |
| CMake | >= 3.22 |
| CUDA (optional) | >= 11.7 (for GPU-accelerated YOLO inference) |
| Gazebo | Ignition Fortress / Classic 11 |
| RAM | >= 8 GB (16 GB recommended for multi-robot sim) |
| Disk | >= 20 GB free |

---

## Installation

### Step 1 — Clone the repository

```bash
git clone https://github.com/your-org/ros_nav_internship.git
cd ros_nav_internship
```

### Step 2 — Run the automated environment setup script

This script installs all system-level ROS2 packages, configures APT sources, initialises `rosdep`, and creates a symlinked ROS2 workspace.

```bash
chmod +x scripts/setup_env.sh
./scripts/setup_env.sh
```

Expected output (abbreviated):

```
[setup_env] Configuring ROS2 humble development environment...
...
[setup_env] Done. Run: source /opt/ros/humble/setup.bash
```

### Step 3 — Source the ROS2 environment

```bash
source /opt/ros/humble/setup.bash
```

Add this to your shell profile to make it permanent:

```bash
echo "source /opt/ros/humble/setup.bash" >> ~/.bashrc
source ~/.bashrc
```

### Step 4 — Install Python dependencies

```bash
pip3 install -r requirements.txt
```

### Step 5 — Build all ROS2 packages

```bash
chmod +x scripts/build_all.sh
./scripts/build_all.sh
```

Expected output:

```
Starting >>> module_1_localization
Starting >>> module_2_planning
...
Finished <<< module_6_testing [12.3s]

Summary: 6 packages finished [1min 47s]
[build_all] Build complete. Source: source ~/ros2_ws/install/setup.bash
```

### Step 6 — Source the built workspace

```bash
source ~/ros2_ws/install/setup.bash
```

### Step 7 (Optional) — Docker-based setup

If you prefer a containerised setup:

```bash
cd docker/
docker compose build
docker compose up humble_sim
```

---

## Running the Modules

### Module 1 — Localization

Launch the EKF-based localization node with AMCL:

```bash
ros2 launch module_1_localization localization.launch.py use_sim_time:=true
```

Expected output:

```
[ekf_node]: Configuring...
[ekf_node]: Subscribing to topic /imu/data
[ekf_node]: Subscribing to topic /wheel/odometry
[ekf_node]: Publishing to topic /odometry/filtered
[amcl]: Received a map — 4096 free cells, 1024 occupied cells
[amcl]: Initial pose set: x=0.00, y=0.00, theta=0.00
```

### Module 2 — Path Planning

Launch the global and local planners via Nav2:

```bash
ros2 launch module_2_planning navigation.launch.py use_sim_time:=true map:=/path/to/map.yaml
```

Send a navigation goal:

```bash
ros2 action send_goal /navigate_to_pose nav2_msgs/action/NavigateToPose \
  "{pose: {header: {frame_id: 'map'}, pose: {position: {x: 3.0, y: 1.5, z: 0.0}, orientation: {w: 1.0}}}}"
```

Expected output:

```
[planner_server]: Got new goal: x=3.00, y=1.50
[planner_server]: Planning path with A* planner...
[planner_server]: Path found, 47 waypoints, length=3.82m
[controller_server]: Following path with TEB local planner
[controller_server]: Goal reached!
```

### Module 3 — Semantic Mapping

Run SLAM with concurrent YOLOv8 object annotation:

```bash
ros2 launch module_3_mapping slam_mapping.launch.py use_sim_time:=true
```

Save the completed map:

```bash
ros2 run nav2_map_server map_saver_cli -f ~/maps/warehouse_map
```

Expected output:

```
[slam_toolbox]: Pose Graph built with 312 nodes
[semantic_annotator]: Detected 'door' at map coords (2.1, 4.3) — confidence 0.94
[semantic_annotator]: Detected 'shelf' at map coords (5.6, 1.2) — confidence 0.88
[map_saver]: Map saved to ~/maps/warehouse_map.pgm / .yaml
```

### Module 4 — Obstacle Avoidance

Run the dynamic obstacle detection and avoidance subsystem:

```bash
ros2 launch module_4_obstacle obstacle_avoidance.launch.py use_sim_time:=true
```

Expected output:

```
[obstacle_detector]: LiDAR scan subscribed on /scan
[obstacle_detector]: Dynamic obstacle detected at range=1.23m, bearing=15.0deg
[costmap_updater]: Inflation layer updated — 8 new lethal cells added
[local_planner]: Replanning around obstacle...
```

### Module 5 — Multi-Robot Simulation

Launch the full multi-robot fleet (default: 3 robots):

```bash
chmod +x scripts/run_sim.sh
ROBOT_COUNT=3 ./scripts/run_sim.sh
```

Or manually:

```bash
ros2 launch module_5_multi_robot multi_robot_sim.launch.py robot_count:=3 use_sim_time:=true world:=warehouse
```

Expected output:

```
[spawn_robot_0]: Robot 'bot_0' spawned at (0.0, 0.0)
[spawn_robot_1]: Robot 'bot_1' spawned at (2.0, 0.0)
[spawn_robot_2]: Robot 'bot_2' spawned at (4.0, 0.0)
[fleet_manager]: Task allocation: bot_0 -> zone_A, bot_1 -> zone_B, bot_2 -> zone_C
[conflict_resolver]: No path conflicts detected
```

### Module 6 — Automated Testing

Run the full test suite:

```bash
ros2 run module_6_testing run_all_tests.py
# or via pytest directly:
pytest src/module_6_testing/tests/ -v --tb=short
```

Expected output:

```
========================= test session starts ==========================
collected 42 items

tests/test_localization.py::test_ekf_convergence PASSED        [  2%]
tests/test_localization.py::test_amcl_global_localisation PASSED [  4%]
tests/test_planning.py::test_astar_path_found PASSED           [  7%]
...
tests/test_multi_robot.py::test_no_collision_3robots PASSED    [ 97%]
tests/test_multi_robot.py::test_task_completion_rate PASSED    [100%]

==================== 42 passed in 183.47s (0:03:03) ====================
Results written to: results/test_report_20260324.xml
```

---

## Project Structure

```
ros_nav_internship/
├── .gitignore
├── README.md
├── PLAN.md
├── requirements.txt
│
├── scripts/
│   ├── setup_env.sh          # One-click environment setup
│   ├── build_all.sh          # Full workspace build
│   └── run_sim.sh            # Gazebo simulation launcher
│
├── docker/
│   ├── Dockerfile.humble     # Ubuntu 22.04 + ROS2 Humble image
│   ├── Dockerfile.noetic     # Ubuntu 20.04 + ROS1 Noetic compatibility layer
│   └── docker-compose.yml    # Multi-container orchestration
│
├── docs/
│   ├── architecture_overview.md
│   ├── module_interfaces.md
│   └── diagrams/
│       ├── system_architecture.drawio
│       └── data_flow.drawio
│
├── results/
│   └── .gitkeep              # Placeholder — test results written here
│
└── src/
    ├── module_1_localization/
    │   ├── package.xml
    │   ├── CMakeLists.txt
    │   ├── config/
    │   │   ├── ekf_params.yaml
    │   │   └── amcl_params.yaml
    │   ├── launch/
    │   │   └── localization.launch.py
    │   └── module_1_localization/
    │       ├── __init__.py
    │       └── ekf_node.py
    │
    ├── module_2_planning/
    │   ├── package.xml
    │   ├── CMakeLists.txt
    │   ├── config/
    │   │   ├── planner_params.yaml
    │   │   └── costmap_params.yaml
    │   ├── launch/
    │   │   └── navigation.launch.py
    │   └── module_2_planning/
    │       ├── __init__.py
    │       ├── astar_planner.py
    │       └── teb_controller.py
    │
    ├── module_3_mapping/
    │   ├── package.xml
    │   ├── CMakeLists.txt
    │   ├── config/
    │   │   └── slam_params.yaml
    │   ├── launch/
    │   │   └── slam_mapping.launch.py
    │   ├── models/            # YOLOv8 .pt files (gitignored)
    │   └── module_3_mapping/
    │       ├── __init__.py
    │       ├── slam_node.py
    │       └── semantic_annotator.py
    │
    ├── module_4_obstacle/
    │   ├── package.xml
    │   ├── CMakeLists.txt
    │   ├── config/
    │   │   └── obstacle_params.yaml
    │   ├── launch/
    │   │   └── obstacle_avoidance.launch.py
    │   └── module_4_obstacle/
    │       ├── __init__.py
    │       ├── obstacle_detector.py
    │       └── costmap_updater.py
    │
    ├── module_5_multi_robot/
    │   ├── package.xml
    │   ├── CMakeLists.txt
    │   ├── config/
    │   │   └── fleet_params.yaml
    │   ├── launch/
    │   │   └── multi_robot_sim.launch.py
    │   ├── worlds/
    │   │   └── warehouse.world
    │   └── module_5_multi_robot/
    │       ├── __init__.py
    │       ├── fleet_manager.py
    │       ├── task_allocator.py
    │       └── conflict_resolver.py
    │
    └── module_6_testing/
        ├── package.xml
        ├── CMakeLists.txt
        ├── scripts/
        │   └── run_all_tests.sh
        └── tests/
            ├── conftest.py
            ├── test_localization.py
            ├── test_planning.py
            ├── test_mapping.py
            ├── test_obstacle.py
            └── test_multi_robot.py
```

---

## Module Descriptions

### Module 1 — Localization (`module_1_localization`)

Provides accurate, fused pose estimation by combining wheel odometry and IMU data through an Extended Kalman Filter (EKF), followed by AMCL (Adaptive Monte Carlo Localization) for global positioning on a pre-built map.

Key components:
- `ekf_node.py` — wraps `robot_localization`'s EKF with custom covariance tuning
- `amcl_params.yaml` — pre-tuned particle filter parameters for indoor environments
- Publishes `/odometry/filtered` (fused odom) and `/amcl_pose`

### Module 2 — Path Planning (`module_2_planning`)

Implements a two-tier planning strategy: A* for global path computation on a 2D costmap, and TEB (Timed Elastic Band) for smooth, kinematically feasible local trajectory following.

Key components:
- `astar_planner.py` — custom Nav2 planner plugin with configurable heuristics
- `teb_controller.py` — Nav2 controller plugin wrapping the TEB local planner
- Supports dynamic costmap updates from Module 4

### Module 3 — Semantic Mapping (`module_3_mapping`)

Runs SLAM Toolbox for online simultaneous localisation and mapping, augmented with a YOLOv8-based semantic annotator that labels detected objects (doors, shelves, hazards) as named landmarks in the map.

Key components:
- `slam_node.py` — SLAM Toolbox lifecycle node wrapper with serialisation support
- `semantic_annotator.py` — subscribes to `/camera/image_raw`, runs YOLOv8 inference, projects detections into map frame
- Model weights stored in `models/` (excluded from version control via `.gitignore`)

### Module 4 — Obstacle Avoidance (`module_4_obstacle`)

Provides real-time detection of both static and dynamic obstacles from LiDAR and depth camera data, updating the Nav2 costmap inflation layers and triggering local replanning as needed.

Key components:
- `obstacle_detector.py` — segments point clouds, classifies static vs. dynamic objects, tracks moving obstacles with a Kalman filter
- `costmap_updater.py` — inflates obstacle footprints in the Nav2 costmap and publishes a `/dynamic_obstacles` marker array for RViz

### Module 5 — Multi-Robot Coordination (`module_5_multi_robot`)

Manages a fleet of N autonomous robots in a shared Gazebo environment. Implements centralised task allocation (auction-based) and decentralised conflict resolution (velocity obstacle method) to prevent deadlocks and collisions.

Key components:
- `fleet_manager.py` — lifecycle node that spawns robots, monitors their state, and reassigns tasks on failure
- `task_allocator.py` — auction-based task assignment optimising total travel distance
- `conflict_resolver.py` — detects predicted path intersections and applies velocity adjustments
- `multi_robot_sim.launch.py` — parameterised launch file supporting 1–10 robots

### Module 6 — Testing (`module_6_testing`)

A comprehensive automated test suite covering unit tests, integration tests, and simulation-based regression tests for all five navigation modules.

Key components:
- `conftest.py` — shared pytest fixtures including ROS2 node lifecycle management and Gazebo launch helpers
- `test_localization.py` — EKF convergence tests, AMCL global localisation accuracy checks
- `test_planning.py` — path validity, reachability, and timing benchmarks
- `test_mapping.py` — map consistency checks, semantic label accuracy evaluation
- `test_obstacle.py` — obstacle detection recall/precision tests
- `test_multi_robot.py` — fleet collision-freedom and task completion rate tests
- Results are written to `results/` as JUnit XML and JSON for CI integration

---

## Configuration

All tunable parameters live in each module's `config/` directory as YAML files. Key files:

| File | Purpose |
|------|---------|
| `module_1_localization/config/ekf_params.yaml` | EKF process/observation noise covariance matrices |
| `module_1_localization/config/amcl_params.yaml` | Particle count, beam model weights |
| `module_2_planning/config/planner_params.yaml` | A* weight, TEB max velocity, acceleration limits |
| `module_2_planning/config/costmap_params.yaml` | Inflation radius, obstacle layer configuration |
| `module_3_mapping/config/slam_params.yaml` | SLAM Toolbox mode (online/offline), resolution |
| `module_4_obstacle/config/obstacle_params.yaml` | Detection thresholds, tracking window size |
| `module_5_multi_robot/config/fleet_params.yaml` | Robot count, task auction timeout, conflict horizon |

---

## Troubleshooting

### `colcon build` fails with "package not found"

Ensure you have sourced the ROS2 base installation before building:

```bash
source /opt/ros/humble/setup.bash
```

### AMCL fails to localise / robot stuck spinning

The robot has likely lost its prior pose. Set an initial pose estimate in RViz2 using the "2D Pose Estimate" tool, or publish it programmatically:

```bash
ros2 topic pub /initialpose geometry_msgs/msg/PoseWithCovarianceStamped \
  "{header: {frame_id: 'map'}, pose: {pose: {position: {x: 0.0, y: 0.0}, orientation: {w: 1.0}}}}" --once
```

### YOLOv8 model not found (`module_3_mapping`)

Download the model weights manually and place them in `src/module_3_mapping/models/`:

```bash
cd src/module_3_mapping/models/
wget https://github.com/ultralytics/assets/releases/download/v0.0.0/yolov8n.pt
```

### Gazebo fails to launch (no display)

If running headless or in Docker:

```bash
export DISPLAY=:0
xhost +local:docker       # allow Docker X11 access
```

Or use the headless Gazebo server:

```bash
ros2 launch module_5_multi_robot multi_robot_sim.launch.py headless:=true
```

### Multi-robot deadlock detected

Increase the conflict resolution horizon in `fleet_params.yaml`:

```yaml
conflict_resolver:
  lookahead_horizon: 5.0      # seconds (default: 3.0)
  velocity_obstacle_margin: 0.3  # metres
```

Then rebuild and relaunch Module 5.

### Tests fail with "ROS2 node failed to start within timeout"

The test environment ROS2 daemon may be stale. Reset it:

```bash
ros2 daemon stop
ros2 daemon start
pytest src/module_6_testing/tests/ -v
```

### `ImportError: No module named 'ultralytics'`

Reinstall Python dependencies:

```bash
pip3 install -r requirements.txt
```

If using a virtual environment, ensure it is activated before installing and before running ROS2 nodes.

---

## License

This project is provided for educational purposes as part of a robotics internship programme. All code is original unless otherwise noted in individual file headers.
