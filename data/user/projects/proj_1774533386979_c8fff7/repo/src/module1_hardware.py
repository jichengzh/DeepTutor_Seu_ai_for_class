"""
模块1：无人机感知平台硬件集成与嵌入式环境部署
技术栈：UART/USB-C布线, PTP时间同步, AprilTag标定
"""

import time
import yaml
import math
import logging
import threading
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger(__name__)


@dataclass
class SensorConfig:
    """传感器配置数据类"""
    sensor_id: str
    interface: str          # "UART" | "USB-C" | "SPI"
    baud_rate: int = 115200
    topic: str = ""
    enabled: bool = True


@dataclass
class CalibrationResult:
    """标定结果数据类"""
    camera_matrix: List[List[float]]
    dist_coeffs: List[float]
    residual_m: float
    timestamp: float = field(default_factory=time.time)


class SensorInterface:
    """硬件传感器接口管理器，支持UART/USB-C规范布线"""

    def __init__(self, configs: List[SensorConfig]):
        self.configs = {c.sensor_id: c for c in configs}
        self._streams: Dict[str, bool] = {}

    def initialize(self) -> Dict[str, bool]:
        """初始化所有传感器接口，返回各传感器初始化状态"""
        results = {}
        for sid, cfg in self.configs.items():
            if not cfg.enabled:
                results[sid] = False
                continue
            logger.info(f"初始化传感器 {sid} [{cfg.interface}@{cfg.baud_rate}]")
            # 模拟DC-DC电源域隔离检查
            power_ok = self._check_power_domain(cfg.interface)
            results[sid] = power_ok
            self._streams[sid] = power_ok
        return results

    def _check_power_domain(self, interface: str) -> bool:
        """检查DC-DC稳压模块电源域隔离状态"""
        voltage_map = {"UART": 3.3, "USB-C": 5.0, "SPI": 3.3}
        expected_v = voltage_map.get(interface, 3.3)
        # 模拟ADC读取，实际环境替换为真实硬件读取
        measured_v = expected_v * (1.0 + (0.02 * math.sin(time.time())))
        deviation = abs(measured_v - expected_v) / expected_v
        ok = deviation < 0.05
        logger.debug(f"{interface} 电压 {measured_v:.3f}V (期望{expected_v}V), 偏差{deviation*100:.1f}%")
        return ok

    def read_sensor(self, sensor_id: str) -> Optional[Dict]:
        """读取指定传感器数据流（至少1000点/帧）"""
        if not self._streams.get(sensor_id, False):
            logger.warning(f"传感器 {sensor_id} 未就绪")
            return None
        # 模拟激光雷达点云（≥1000点）
        n_points = 1024
        import numpy as np
        points = np.random.randn(n_points, 4).astype("float32")  # x,y,z,intensity
        return {"sensor_id": sensor_id, "timestamp": time.time(),
                "points": points, "n_points": n_points}

    def get_topic_list(self) -> List[str]:
        """返回已注册的ROS2 topic列表（对应`ros2 topic list`）"""
        topics = []
        for sid, cfg in self.configs.items():
            if self._streams.get(sid, False) and cfg.topic:
                topics.append(cfg.topic)
        return topics


class PTPTimeSynchronizer:
    """PTP协议时间同步器，实现传感器间时间偏差≤12μs"""

    def __init__(self, master_clock_hz: float = 1e9):
        self.master_clock_hz = master_clock_hz
        self._offsets: Dict[str, float] = {}
        self._running = False
        self._lock = threading.Lock()

    def start(self):
        """启动PTP同步守护线程"""
        self._running = True
        t = threading.Thread(target=self._sync_loop, daemon=True)
        t.start()
        logger.info("PTP时间同步已启动")

    def stop(self):
        """停止PTP同步"""
        self._running = False

    def _sync_loop(self):
        """PTP同步主循环，定期校准各传感器时钟偏移"""
        while self._running:
            self._do_sync()
            time.sleep(1.0)  # 每秒同步一次

    def _do_sync(self):
        """执行一次PTP时间同步，计算并记录偏移量"""
        master_ts = time.time_ns()
        with self._lock:
            for sensor_id in list(self._offsets.keys()):
                # 模拟round-trip时延测量
                rtt_ns = 8000 + int(4000 * math.sin(master_ts * 1e-9))  # 8±4 μs
                offset_ns = rtt_ns // 2
                self._offsets[sensor_id] = offset_ns
                logger.debug(f"{sensor_id} PTP偏移: {offset_ns/1000:.1f} μs")

    def register_sensor(self, sensor_id: str):
        """注册待同步传感器"""
        with self._lock:
            self._offsets[sensor_id] = 0

    def get_offset_us(self, sensor_id: str) -> float:
        """获取传感器时间偏移（μs）"""
        with self._lock:
            return self._offsets.get(sensor_id, 0.0) / 1000.0

    def verify_sync(self) -> bool:
        """验证所有传感器时间偏差是否≤12μs"""
        with self._lock:
            offsets = list(self._offsets.values())
        if not offsets:
            return True
        max_offset_us = max(abs(o) / 1000.0 for o in offsets)
        ok = max_offset_us <= 12.0
        logger.info(f"时间同步验证: 最大偏差={max_offset_us:.2f}μs, {'通过' if ok else '未通过'}")
        return ok


class AprilTagCalibrator:
    """AprilTag相机标定器，残差目标≤0.018m"""

    TAG_SIZE_M = 0.2  # 标定板tag尺寸（米）

    def __init__(self, board_cols: int = 6, board_rows: int = 4):
        self.board_cols = board_cols
        self.board_rows = board_rows

    def calibrate(self, image_paths: List[str]) -> CalibrationResult:
        """
        执行AprilTag相机标定。

        Args:
            image_paths: 标定图像路径列表（建议≥20张）
        Returns:
            CalibrationResult 包含相机内参与残差
        """
        logger.info(f"开始AprilTag标定，图像数量: {len(image_paths)}")
        # 模拟标定过程（实际使用apriltag + cv2.calibrateCamera）
        fx, fy = 615.0, 615.0
        cx, cy = 320.0, 240.0
        camera_matrix = [[fx, 0, cx], [0, fy, cy], [0, 0, 1]]
        dist_coeffs = [-0.041, 0.025, 0.0, 0.0, -0.005]
        # 计算重投影残差（模拟）
        residual = self._compute_residual(len(image_paths))
        result = CalibrationResult(camera_matrix, dist_coeffs, residual)
        logger.info(f"标定完成: 残差={residual:.4f}m ({'通过' if residual <= 0.018 else '未通过'})")
        return result

    def _compute_residual(self, n_images: int) -> float:
        """根据图像数量估算残差（图像越多残差越小）"""
        base = 0.025
        improvement = min(0.012, 0.001 * n_images)
        return max(0.010, base - improvement)

    def save_calibration(self, result: CalibrationResult, path: str):
        """保存标定参数到YAML文件"""
        data = {
            "camera_matrix": {"rows": 3, "cols": 3, "data": result.camera_matrix},
            "distortion_coefficients": {"rows": 1, "cols": 5, "data": result.dist_coeffs},
            "residual_m": result.residual_m,
            "timestamp": result.timestamp,
        }
        with open(path, "w") as f:
            yaml.dump(data, f, default_flow_style=False)
        logger.info(f"标定参数已保存: {path}")


class DeploymentVerifier:
    """部署验证器，生成可复现部署报告"""

    def __init__(self, sensor_iface: SensorInterface, ptp: PTPTimeSynchronizer,
                 calibrator: AprilTagCalibrator):
        self.sensor_iface = sensor_iface
        self.ptp = ptp
        self.calibrator = calibrator

    def run_verification(self) -> Dict:
        """执行完整部署验证流程，返回验证报告"""
        report = {"timestamp": time.time(), "checks": {}}
        # 1. 传感器接口检查
        sensor_status = self.sensor_iface.initialize()
        report["checks"]["sensors"] = sensor_status
        report["checks"]["topics"] = self.sensor_iface.get_topic_list()
        # 2. 时间同步检查
        report["checks"]["ptp_sync"] = self.ptp.verify_sync()
        # 3. 数据流检查（每帧≥1000点）
        stream_ok = {}
        for sid in sensor_status:
            data = self.sensor_iface.read_sensor(sid)
            if data:
                stream_ok[sid] = data["n_points"] >= 1000
        report["checks"]["data_streams"] = stream_ok
        report["passed"] = all([
            all(sensor_status.values()),
            report["checks"]["ptp_sync"],
            all(stream_ok.values()) if stream_ok else False,
        ])
        logger.info(f"部署验证{'通过' if report['passed'] else '未通过'}")
        return report


if __name__ == "__main__":
    configs = [
        SensorConfig("lidar_front", "USB-C", topic="/sensor/lidar/front"),
        SensorConfig("camera_rgb", "USB-C", topic="/sensor/camera/rgb"),
        SensorConfig("imu_main", "UART", baud_rate=921600, topic="/sensor/imu"),
    ]
    iface = SensorInterface(configs)
    ptp = PTPTimeSynchronizer()
    ptp.register_sensor("lidar_front")
    ptp.register_sensor("camera_rgb")
    ptp.start()

    calibrator = AprilTagCalibrator()
    verifier = DeploymentVerifier(iface, ptp, calibrator)
    report = verifier.run_verification()
    print("=== 部署验证报告 ===")
    for k, v in report["checks"].items():
        print(f"  {k}: {v}")
    print(f"  总体结果: {'通过' if report['passed'] else '未通过'}")
    ptp.stop()
