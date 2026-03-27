# 无人机智能感知与自主导航系统开发实习

基于 ROS2 Humble + PyTorch + TensorRT 的无人机全栈感知导航系统，运行于 NVIDIA Jetson Orin Nano（JetPack 5.1.2）。

---

## 项目介绍

本项目模拟真实无人机开发实习场景，覆盖从硬件集成、感知模型训练、边缘部署到多机协同和自主避障的完整工程链路：

| 模块 | 功能 | 关键指标 |
|------|------|----------|
| module1_hardware | 传感器集成、PTP时间同步、AprilTag标定 | 残差≤0.018m, 时偏≤12μs |
| module2_training | YOLOv8数据集构建与端到端训练 | 延迟≤85ms@1080p |
| module3_perception | LiteNavNet边缘推理、TensorRT加速 | /perception/obstacle_array |
| module4_collaborative | 多机协同、故障注入、SLAM重定位 | 重定位误差＜0.3m |
| module5_mapping | 激光-相机对齐、语义建图、OctoMap | ATE≤0.14m, IoU≥76.3% |
| module6_avoidance | TEB规划、TTC预测、安全飞控闭环 | 避障≥99.2%, 响应≤142ms |

---

## 环境要求

- **OS**: Ubuntu 22.04 LTS
- **Python**: 3.10+
- **硬件**: NVIDIA Jetson Orin Nano（JetPack 5.1.2）或 x86 PC（开发模式）
- **ROS2**: Humble Hawksbill（可选，节点接口已抽象）

---

## 安装步骤

```bash
# 1. 克隆仓库
git clone <repo_url> drone_nav
cd drone_nav

# 2. 创建并激活虚拟环境
python3.10 -m venv .venv
source .venv/bin/activate

# 3. 安装依赖
pip install --upgrade pip
pip install -r requirements.txt

# 4. 验证安装
python -c "import sys; sys.path.insert(0,'src'); import module1_hardware; print('OK')"

# 5. 一键部署（含测试）
bash scripts/deploy.sh
```

---

## 运行示例

### 模块1：硬件集成验证
```bash
python src/module1_hardware.py
```
**预期输出：**
```
2025-xx-xx [INFO] 初始化传感器 lidar_front [USB-C@115200]
2025-xx-xx [INFO] PTP时间同步已启动
2025-xx-xx [INFO] 时间同步验证: 最大偏差=5.23μs, 通过
=== 部署验证报告 ===
  sensors: {'lidar_front': True, 'camera_rgb': True, 'imu_main': True}
  topics: ['/sensor/lidar/front', '/sensor/camera/rgb', '/sensor/imu']
  总体结果: 通过
```

### 模块3：感知节点延迟测试
```bash
python src/module3_perception.py
```
**预期输出：**
```
=== 端到端延迟测试报告 ===
  均值延迟: 42.35 ms
  P95延迟:  58.12 ms
  通过率:   100.0% (<85ms)
```

### 模块6：安全飞控验证
```bash
python src/module6_avoidance.py
```
**预期输出：**
```
=== 安全验证报告 ===
  避障成功率: 100.00%
  应急响应均值: 0.1ms (目标≤142ms)
  应急响应通过率: 100.0%
```

### 运行全套测试
```bash
pytest tests/test_modules.py -v
```
**预期输出：** 25+ 测试全部通过

---

## 项目结构说明

```
drone_nav/
├── README.md                    # 本文档
├── requirements.txt             # Python依赖（含版本号）
├── PLAN.md                      # 项目计划书
├── src/
│   ├── module1_hardware.py      # 硬件集成与嵌入式部署
│   ├── module2_training.py      # 数据采集与模型训练
│   ├── module3_perception.py    # 轻量化感知算法
│   ├── module4_collaborative.py # 多机协同与鲁棒性测试
│   ├── module5_mapping.py       # 时空对齐与语义建图
│   └── module6_avoidance.py     # 避障决策与飞控验证
├── tests/
│   └── test_modules.py          # 全模块单元测试（pytest）
└── scripts/
    └── deploy.sh                # 一键部署脚本
```

---

## 各模块功能说明

### module1_hardware — 硬件集成与嵌入式环境部署
- `SensorInterface`: UART/USB-C传感器接口，DC-DC电源域隔离检查
- `PTPTimeSynchronizer`: PTP协议时间同步，传感器间偏差≤12μs
- `AprilTagCalibrator`: AprilTag相机标定，残差目标≤0.018m
- `DeploymentVerifier`: 生成可复现部署验证报告

### module2_training — 多源异构感知数据采集与训练
- `YOLODatasetManager`: 生成YOLOv8格式结构化标注数据集
- `CutMixAugmentation`: CutMix数据增强（alpha=1.0）
- `ModelTrainer`: 支持梯度裁剪、类别权重重采样的训练器
- `ONNXExporter`: ONNX+INT8量化模型导出

### module3_perception — 轻量化在线感知算法
- `LiteNavNet`: MobileNetV3-Small + SE Block + Deformable Conv推理网络
- `TensorRTOptimizer`: INT8 TensorRT引擎构建与推理
- `PerceptionNode`: 发布`/perception/obstacle_array`的ROS2节点

### module4_collaborative — 多机协同与鲁棒性测试
- `MQTTBrokerSimulator`: MQTT多机通信总线
- `IMURadarFusion`: EKF融合IMU+雷达（视觉失效备份）
- `SLAMRelocalizer`: ORB-SLAM3/RTAB-Map重定位接口
- `FaultInjector`: 故障注入与MTTF/MTTR计算

### module5_mapping — 多传感器时空对齐与三维语义建图
- `TemporalAligner`: 激光-相机时间对齐（≤12μs）
- `ExtrinsicCalibration`: 激光-相机外参标定（残差≤0.018m）
- `SLAMTrajectoryEvaluator`: ATE RMSE评估（TUM格式）
- `SemanticMapper`: 语义标签融合与mIoU计算
- `OctoMapBuilder`: 语义OctoMap构建（体积≤85MB）

### module6_avoidance — 自主避障决策与安全飞控
- `TEBPlannerConfig`: TEB局部规划器参数配置（加速度≤1.4g）
- `TTCPredictor`: 碰撞时间预测与应急响应日志
- `EmergencyResponseSystem`: 失效响应（≤142ms）
- `AvoidanceController`: TEB避障路径规划（成功率≥99.2%）
- `SafetyValidator`: 自动化安全验证测试套件
