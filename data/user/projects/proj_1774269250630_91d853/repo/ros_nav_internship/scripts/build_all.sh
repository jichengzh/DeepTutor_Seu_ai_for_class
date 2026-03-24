#!/usr/bin/env bash
# build_all.sh — 全量构建所有 ROS2 模块
set -euo pipefail

WS_DIR="${HOME}/ros2_ws"
source /opt/ros/humble/setup.bash 2>/dev/null || true

cd "${WS_DIR}"
colcon build \
  --symlink-install \
  --cmake-args -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  --packages-select \
    module_1_localization \
    module_2_planning \
    module_3_mapping \
    module_4_obstacle \
    module_5_multi_robot \
    module_6_testing \
  --event-handlers console_direct+

echo "[build_all] Build complete. Source: source ${WS_DIR}/install/setup.bash"
