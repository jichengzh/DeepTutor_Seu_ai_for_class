#!/usr/bin/env bash
# run_sim.sh — 启动 Gazebo 仿真快捷入口
set -euo pipefail

source /opt/ros/humble/setup.bash 2>/dev/null || true
source "${HOME}/ros2_ws/install/setup.bash" 2>/dev/null || true

echo "[run_sim] Launching Gazebo simulation environment..."
ros2 launch module_5_multi_robot multi_robot_sim.launch.py \
  robot_count:="${ROBOT_COUNT:-3}" \
  use_sim_time:=true \
  world:="${WORLD:-warehouse}"
