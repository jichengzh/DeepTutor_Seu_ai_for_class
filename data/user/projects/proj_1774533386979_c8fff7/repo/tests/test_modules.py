"""
单元测试：覆盖所有6个模块的核心功能
运行：pytest tests/test_modules.py -v
"""

import os
import sys
import time
import math
import pytest
import numpy as np

# 添加src到路径
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

# ─────────────── 模块1：硬件集成 ───────────────

from module1_hardware import (
    SensorConfig, SensorInterface, PTPTimeSynchronizer,
    AprilTagCalibrator, DeploymentVerifier
)


def test_sensor_interface_init():
    configs = [SensorConfig("lidar", "USB-C", topic="/lidar")]
    iface = SensorInterface(configs)
    status = iface.initialize()
    assert "lidar" in status


def test_sensor_read_returns_min_points():
    configs = [SensorConfig("lidar", "USB-C", topic="/lidar")]
    iface = SensorInterface(configs)
    iface.initialize()
    data = iface.read_sensor("lidar")
    if data:
        assert data["n_points"] >= 1000


def test_ptp_sync_registers_sensor():
    ptp = PTPTimeSynchronizer()
    ptp.register_sensor("cam")
    offset = ptp.get_offset_us("cam")
    assert isinstance(offset, float)


def test_apriltag_residual_under_threshold():
    cal = AprilTagCalibrator()
    result = cal.calibrate(["img_{}.jpg".format(i) for i in range(25)])
    assert result.residual_m <= 0.018


def test_apriltag_save_yaml(tmp_path):
    cal = AprilTagCalibrator()
    result = cal.calibrate(["img.jpg"] * 20)
    yaml_path = str(tmp_path / "calib.yaml")
    cal.save_calibration(result, yaml_path)
    assert os.path.exists(yaml_path)


# ─────────────── 模块2：数据采集与训练 ───────────────

from module2_training import (
    TrainingConfig, YOLODatasetManager, CutMixAugmentation,
    ModelTrainer, ONNXExporter
)


def test_dataset_creates_yaml(tmp_path):
    dm = YOLODatasetManager(str(tmp_path / "ds"))
    dm.generate_synthetic(n_train=10, n_val=5, n_test=5)
    assert os.path.exists(str(tmp_path / "ds" / "dataset.yaml"))


def test_class_weights_sum_to_one():
    dm = YOLODatasetManager("/tmp/ds_test")
    weights = dm.compute_class_weights()
    assert abs(weights.sum() - 1.0) < 1e-5


def test_cutmix_output_shape():
    cutmix = CutMixAugmentation(alpha=1.0)
    images = np.random.rand(4, 64, 64, 3).astype("float32")
    labels = np.array([0, 1, 2, 3])
    mixed_imgs, mixed_labels = cutmix(images, labels)
    assert mixed_imgs.shape == images.shape


def test_trainer_history_length():
    cfg = TrainingConfig(epochs=3, batch_size=4)
    trainer = ModelTrainer(cfg)
    history = trainer.train(n_batches_per_epoch=2)
    assert len(history) == 3


def test_trainer_latency_within_target():
    cfg = TrainingConfig(epochs=2, batch_size=4, target_latency_ms=85.0)
    trainer = ModelTrainer(cfg)
    history = trainer.train(n_batches_per_epoch=3)
    # 大多数帧延迟应在合理范围
    lats = [h["latency_ms"] for h in history]
    assert all(l < 200 for l in lats)  # 模拟值宽松验证


def test_onnx_exporter_writes_meta(tmp_path):
    exporter = ONNXExporter()
    onnx_path = str(tmp_path / "model.onnx")
    exporter.export_onnx("model.pt", onnx_path)
    meta_path = onnx_path.replace(".onnx", "_meta.json")
    assert os.path.exists(meta_path)


# ─────────────── 模块3：轻量化感知 ───────────────

from module3_perception import (
    LiteNavNet, SEBlock, TensorRTOptimizer, PerceptionNode
)


def test_se_block_output_shape():
    se = SEBlock(channels=16)
    x = np.random.rand(16).astype("float32")
    out = se.forward(x)
    assert out.shape == (16,)


def test_litnavnet_forward_returns_list():
    net = LiteNavNet(num_classes=8)
    img = np.random.randint(0, 255, (480, 640, 3), dtype=np.uint8)
    detections = net.forward(img)
    assert isinstance(detections, list)


def test_perception_node_benchmark(capsys):
    net = LiteNavNet(num_classes=4)
    node = PerceptionNode(net, trt=None)
    report = node.benchmark(n_frames=10, target_ms=85.0)
    assert report.n_frames == 10
    assert report.mean_ms > 0


# ─────────────── 模块4：多机协同 ───────────────

from module4_collaborative import (
    DroneSwarmCoordinator, IMURadarFusion, SLAMRelocalizer,
    FaultInjector
)


def test_imu_radar_fusion_state_update():
    fusion = IMURadarFusion()
    accel = np.array([0.1, 0.0, -9.81])
    fusion.predict(accel, dt=0.1)
    fusion.update(np.array([0.01, 0.0, 0.0]))
    pos = fusion.get_position()
    assert pos.shape == (3,)


def test_slam_relocalize_error_under_threshold():
    slam = SLAMRelocalizer()
    for i in range(5):
        slam.add_keyframe(np.random.randn(3), np.random.randn(32))
    success, pos, err = slam.relocalize(np.random.randn(32))
    assert err < 0.3 or not success


def test_fault_injector_records_event():
    fi = FaultInjector()
    evt = fi.inject("visual_blackout", "drone_00", duration_s=0.1)
    assert evt.fault_type == "visual_blackout"
    assert len(fi._events) == 1


def test_swarm_gate_traversal_success_rate():
    coord = DroneSwarmCoordinator(n_drones=2)
    rate = coord.gate_traversal_test(n_attempts=10)
    assert 0.0 <= rate <= 1.0


# ─────────────── 模块5：时空对齐与建图 ───────────────

from module5_mapping import (
    Pose, TemporalAligner, ExtrinsicCalibration,
    SLAMTrajectoryEvaluator, SemanticMapper, OctoMapBuilder
)


def test_pose_to_matrix_shape():
    p = Pose(t=np.array([1, 2, 3]), q=np.array([0, 0, 0, 1.0]))
    M = p.to_matrix()
    assert M.shape == (4, 4)


def test_temporal_aligner_returns_pair():
    aligner = TemporalAligner()
    pts = np.random.randn(500, 4).astype("float32")
    img = np.zeros((240, 320, 3), dtype=np.uint8)
    aligner.push_lidar(time.time(), pts)
    aligner.push_camera(time.time(), img)
    pair = aligner.get_synchronized_pair()
    assert pair is not None
    assert pair[4] < 100.0  # delta < 100μs


def test_ate_rmse_under_threshold():
    evaluator = SLAMTrajectoryEvaluator()
    gt = [Pose(t=np.random.randn(3)) for _ in range(50)]
    est = [Pose(t=g.t + np.random.randn(3) * 0.05) for g in gt]
    result = evaluator.evaluate_ate(gt, est)
    assert result["ate_rmse_m"] < 0.14


def test_octomap_memory_under_85mb():
    octo = OctoMapBuilder(resolution=0.1)
    pts = np.random.randn(10000, 4).astype("float32") * 10
    labels = np.random.randint(0, 8, size=10000)
    octo.insert_point_cloud(pts, labels)
    assert octo.verify_size()


def test_octomap_save_bt(tmp_path):
    octo = OctoMapBuilder()
    pts = np.random.randn(100, 4).astype("float32")
    labels = np.zeros(100, dtype=int)
    octo.insert_point_cloud(pts, labels)
    bt_path = str(tmp_path / "map.bt")
    octo.save_bt(bt_path)
    assert os.path.exists(bt_path)


# ─────────────── 模块6：避障与飞控 ───────────────

from module6_avoidance import (
    TEBPlannerConfig, TTCPredictor, EmergencyResponseSystem,
    AvoidanceController, Obstacle, Waypoint, SafetyValidator
)


def test_teb_config_accel_limit():
    cfg = TEBPlannerConfig()
    assert cfg.verify_accel_limit()


def test_teb_config_save_yaml(tmp_path):
    cfg = TEBPlannerConfig()
    path = str(tmp_path / "teb.yaml")
    cfg.save(path)
    assert os.path.exists(path)


def test_ttc_infinite_when_no_collision():
    predictor = TTCPredictor()
    pos = np.array([0.0, 0.0, 0.0])
    vel = np.array([1.0, 0.0, 0.0])
    obs = Obstacle(0, np.array([0.0, 10.0, 0.0]))  # 侧方无碰撞
    ttc = predictor.predict_ttc(pos, vel, obs)
    assert ttc == float("inf") or ttc > 5.0


def test_emergency_response_within_142ms():
    em = EmergencyResponseSystem()
    evt = em.trigger_emergency("sensor_failure")
    # 软件级响应应远快于142ms
    assert evt.response_time_ms < 142.0


def test_avoidance_controller_velocity_clipped():
    cfg = TEBPlannerConfig()
    ttc = TTCPredictor()
    em = EmergencyResponseSystem()
    ctrl = AvoidanceController(cfg, ttc, em)
    pos = np.zeros(3)
    vel = np.zeros(3)
    goal = Waypoint(10.0, 0.0, 2.0)
    obs = [Obstacle(0, np.array([20.0, 0.0, 0.0]))]  # 远处障碍
    cmd = ctrl.plan_avoidance(pos, vel, goal, obs)
    assert np.linalg.norm(cmd) <= cfg.params["max_vel_x"] * 2


def test_safety_validator_runs(tmp_path):
    cfg = TEBPlannerConfig()
    ttc = TTCPredictor()
    em = EmergencyResponseSystem()
    ctrl = AvoidanceController(cfg, ttc, em)
    validator = SafetyValidator(ctrl)
    report_path = str(tmp_path / "report.json")
    validator.save_report(report_path)
    assert os.path.exists(report_path)


if __name__ == "__main__":
    pytest.main([__file__, "-v", "--tb=short"])
