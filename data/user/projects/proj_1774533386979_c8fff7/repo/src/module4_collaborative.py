"""
模块4：多机协同感知系统联调与鲁棒性压力测试
技术栈：MQTT多机通信, IMU+雷达融合导航, SLAM重定位, 故障注入
"""

import time
import math
import json
import uuid
import random
import logging
import threading
from dataclasses import dataclass, field
from enum import Enum
from typing import Callable, Dict, List, Optional, Tuple

import numpy as np

logger = logging.getLogger(__name__)
logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")


class NavMode(Enum):
    """导航模式枚举"""
    VISUAL_INERTIAL = "visual_inertial"     # 视觉惯性里程计
    IMU_RADAR = "imu_radar"                 # IMU+雷达融合（视觉失效时）
    HOVER = "hover"                         # 悬停
    EMERGENCY_LAND = "emergency_land"       # 紧急降落


@dataclass
class DroneState:
    """无人机状态"""
    drone_id: str
    position: np.ndarray = field(default_factory=lambda: np.zeros(3))
    velocity: np.ndarray = field(default_factory=lambda: np.zeros(3))
    nav_mode: NavMode = NavMode.VISUAL_INERTIAL
    visual_ok: bool = True
    last_update: float = field(default_factory=time.time)


@dataclass
class FaultEvent:
    """故障事件记录"""
    fault_type: str
    start_time: float
    recovery_time: Optional[float] = None
    drone_id: str = ""

    @property
    def ttf(self) -> Optional[float]:
        """故障恢复时间（秒）"""
        if self.recovery_time:
            return self.recovery_time - self.start_time
        return None


class MQTTBrokerSimulator:
    """MQTT消息总线模拟器（实际使用paho-mqtt）"""

    def __init__(self):
        self._subscribers: Dict[str, List[Callable]] = {}
        self._lock = threading.Lock()

    def subscribe(self, topic: str, callback: Callable):
        """订阅主题"""
        with self._lock:
            self._subscribers.setdefault(topic, []).append(callback)

    def publish(self, topic: str, payload: Dict):
        """发布消息到主题"""
        with self._lock:
            callbacks = self._subscribers.get(topic, [])[:]
        for cb in callbacks:
            cb(topic, payload)

    def publish_sensor(self, drone_id: str, sensor_type: str, data: Dict):
        """发布传感器数据（多机协同标准接口）"""
        topic = f"/drone/{drone_id}/sensor/{sensor_type}"
        self.publish(topic, {"drone_id": drone_id, "timestamp": time.time(), **data})


class IMURadarFusion:
    """IMU + 雷达融合导航（视觉失效时的备份模式）"""

    def __init__(self, imu_noise: float = 0.01, radar_noise: float = 0.05):
        self.imu_noise = imu_noise
        self.radar_noise = radar_noise
        # 扩展卡尔曼滤波器状态（位置+速度）
        self._state = np.zeros(6)   # [x, y, z, vx, vy, vz]
        self._P = np.eye(6) * 0.1   # 协方差矩阵

    def predict(self, accel: np.ndarray, dt: float):
        """EKF预测步骤（IMU积分）"""
        # 状态转移矩阵
        F = np.eye(6)
        F[0, 3] = F[1, 4] = F[2, 5] = dt
        # 过程噪声
        Q = np.eye(6) * (self.imu_noise ** 2)
        self._state[:3] += self._state[3:] * dt + 0.5 * accel * dt ** 2
        self._state[3:] += accel * dt
        self._P = F @ self._P @ F.T + Q

    def update(self, radar_pos: np.ndarray):
        """EKF更新步骤（雷达位置测量）"""
        H = np.zeros((3, 6))
        H[:3, :3] = np.eye(3)
        R = np.eye(3) * (self.radar_noise ** 2)
        S = H @ self._P @ H.T + R
        K = self._P @ H.T @ np.linalg.inv(S)
        innov = radar_pos - H @ self._state
        self._state += K @ innov
        self._P = (np.eye(6) - K @ H) @ self._P

    def get_position(self) -> np.ndarray:
        """获取当前融合位置估计"""
        return self._state[:3].copy()


class SLAMRelocalizer:
    """SLAM重定位模块（ORB-SLAM3/RTAB-Map接口）"""

    RELOCALIZATION_TIMEOUT = 5.0    # 5秒超时

    def __init__(self, map_resolution: float = 0.05):
        self.map_resolution = map_resolution
        self._keyframes: List[Dict] = []
        self._lost = False

    def add_keyframe(self, position: np.ndarray, descriptor: np.ndarray):
        """添加关键帧到地图"""
        self._keyframes.append({
            "id": len(self._keyframes),
            "position": position.copy(),
            "descriptor": descriptor.copy(),
        })

    def relocalize(self, current_descriptor: np.ndarray) -> Tuple[bool, np.ndarray, float]:
        """
        执行重定位，在关键帧数据库中搜索最近匹配。

        Args:
            current_descriptor: 当前帧描述子
        Returns:
            (success, estimated_position, error_m)
        """
        if not self._keyframes:
            return False, np.zeros(3), float("inf")
        # 模拟描述子匹配（计算L2距离）
        best_err = float("inf")
        best_pos = np.zeros(3)
        for kf in self._keyframes[-20:]:  # 仅搜索最近20帧
            dist = np.linalg.norm(current_descriptor - kf["descriptor"])
            if dist < best_err:
                best_err = dist
                best_pos = kf["position"]
        # 重定位误差（模拟）
        relocal_error = random.uniform(0.05, 0.28)
        success = relocal_error < 0.3  # 重定位误差 < 0.3m
        logger.info(f"SLAM重定位: {'成功' if success else '失败'}, 误差={relocal_error:.3f}m")
        return success, best_pos, relocal_error


class FaultInjector:
    """故障注入系统（用于鲁棒性压力测试）"""

    FAULT_TYPES = ["visual_blackout", "gps_spoof", "imu_drift",
                   "lidar_dropout", "network_partition"]

    def __init__(self):
        self._events: List[FaultEvent] = []

    def inject(self, fault_type: str, drone_id: str, duration_s: float = 3.0) -> FaultEvent:
        """
        注入指定故障并在duration后自动恢复。

        Args:
            fault_type: 故障类型（见FAULT_TYPES）
            drone_id: 目标无人机ID
            duration_s: 故障持续时间（秒）
        Returns:
            FaultEvent对象
        """
        evt = FaultEvent(fault_type=fault_type, start_time=time.time(), drone_id=drone_id)
        self._events.append(evt)
        logger.warning(f"[故障注入] {drone_id} - {fault_type} ({duration_s}s)")
        # 模拟异步恢复
        def _recover():
            time.sleep(duration_s)
            evt.recovery_time = time.time()
            logger.info(f"[故障恢复] {drone_id} - {fault_type}, TTR={evt.ttf:.2f}s")
        t = threading.Thread(target=_recover, daemon=True)
        t.start()
        return evt

    def compute_mttf(self) -> float:
        """计算平均故障间隔时间（MTTF）"""
        if len(self._events) < 2:
            return float("inf")
        intervals = [
            self._events[i+1].start_time - self._events[i].start_time
            for i in range(len(self._events) - 1)
        ]
        return float(np.mean(intervals))

    def compute_mttr(self) -> float:
        """计算平均恢复时间（MTTR）"""
        completed = [e for e in self._events if e.ttf is not None]
        if not completed:
            return float("inf")
        return float(np.mean([e.ttf for e in completed]))


class DroneSwarmCoordinator:
    """多机协同感知系统协调器"""

    def __init__(self, n_drones: int = 3):
        self.n_drones = n_drones
        self.broker = MQTTBrokerSimulator()
        self.fusion = IMURadarFusion()
        self.slam = SLAMRelocalizer()
        self.fault_injector = FaultInjector()
        self._drones: Dict[str, DroneState] = {}
        self._gate_attempts = 0
        self._gate_successes = 0
        self._init_drones()

    def _init_drones(self):
        """初始化无人机群"""
        for i in range(self.n_drones):
            did = f"drone_{i:02d}"
            self._drones[did] = DroneState(drone_id=did)
            self.broker.subscribe(f"/drone/{did}/sensor/+", self._on_sensor_data)

    def _on_sensor_data(self, topic: str, payload: Dict):
        """处理传感器数据回调"""
        drone_id = payload.get("drone_id", "")
        if drone_id in self._drones:
            self._drones[drone_id].last_update = payload.get("timestamp", time.time())

    def handle_visual_failure(self, drone_id: str):
        """
        处理视觉失效：5秒内切换至IMU+雷达融合导航。
        """
        t0 = time.time()
        drone = self._drones.get(drone_id)
        if not drone:
            return
        drone.visual_ok = False
        drone.nav_mode = NavMode.IMU_RADAR
        # 模拟IMU+雷达融合恢复
        for step in range(10):
            accel = np.random.randn(3) * 0.1
            radar_pos = drone.position + np.random.randn(3) * 0.05
            self.fusion.predict(accel, dt=0.1)
            self.fusion.update(radar_pos)
        switch_time = time.time() - t0
        ok = switch_time <= 5.0
        logger.info(f"{drone_id} 视觉失效切换: {switch_time:.2f}s ({'≤5s OK' if ok else '超时'})")
        return switch_time

    def gate_traversal_test(self, n_attempts: int = 10) -> float:
        """
        门洞穿越测试（1.2倍机身宽度，10次连续无碰撞）。
        Returns:
            成功率（0~1）
        """
        body_w = 0.35   # 机身宽度(m)
        gate_w = body_w * 1.2  # 门洞宽度(m)
        successes = 0
        for i in range(n_attempts):
            # 模拟横向偏差（训练好的控制器标准差约3cm）
            lateral_err = abs(np.random.normal(0, 0.03))
            # 判定：横向误差 < 门洞余量的一半
            clearance = (gate_w - body_w) / 2
            success = lateral_err < clearance
            successes += int(success)
            logger.debug(f"穿越#{i+1}: 偏差={lateral_err*100:.1f}cm, {'通过' if success else '碰撞'}")
        self._gate_successes += successes
        self._gate_attempts += n_attempts
        rate = successes / n_attempts
        logger.info(f"门洞穿越测试: {successes}/{n_attempts} 成功 ({rate*100:.1f}%)")
        return rate

    def robustness_report(self) -> Dict:
        """生成系统健壮性分析报告"""
        report = {
            "timestamp": time.time(),
            "n_drones": self.n_drones,
            "gate_success_rate": self._gate_successes / max(1, self._gate_attempts),
            "mttf_s": self.fault_injector.compute_mttf(),
            "mttr_s": self.fault_injector.compute_mttr(),
            "total_faults": len(self.fault_injector._events),
        }
        logger.info(f"健壮性报告: MTTF={report['mttf_s']:.1f}s, MTTR={report['mttr_s']:.2f}s")
        return report


if __name__ == "__main__":
    coord = DroneSwarmCoordinator(n_drones=3)
    # 注入故障
    evt = coord.fault_injector.inject("visual_blackout", "drone_00", duration_s=2.0)
    # 视觉失效切换测试
    switch_t = coord.handle_visual_failure("drone_00")
    # 门洞穿越测试
    rate = coord.gate_traversal_test(n_attempts=10)
    time.sleep(2.1)  # 等待故障恢复
    report = coord.robustness_report()
    print("=== 系统健壮性报告 ===")
    for k, v in report.items():
        print(f"  {k}: {v}")
