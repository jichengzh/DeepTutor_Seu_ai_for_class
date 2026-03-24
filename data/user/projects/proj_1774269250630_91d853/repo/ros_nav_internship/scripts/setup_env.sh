#!/usr/bin/env bash
# setup_env.sh — 一键配置 ROS2 Humble 开发环境
set -euo pipefail

ROS_DISTRO=${ROS_DISTRO:-humble}
echo "[setup_env] Configuring ROS2 ${ROS_DISTRO} development environment..."

# 1. 添加 ROS2 APT 源
sudo apt-get update && sudo apt-get install -y curl gnupg lsb-release
sudo curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key \
  -o /usr/share/keyrings/ros-archive-keyring.gpg
echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] \
  http://packages.ros.org/ros2/ubuntu $(. /etc/os-release && echo $UBUNTU_CODENAME) main" \
  | sudo tee /etc/apt/sources.list.d/ros2.list > /dev/null

# 2. 安装 ROS2 基础包
sudo apt-get update && sudo apt-get install -y \
  ros-${ROS_DISTRO}-desktop \
  ros-${ROS_DISTRO}-nav2-bringup \
  ros-${ROS_DISTRO}-slam-toolbox \
  ros-${ROS_DISTRO}-robot-localization \
  ros-${ROS_DISTRO}-teb-local-planner \
  ros-${ROS_DISTRO}-pcl-ros \
  ros-${ROS_DISTRO}-gazebo-ros-pkgs \
  python3-colcon-common-extensions \
  python3-rosdep \
  python3-vcstool

# 3. 初始化 rosdep
sudo rosdep init 2>/dev/null || true
rosdep update

# 4. 安装 Python 依赖
pip3 install -r "$(dirname "$0")/../requirements.txt"

# 5. 初始化 ROS2 工作空间
WS_DIR="${HOME}/ros2_ws"
mkdir -p "${WS_DIR}/src"
if [ ! -L "${WS_DIR}/src/ros_nav_internship" ]; then
  ln -sf "$(realpath "$(dirname "$0")/..")/src" "${WS_DIR}/src/ros_nav_internship"
fi

echo "[setup_env] Done. Run: source /opt/ros/${ROS_DISTRO}/setup.bash"
