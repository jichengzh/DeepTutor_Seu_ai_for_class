"""
模块3：轻量化在线感知算法开发与边缘部署验证
技术栈：MobileNetV3-Small + SE Block + Deformable Conv, TensorRT, ROS2
输出：/perception/obstacle_array 消息
"""

import time
import math
import logging
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Tuple

import numpy as np

logger = logging.getLogger(__name__)
logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")


@dataclass
class ObstacleMsg:
    """ROS2 /perception/obstacle_array 消息结构"""
    stamp: float
    frame_id: str
    obstacles: List[Dict]   # 每个元素: {id, class, x, y, z, w, h, d, confidence}


@dataclass
class LatencyReport:
    """端到端延迟测试报告"""
    n_frames: int
    mean_ms: float
    p95_ms: float
    p99_ms: float
    max_ms: float
    pass_rate: float        # 延迟≤target的帧比例


class SEBlock:
    """Squeeze-and-Excitation模块（NumPy模拟）"""

    def __init__(self, channels: int, reduction: int = 4):
        self.channels = channels
        self.reduction = reduction
        mid = channels // reduction
        # 模拟SE权重（正态初始化）
        self._w1 = np.random.randn(channels, mid).astype("float32") * 0.1
        self._w2 = np.random.randn(mid, channels).astype("float32") * 0.1

    def forward(self, x: np.ndarray) -> np.ndarray:
        """
        SE前向传播: 通道注意力加权。

        Args:
            x: (C,) 或 (N, C) 特征向量
        Returns:
            注意力加权后的特征
        """
        if x.ndim == 1:
            x = x[np.newaxis, :]
        # Global Average Pooling
        gap = x.mean(axis=0)
        # FC1 + ReLU
        h = np.maximum(0, gap @ self._w1)
        # FC2 + Sigmoid
        scale = 1.0 / (1.0 + np.exp(-(h @ self._w2)))
        return (x * scale).squeeze()


class DeformableConvSimulator:
    """可变形卷积模拟器（NumPy实现，用于验证接口）"""

    def __init__(self, in_channels: int, out_channels: int, kernel_size: int = 3):
        self.in_ch = in_channels
        self.out_ch = out_channels
        self.k = kernel_size
        # 模拟卷积核权重
        self._weight = np.random.randn(out_channels, in_channels, kernel_size, kernel_size
                                        ).astype("float32") * 0.01
        # 可变形偏移量网络（输出2*k*k个偏移）
        self._offset_w = np.random.randn(2 * kernel_size * kernel_size, in_channels,
                                          1, 1).astype("float32") * 0.01

    def forward(self, feature_map: np.ndarray) -> np.ndarray:
        """可变形卷积前向（模拟，返回相同形状特征图）"""
        # 实际应实现双线性插值采样，此处返回模拟输出
        n = feature_map.shape[0] if feature_map.ndim > 1 else 1
        out = np.random.randn(n, self.out_ch).astype("float32") * 0.1
        return out.squeeze()


class LiteNavNet:
    """
    轻量化导航感知网络: MobileNetV3-Small + SE Block + Deformable Conv
    骨干网络用NumPy模拟，实际部署时使用PyTorch/TensorRT版本
    """

    def __init__(self, num_classes: int = 8, input_size: Tuple[int, int] = (640, 640)):
        self.num_classes = num_classes
        self.input_size = input_size
        # 特征维度（MobileNetV3-Small最后一层）
        self._feat_dim = 96
        self.se = SEBlock(self._feat_dim)
        self.dcn = DeformableConvSimulator(self._feat_dim, 128)
        # 检测头权重（模拟）
        self._head_w = np.random.randn(128, num_classes + 5).astype("float32") * 0.01
        logger.info(f"LiteNavNet初始化: {num_classes}类, 输入{input_size}")

    def forward(self, image: np.ndarray) -> List[Dict]:
        """
        前向推理，返回检测结果列表。

        Args:
            image: (H, W, 3) uint8图像
        Returns:
            检测框列表 [{"class_id", "confidence", "bbox": [x,y,w,h]}]
        """
        # 1. 模拟MobileNetV3-Small骨干特征提取
        backbone_feat = np.random.randn(self._feat_dim).astype("float32")
        # 2. SE Block注意力
        feat = self.se.forward(backbone_feat)
        # 3. Deformable Conv
        feat = self.dcn.forward(feat)
        # 4. 检测头
        logits = feat @ self._head_w
        # 5. 解码检测框（模拟NMS后结果）
        detections = self._decode(logits)
        return detections

    def _decode(self, logits: np.ndarray) -> List[Dict]:
        """解码检测头输出为目标框列表（支持1D/2D logits）"""
        results = []
        # 确保logits为2D: (n_anchors, num_classes+5)
        if logits.ndim == 1:
            logits = logits.reshape(1, -1)
        n_pred = min(logits.shape[0], 3)
        for i in range(n_pred):
            row = logits[i]
            if len(row) < 5:
                continue
            conf = float(1.0 / (1.0 + math.exp(-float(row[4]))))
            if conf < 0.3:
                continue
            cls_id = int(np.argmax(row[5:])) if len(row) > 5 else 0
            results.append({
                "class_id": cls_id,
                "confidence": conf,
                "bbox": [float(x) for x in row[:4].tolist()],
            })
        return results

    def load_weights(self, path: str):
        """加载模型权重（模拟）"""
        logger.info(f"加载LiteNavNet权重: {path}")
        # 实际：torch.load(path, map_location="cpu")


class TensorRTOptimizer:
    """TensorRT加速优化器（Jetson Orin Nano部署）"""

    def __init__(self, target_latency_ms: float = 85.0):
        self.target_latency_ms = target_latency_ms
        self._engine_loaded = False

    def build_engine(self, onnx_path: str, precision: str = "int8") -> bool:
        """
        从ONNX文件构建TensorRT引擎。

        Args:
            onnx_path: ONNX模型路径
            precision: "fp32" | "fp16" | "int8"
        Returns:
            构建是否成功
        """
        logger.info(f"构建TRT引擎: {onnx_path} [{precision}]")
        # 实际：使用tensorrt.Builder, INetworkDefinition等API
        self._engine_loaded = True
        logger.info("TRT引擎构建完成")
        return True

    def infer(self, image: np.ndarray) -> Tuple[List[Dict], float]:
        """执行TRT推理，返回(检测结果, 延迟ms)"""
        t0 = time.perf_counter()
        # 模拟TRT推理延迟（INT8@Jetson Orin Nano约30-60ms）
        time.sleep(0.035 + 0.010 * np.random.rand())
        detections = [{"class_id": 0, "confidence": 0.92, "bbox": [0.3, 0.4, 0.1, 0.1]}]
        latency_ms = (time.perf_counter() - t0) * 1000
        return detections, latency_ms


class PerceptionNode:
    """ROS2感知节点，发布/perception/obstacle_array"""

    TOPIC = "/perception/obstacle_array"

    def __init__(self, model: LiteNavNet, trt: Optional[TensorRTOptimizer] = None):
        self.model = model
        self.trt = trt
        self._latencies: List[float] = []
        self._use_trt = trt is not None and trt._engine_loaded
        logger.info(f"PerceptionNode已启动, TRT={'启用' if self._use_trt else '禁用'}")

    def process_frame(self, image: np.ndarray) -> ObstacleMsg:
        """处理单帧图像，返回障碍物消息"""
        t0 = time.perf_counter()
        if self._use_trt:
            detections, lat = self.trt.infer(image)
        else:
            detections = self.model.forward(image)
            lat = (time.perf_counter() - t0) * 1000
        self._latencies.append(lat)
        msg = ObstacleMsg(
            stamp=time.time(),
            frame_id="base_link",
            obstacles=[{
                "id": i, "class": d["class_id"],
                "confidence": d["confidence"],
                "x": d["bbox"][0], "y": d["bbox"][1],
                "w": d["bbox"][2], "h": d["bbox"][3], "d": 0.0,
            } for i, d in enumerate(detections)]
        )
        return msg

    def publish(self, msg: ObstacleMsg):
        """模拟发布ROS2消息到/perception/obstacle_array"""
        logger.debug(f"[{self.TOPIC}] {len(msg.obstacles)} 个障碍物")

    def benchmark(self, n_frames: int = 100, target_ms: float = 85.0) -> LatencyReport:
        """
        端到端延迟基准测试。

        Args:
            n_frames: 测试帧数
            target_ms: 目标延迟上限（ms）
        Returns:
            LatencyReport 延迟统计报告
        """
        logger.info(f"开始延迟基准测试: {n_frames} 帧")
        dummy = np.random.randint(0, 255, (480, 640, 3), dtype=np.uint8)
        for _ in range(n_frames):
            msg = self.process_frame(dummy)
            self.publish(msg)
        lats = np.array(self._latencies[-n_frames:])
        report = LatencyReport(
            n_frames=n_frames,
            mean_ms=float(np.mean(lats)),
            p95_ms=float(np.percentile(lats, 95)),
            p99_ms=float(np.percentile(lats, 99)),
            max_ms=float(np.max(lats)),
            pass_rate=float((lats <= target_ms).mean()),
        )
        logger.info(f"延迟测试完成: 均值={report.mean_ms:.1f}ms, "
                    f"P95={report.p95_ms:.1f}ms, 通过率={report.pass_rate*100:.1f}%")
        return report


if __name__ == "__main__":
    net = LiteNavNet(num_classes=8)
    trt = TensorRTOptimizer(target_latency_ms=85.0)
    trt.build_engine("litenavnet.onnx", precision="int8")
    node = PerceptionNode(net, trt)
    report = node.benchmark(n_frames=50)
    print(f"=== 端到端延迟测试报告 ===")
    print(f"  均值延迟: {report.mean_ms:.2f} ms")
    print(f"  P95延迟:  {report.p95_ms:.2f} ms")
    print(f"  P99延迟:  {report.p99_ms:.2f} ms")
    print(f"  通过率:   {report.pass_rate*100:.1f}% (<85ms)")
