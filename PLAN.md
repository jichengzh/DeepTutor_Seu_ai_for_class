# ROS机器人自主导航系统开发与实操实习 — 代码架构计划

> **技术栈**: C++17, Python 3.10, ROS 2 Humble (主), ROS 1 Noetic (可选桥接)
> **运行环境**: Ubuntu 22.04 (主) / Ubuntu 20.04 (可选)
> **生成日期**: 2026-03-24

---

## 目录

1. [项目目录结构](#1-项目目录结构)
2. [技术选型说明](#2-技术选型说明)
3. [模块与文件对应关系](#3-模块与文件对应关系)
4. [主要类/函数接口定义](#4-主要类函数接口定义)
5. [运行方式](#5-运行方式)

---

## 1. 项目目录结构

```
ros_nav_system/                          # 工作空间根目录
├── README.md
├── PLAN.md                              # 本文件
├── .clang-format                        # C++ 代码风格配置 (Google Style)
├── .pre-commit-config.yaml              # pre-commit 钩子（clang-tidy, black, isort）
├── docker/
│   ├── Dockerfile.humble                # Ubuntu 22.04 + ROS 2 Humble 镜像
│   ├── Dockerfile.noetic                # Ubuntu 20.04 + ROS 1 Noetic 镜像
│   └── docker-compose.yml              # 多容器编排（主节点 + 可视化 + 测试）
├── scripts/
│   ├── setup_workspace.sh               # 一键初始化 colcon 工作空间
│   ├── run_simulation.sh                # 启动 Gazebo 仿真环境
│   └── run_tests.sh                     # 运行全量测试套件
│
├── src/                                 # ROS 2 功能包根目录（colcon build 入口）
│   │
│   ├── module_1_localization/           # ── 模块1：多传感器融合定位 ──────────────
│   │   ├── CMakeLists.txt
│   │   ├── package.xml
│   │   ├── include/module_1_localization/
│   │   │   ├── sensor_sync_manager.hpp  # 激光/IMU/编码器时间同步管理器
│   │   │   ├── tf_aligner.hpp           # 坐标系对齐与外参标定接口
│   │   │   ├── amcl_likelihood_field.hpp # 重写的 likelihood_field_model
│   │   │   └── localization_lifecycle.hpp # LifecycleNode 定位节点状态机
│   │   ├── src/
│   │   │   ├── sensor_sync_manager.cpp  # 基于 message_filters ApproximateTime 同步
│   │   │   ├── tf_aligner.cpp           # 静态/动态 TF 广播，外参在线标定
│   │   │   ├── amcl_likelihood_field.cpp # 非理想环境适配：光线截断+混合模型
│   │   │   └── localization_lifecycle.cpp # configure/activate/deactivate 状态回调
│   │   ├── config/
│   │   │   ├── sensor_sync_params.yaml  # 时间戳容差、队列深度参数
│   │   │   └── amcl_params.yaml         # 粒子数、运动模型、传感器权重
│   │   ├── launch/
│   │   │   └── localization.launch.py   # 组合启动：sync + amcl + lifecycle_manager
│   │   └── test/
│   │       ├── test_sensor_sync.cpp     # GTest：时间同步精度验证
│   │       └── test_amcl_model.py       # pytest：似然场模型单元测试
│   │
│   ├── module_2_planning/               # ── 模块2：分层导航栈路径规划 ───────────
│   │   ├── CMakeLists.txt
│   │   ├── package.xml
│   │   ├── include/module_2_planning/
│   │   │   ├── hybrid_astar_planner.hpp # 改进型 Hybrid A* 全局规划器接口
│   │   │   ├── dynamic_obstacle_predictor.hpp # 动态障碍物轨迹预测
│   │   │   ├── dwb_dynamic_critic.hpp   # DWB 动态障碍物代价评分插件
│   │   │   ├── trajectory_optimizer.hpp # 轨迹优化组件（独立 ROS 2 节点）
│   │   │   └── velocity_clipper.hpp     # 速度裁剪组件（独立 ROS 2 节点）
│   │   ├── src/
│   │   │   ├── hybrid_astar_planner.cpp # Reeds-Shepp 曲线 + 启发式代价函数
│   │   │   ├── dynamic_obstacle_predictor.cpp # 卡尔曼滤波轨迹外推
│   │   │   ├── dwb_dynamic_critic.cpp   # 基于预测轨迹的 DWB 代价项
│   │   │   ├── trajectory_optimizer.cpp # IPOPT/CasADi 数值优化
│   │   │   └── velocity_clipper.cpp     # 加速度/转向角约束裁剪
│   │   ├── plugins/
│   │   │   └── planner_plugins.xml      # pluginlib 插件描述文件
│   │   ├── config/
│   │   │   ├── hybrid_astar_params.yaml # 搜索步长、转向分辨率、启发权重
│   │   │   └── dwb_critic_params.yaml   # 动态代价权重、预测时域
│   │   ├── launch/
│   │   │   └── planning_stack.launch.py # 启动规划器+优化器+裁剪器三节点
│   │   └── test/
│   │       ├── test_hybrid_astar.cpp    # GTest：典型场景路径可达性
│   │       └── test_velocity_clipper.py # pytest：约束边界条件验证
│   │
│   ├── module_3_mapping/                # ── 模块3：语义增强探索与建图 ───────────
│   │   ├── CMakeLists.txt
│   │   ├── package.xml
│   │   ├── include/module_3_mapping/
│   │   │   ├── frontier_explorer.hpp    # Frontier 驱动主动探索控制器
│   │   │   ├── yolov8_detector.hpp      # YOLOv8 ROS 2 推理封装
│   │   │   ├── semantic_map_annotator.hpp # 语义标注反向注册至 SLAM 地图
│   │   │   └── semantic_consistency.hpp  # 多趟建图语义一致性对齐
│   │   ├── src/
│   │   │   ├── frontier_explorer.cpp    # 基于信息增益的 Frontier 评分与选择
│   │   │   ├── yolov8_detector.cpp      # ONNX Runtime 推理 + 检测结果发布
│   │   │   ├── semantic_map_annotator.cpp # 3D bbox 投影至栅格地图语义层
│   │   │   └── semantic_consistency.cpp  # ICP 对齐 + 语义标签投票融合
│   │   ├── models/
│   │   │   └── yolov8n.onnx             # 预训练 YOLOv8n 模型（轻量）
│   │   ├── config/
│   │   │   ├── frontier_params.yaml     # 探索半径、信息增益阈值
│   │   │   └── semantic_params.yaml     # 类别映射、置信度阈值、对齐迭代数
│   │   ├── launch/
│   │   │   └── semantic_mapping.launch.py # 启动探索+检测+注标+一致性节点
│   │   └── test/
│   │       ├── test_frontier_explorer.py  # pytest：探索目标选择逻辑
│   │       └── test_semantic_annotator.py # pytest：投影精度与标签一致性
│   │
│   ├── module_4_dynamic_avoidance/      # ── 模块4：动态障碍物感知与避障 ─────────
│   │   ├── CMakeLists.txt
│   │   ├── package.xml
│   │   ├── include/module_4_dynamic_avoidance/
│   │   │   ├── euclidean_cluster.hpp    # 欧氏聚类点云分割
│   │   │   ├── ekf_tracker.hpp          # EKF 6DoF 状态估计器
│   │   │   ├── teb_constraint_plugin.hpp # TEB 非线性速度约束+硬限幅插件
│   │   │   └── avoidance_pipeline.hpp   # 端到端避障响应闭环协调器
│   │   ├── src/
│   │   │   ├── euclidean_cluster.cpp    # PCL 欧氏聚类 + 最小包围盒提取
│   │   │   ├── ekf_tracker.cpp          # 位置/速度/加速度 6DoF EKF 实现
│   │   │   ├── teb_constraint_plugin.cpp # 速度包络约束 + 转向角加速度硬限幅
│   │   │   └── avoidance_pipeline.cpp   # 感知→预测→规划反馈闭环
│   │   ├── config/
│   │   │   ├── cluster_params.yaml      # 聚类半径、最小点数、最大障碍物数
│   │   │   └── teb_params.yaml          # 速度约束上界、转向角加速度限幅值
│   │   ├── launch/
│   │   │   └── dynamic_avoidance.launch.py
│   │   └── test/
│   │       ├── test_ekf_tracker.cpp     # GTest：EKF 收敛性与误差界
│   │       └── test_teb_constraint.py   # pytest：约束满足性仿真验证
│   │
│   ├── module_5_multi_robot/            # ── 模块5：多机器人协同导航 ──────────────
│   │   ├── CMakeLists.txt
│   │   ├── package.xml
│   │   ├── include/module_5_multi_robot/
│   │   │   ├── cbba_task_allocator.hpp  # CBBA 中心化任务分配器
│   │   │   ├── fleet_executor.hpp       # 轻量级舰队执行控制器
│   │   │   ├── rstc_planner.hpp         # RSTC 预规划避让策略
│   │   │   └── tf_chain_validator.hpp   # 跨机器人 TF 链路验证
│   │   ├── src/
│   │   │   ├── cbba_task_allocator.cpp  # 共识竞价+任务束分配算法
│   │   │   ├── fleet_executor.cpp       # BT 行为树驱动多机协调执行
│   │   │   ├── rstc_planner.cpp         # 相对速度投影紧急偏移 + 时空路径段
│   │   │   └── tf_chain_validator.cpp   # TF 树完整性检查 + 时延监控
│   │   ├── msg/
│   │   │   ├── TaskBundle.msg           # 自定义：任务束消息（ID, waypoints, priority）
│   │   │   └── RobotStatus.msg          # 自定义：机器人状态广播
│   │   ├── srv/
│   │   │   └── AllocateTasks.srv        # 服务：触发 CBBA 任务重分配
│   │   ├── config/
│   │   │   ├── cbba_params.yaml         # 竞价折扣因子、收敛迭代上限
│   │   │   └── fleet_params.yaml        # 机器人列表、通信心跳频率
│   │   ├── launch/
│   │   │   ├── fleet_bringup.launch.py  # 启动舰队管理器 + TF 验证器
│   │   │   └── multi_robot_sim.launch.py # Gazebo 多机仿真场景
│   │   └── test/
│   │       ├── test_cbba_allocator.py   # pytest：分配最优性 + 收敛时间
│   │       └── test_rstc_planner.cpp    # GTest：碰撞规避距离保证
│   │
│   ├── module_6_testing/                # ── 模块6：鲁棒性验证与测试平台 ─────────
│   │   ├── CMakeLists.txt
│   │   ├── package.xml
│   │   ├── include/module_6_testing/
│   │   │   ├── scenario_runner.hpp      # OpenSCENARIO 场景解析与执行器
│   │   │   ├── fault_injector.hpp       # ISO 26262 ASIL-B 故障注入框架
│   │   │   ├── realtime_stress_tester.hpp # 实时性压力测试（延迟/抖动监控）
│   │   │   └── report_generator.hpp     # IEEE 829 test_report.xml 生成器
│   │   ├── src/
│   │   │   ├── scenario_runner.cpp      # esmini 集成 + ROS 2 话题桥接
│   │   │   ├── fault_injector.cpp       # 传感器数据篡改/节点崩溃/网络分区注入
│   │   │   ├── realtime_stress_tester.cpp # /clock 话题延迟统计 + 超时告警
│   │   │   └── report_generator.cpp     # TinyXML2 生成符合 IEEE 829 的 XML
│   │   ├── scenarios/
│   │   │   ├── basic_navigation.xosc    # OpenSCENARIO：基础导航场景
│   │   │   ├── dynamic_obstacle.xosc    # OpenSCENARIO：动态障碍物场景
│   │   │   ├── multi_robot_conflict.xosc # OpenSCENARIO：多机冲突场景
│   │   │   └── sensor_degradation.xosc  # OpenSCENARIO：传感器降级场景
│   │   ├── config/
│   │   │   ├── fault_profiles.yaml      # 故障类型、注入概率、持续时间
│   │   │   └── stress_test_params.yaml  # 话题频率、超时阈值、测试时长
│   │   ├── launch/
│   │   │   └── test_platform.launch.py  # 启动测试平台全套组件
│   │   ├── reports/
│   │   │   └── test_report.xml          # IEEE 829 标准测试报告（自动生成）
│   │   └── test/
│   │       ├── test_scenario_runner.py  # pytest：场景解析与执行正确性
│   │       └── test_fault_injector.py   # pytest：故障注入可重复性验证
│   │
│   └── common/                          # ── 公共工具包 ────────────────────────────
│       ├── CMakeLists.txt
│       ├── package.xml
│       ├── include/common/
│       │   ├── math_utils.hpp           # 四元数/欧拉角/SE3 变换工具
│       │   ├── ros2_utils.hpp           # 参数声明、QoS 配置、节点工具宏
│       │   └── logging.hpp              # 统一日志宏（封装 RCLCPP_* 系列）
│       └── src/
│           ├── math_utils.cpp
│           └── ros2_utils.cpp
│
├── simulation/                          # 仿真环境资源
│   ├── worlds/
│   │   ├── warehouse.world              # 仓库场景（模块2/4/5 测试）
│   │   ├── outdoor_campus.world         # 室外校园场景（模块3 探索）
│   │   └── multi_robot_arena.world      # 多机竞技场（模块5 协同）
│   ├── urdf/
│   │   ├── robot_base.urdf.xacro        # 机器人底盘 URDF（差速/全向可配置）
│   │   ├── lidar_sensor.urdf.xacro      # 激光雷达 Xacro 插件
│   │   └── imu_sensor.urdf.xacro        # IMU Xacro 插件
│   └── rviz/
│       ├── navigation.rviz              # 导航可视化配置
│       └── multi_robot.rviz             # 多机协同可视化配置
│
└── docs/
    ├── architecture.drawio              # 系统架构图（可用 draw.io 打开）
    ├── module_interfaces.md             # 各模块 ROS 2 话题/服务/动作接口文档
    └── calibration_guide.md             # 传感器外参标定操作指南
```

---

## 2. 技术选型说明

### 2.1 核心中间件

| 层次 | 选型 | 理由 |
|------|------|------|
| 主框架 | **ROS 2 Humble** | LTS 版本（支持至 2027），LifecycleNode、组件化架构成熟 |
| 可选桥接 | **ROS 1 Noetic + ros1_bridge** | 兼容存量 ROS 1 传感器驱动与遗留包 |
| 通信 | **DDS (FastDDS/CycloneDDS)** | 去中心化发现，支持实时 QoS 策略 |
| 构建 | **colcon + ament_cmake** | ROS 2 官方构建工具，支持并行编译 |

### 2.2 算法库

| 模块 | 库 | 版本 | 用途 |
|------|----|------|------|
| 定位 | nav2_amcl (修改版) | Humble | AMCL 基础算法框架，重写 likelihood_field |
| 规划 | Nav2 + 自定义 plugin | Humble | Hybrid A* 通过 pluginlib 替换默认规划器 |
| 建图 | SLAM Toolbox | 2.6.x | 在线/离线建图，支持语义层扩展 |
| 感知 | PCL 1.12, ONNX Runtime 1.17 | — | 点云聚类、YOLOv8 推理 |
| 优化 | CasADi 3.6 / IPOPT 3.14 | — | 轨迹数值优化（TEB 增强） |
| 滤波 | Eigen 3.4 | — | EKF 矩阵运算 |
| 仿真 | Gazebo Classic 11 / Gazebo Harmonic | — | 物理仿真，OpenSCENARIO 桥接 |
| 测试 | GTest 1.14, pytest 7.x, esmini 2.x | — | 单元/集成/场景化测试 |
| 报告 | TinyXML2 | — | IEEE 829 XML 报告生成 |

### 2.3 开发约定

- **C++17**：RAII、`std::optional`、结构化绑定、`if constexpr`
- **Python 3.10**：类型注解全覆盖，`asyncio` 处理非实时异步任务
- **QoS 策略**：传感器话题使用 `SENSOR_DATA`（BestEffort + Volatile）；导航指令使用 `RELIABLE + TRANSIENT_LOCAL`
- **参数管理**：所有可调参数通过 `.yaml` 文件声明，运行时支持 `ros2 param set` 热更新
- **日志级别**：生产环境 WARN+，调试环境 DEBUG，通过 `--ros-args --log-level` 切换

---

## 3. 模块与文件对应关系

### module_1「基于AMCL的多传感器融合定位实践」

```
src/module_1_localization/
├── include/module_1_localization/
│   ├── sensor_sync_manager.hpp      ← 传感器时间同步（激光/IMU/编码器）
│   ├── tf_aligner.hpp               ← 坐标系对齐，base_link↔sensor_frame 外参
│   ├── amcl_likelihood_field.hpp    ← 重写 likelihood_field_model
│   └── localization_lifecycle.hpp   ← LifecycleNode 状态机封装
├── src/
│   ├── sensor_sync_manager.cpp      ← message_filters ApproximateTime 3路同步
│   ├── tf_aligner.cpp               ← StaticTF + 在线外参优化回调
│   ├── amcl_likelihood_field.cpp    ← 截断高斯 + 混合模型（z_hit/z_rand/z_max）
│   └── localization_lifecycle.cpp   ← configure()建图加载, activate()启动粒子滤波
├── config/
│   ├── sensor_sync_params.yaml      ← slop: 0.05s, queue_size: 10
│   └── amcl_params.yaml             ← max_particles: 5000, laser_model_type: "likelihood_field_prob"
└── launch/localization.launch.py    ← 组合启动入口
```

**关键 ROS 2 接口**：
- 订阅：`/scan` (sensor_msgs/LaserScan), `/imu/data` (sensor_msgs/Imu), `/odom` (nav_msgs/Odometry)
- 发布：`/amcl_pose` (geometry_msgs/PoseWithCovarianceStamped), `/particlecloud` (nav2_msgs/ParticleCloud)
- TF：`map → odom → base_link`

---

### module_2「分层导航栈的路径规划定制开发」

```
src/module_2_planning/
├── include/module_2_planning/
│   ├── hybrid_astar_planner.hpp     ← Nav2 GlobalPlanner 插件接口实现
│   ├── dynamic_obstacle_predictor.hpp ← 障碍物 CV 模型轨迹预测
│   ├── dwb_dynamic_critic.hpp       ← DWB Critic 插件：动态障碍物代价
│   ├── trajectory_optimizer.hpp     ← 独立节点：接收粗路径，发布优化轨迹
│   └── velocity_clipper.hpp         ← 独立节点：接收速度指令，施加约束裁剪
├── src/
│   ├── hybrid_astar_planner.cpp     ← Reeds-Shepp 运动基元 + 启发函数
│   ├── dynamic_obstacle_predictor.cpp ← 恒速/恒加速预测，发布预测占用栅格
│   ├── dwb_dynamic_critic.cpp       ← 时间展开代价：∑ P(collision|t) * discount^t
│   ├── trajectory_optimizer.cpp     ← CasADi NLP：min(curvature) s.t. obstacle clearance
│   └── velocity_clipper.cpp         ← 速度/加速度/转向角三级硬限幅
└── plugins/planner_plugins.xml      ← pluginlib 注册 HybridAStarPlanner
```

**关键 ROS 2 接口**：
- 动作服务器：`/compute_path_to_pose` (nav2_msgs/ComputePathToPose)
- 订阅：`/predicted_obstacles` (nav_msgs/OccupancyGrid), `/cmd_vel_raw` (geometry_msgs/Twist)
- 发布：`/plan` (nav_msgs/Path), `/cmd_vel` (geometry_msgs/Twist)

---

### module_3「语义增强的自主探索与建图闭环」

```
src/module_3_mapping/
├── include/module_3_mapping/
│   ├── frontier_explorer.hpp        ← 主动探索目标选择（信息增益最大化）
│   ├── yolov8_detector.hpp          ← ONNX Runtime 推理封装，ROS 2 话题输出
│   ├── semantic_map_annotator.hpp   ← 检测结果 3D 投影至语义栅格层
│   └── semantic_consistency.hpp    ← 多趟建图语义对齐（ICP + 标签投票）
├── src/
│   ├── frontier_explorer.cpp        ← BFS Frontier 提取 + 路径代价加权评分
│   ├── yolov8_detector.cpp          ← 图像预处理 → ONNX 推理 → NMS → 发布 BoundingBox2DArray
│   ├── semantic_map_annotator.cpp   ← 深度图 + 相机内参 → 3D bbox → 栅格语义层写入
│   └── semantic_consistency.cpp    ← 跨地图 ICP 对齐 + 语义标签一致性投票
└── models/yolov8n.onnx              ← 可替换为自定义训练权重
```

**关键 ROS 2 接口**：
- 订阅：`/map` (nav_msgs/OccupancyGrid), `/camera/image_raw` (sensor_msgs/Image), `/camera/depth/image_raw`
- 发布：`/explore/goal` (geometry_msgs/PoseStamped), `/semantic_map` (nav_msgs/OccupancyGrid), `/detections` (vision_msgs/Detection2DArray)
- 动作客户端：调用 Nav2 `NavigateToPose`

---

### module_4「动态障碍物感知与实时避障策略实现」

```
src/module_4_dynamic_avoidance/
├── include/module_4_dynamic_avoidance/
│   ├── euclidean_cluster.hpp        ← PCL 欧氏聚类 + 最小包围盒
│   ├── ekf_tracker.hpp              ← 6DoF EKF：[x,y,z,vx,vy,vz] 状态向量
│   ├── teb_constraint_plugin.hpp    ← TEB Constraint 插件：非线性速度包络
│   └── avoidance_pipeline.hpp      ← 感知→跟踪→预测→规划闭环协调器
├── src/
│   ├── euclidean_cluster.cpp        ← PCL EuclideanClusterExtraction + ConvexHull
│   ├── ekf_tracker.cpp              ← Q/R 矩阵自适应调整，多目标匹配（匈牙利算法）
│   ├── teb_constraint_plugin.cpp    ← 速度包络约束 g(v,ω) ≤ 0 + 转向角加速度 Δω/Δt 限幅
│   └── avoidance_pipeline.cpp      ← ROS 2 ComposableNode 组合，零拷贝 intra-process
```

**关键 ROS 2 接口**：
- 订阅：`/points` (sensor_msgs/PointCloud2), `/odom`
- 发布：`/tracked_objects` (derived_object_msgs/ObjectArray), `/dynamic_costmap_update` (map_msgs/OccupancyGridUpdate)

---

### module_5「多机器人协同导航与任务分配系统开发」

```
src/module_5_multi_robot/
├── include/module_5_multi_robot/
│   ├── cbba_task_allocator.hpp      ← CBBA：竞价向量 + 赢者列表 + 共识协议
│   ├── fleet_executor.hpp           ← BT 行为树驱动，状态机监控各机器人
│   ├── rstc_planner.hpp             ← RSTC：相对速度投影 + 紧急横向偏移
│   └── tf_chain_validator.hpp       ← 跨命名空间 TF 链路完整性 + 时延告警
├── src/
│   ├── cbba_task_allocator.cpp      ← 任务束竞价迭代，去中心化共识收敛
│   ├── fleet_executor.cpp           ← BehaviorTree.CPP v4 集成，任务下发/回收
│   ├── rstc_planner.cpp             ← 时空路径段持久化（SQLite），冲突检测窗口
│   └── tf_chain_validator.cpp       ← /tf_static + /tf 监听，链路图检查
├── msg/
│   ├── TaskBundle.msg               ← string task_id; geometry_msgs/Pose[] waypoints; int32 priority
│   └── RobotStatus.msg             ← string robot_id; int8 state; float32 battery; geometry_msgs/Pose pose
└── srv/AllocateTasks.srv            ← TaskBundle[] tasks → TaskBundle[] assignments
```

**关键 ROS 2 接口**：
- 话题（每机器人命名空间 `/robot_{id}/`）：`/cmd_vel`, `/odom`, `/amcl_pose`
- 服务：`/fleet/allocate_tasks` (AllocateTasks)
- 动作：`/robot_{id}/navigate_to_pose` (nav2_msgs/NavigateToPose)

---

### module_6「导航系统鲁棒性验证与形式化测试平台构建」

```
src/module_6_testing/
├── include/module_6_testing/
│   ├── scenario_runner.hpp          ← OpenSCENARIO .xosc 解析 + esmini 执行桥接
│   ├── fault_injector.hpp           ← ASIL-B 故障模型：传感器失效/数据篡改/节点崩溃
│   ├── realtime_stress_tester.hpp   ← 话题延迟/抖动统计，实时性违规计数
│   └── report_generator.hpp        ← TinyXML2 生成 IEEE 829 test_report.xml
├── src/
│   ├── scenario_runner.cpp          ← esmini C API 封装 + ROS 2 话题同步注入
│   ├── fault_injector.cpp           ← 概率性故障触发，持续时间控制，事后恢复
│   ├── realtime_stress_tester.cpp   ← /clock 订阅，滑动窗口 P99 延迟计算
│   └── report_generator.cpp        ← TestSuite/TestCase/Failure XML 节点构建
├── scenarios/                       ← OpenSCENARIO 2.0 场景文件（.xosc）
└── reports/test_report.xml          ← 自动生成，覆盖功能/性能/安全三维度
```

**关键测试维度**：
- 功能维度：路径规划可达性、定位精度（ATE/RTE）、任务完成率
- 性能维度：规划延迟 P99 < 100ms，控制循环 > 20Hz，建图更新 < 500ms
- 安全维度：ASIL-B 故障注入后安全停车，碰撞率 = 0

---

## 4. 主要类/函数接口定义

### 4.1 SensorSyncManager（module_1）

```cpp
// include/module_1_localization/sensor_sync_manager.hpp
class SensorSyncManager : public rclcpp::Node {
public:
  explicit SensorSyncManager(const rclcpp::NodeOptions & options);

private:
  // ApproximateTime 三路同步回调
  void syncCallback(
    const sensor_msgs::msg::LaserScan::ConstSharedPtr & scan,
    const sensor_msgs::msg::Imu::ConstSharedPtr & imu,
    const nav_msgs::msg::Odometry::ConstSharedPtr & odom);

  // 时间戳漂移补偿（硬件时钟偏差修正）
  rclcpp::Time compensateTimestamp(
    const rclcpp::Time & raw_stamp,
    const std::string & sensor_frame);

  using SyncPolicy = message_filters::sync_policies::ApproximateTime<
    sensor_msgs::msg::LaserScan,
    sensor_msgs::msg::Imu,
    nav_msgs::msg::Odometry>;

  message_filters::Synchronizer<SyncPolicy> sync_;
  rclcpp::Publisher<sensor_msgs::msg::LaserScan>::SharedPtr synced_scan_pub_;
};
```

### 4.2 AmclLikelihoodField（module_1）

```cpp
// include/module_1_localization/amcl_likelihood_field.hpp
class AmclLikelihoodField : public nav2_amcl::SensorModel {
public:
  // 计算单粒子的传感器似然（重写基类纯虚函数）
  double sensorLikelihood(
    const nav2_amcl::Particle & particle,
    const sensor_msgs::msg::LaserScan & scan,
    const nav_msgs::msg::OccupancyGrid & map) override;

private:
  // 截断高斯距离场查询（非理想环境适配）
  double lookupLikelihood(float obstacle_dist, float z_hit, float sigma_hit) const;

  // 混合模型权重（z_hit + z_short + z_max + z_rand）
  struct ModelWeights { float z_hit, z_short, z_max, z_rand; };
  ModelWeights weights_;

  // 预计算距离变换场（occupancy grid → distance field）
  std::vector<float> distance_field_;
};
```

### 4.3 HybridAStarPlanner（module_2）

```cpp
// include/module_2_planning/hybrid_astar_planner.hpp
class HybridAStarPlanner : public nav2_core::GlobalPlanner {
public:
  // pluginlib 初始化
  void configure(
    const rclcpp_lifecycle::LifecycleNode::WeakPtr & parent,
    std::string name,
    std::shared_ptr<tf2_ros::Buffer> tf,
    std::shared_ptr<nav2_costmap_2d::Costmap2DROS> costmap_ros) override;

  // 路径规划主入口
  nav_msgs::msg::Path createPlan(
    const geometry_msgs::msg::PoseStamped & start,
    const geometry_msgs::msg::PoseStamped & goal) override;

private:
  struct Node {
    float x, y, theta;           // 连续状态空间位姿
    float g_cost, h_cost;        // 代价函数
    std::shared_ptr<Node> parent;
    int grid_idx;                 // 离散化索引（用于哈希去重）
  };

  // Reeds-Shepp 启发式（考虑非完整约束）
  float reedSheppHeuristic(const Node & from, const Node & goal) const;

  // 运动基元展开（前/后/左/右转组合）
  std::vector<Node> expandNode(const Node & current) const;

  float min_turning_radius_;     // 最小转弯半径（米）
  float step_size_;              // 搜索步长（米）
  int heading_resolution_;       // 方向离散化分辨率（度）
};
```

### 4.4 EKFTracker（module_4）

```cpp
// include/module_4_dynamic_avoidance/ekf_tracker.hpp
class EKFTracker : public rclcpp::Node {
public:
  explicit EKFTracker(const rclcpp::NodeOptions & options);

private:
  struct TrackState {
    Eigen::VectorXd x;   // 状态向量 [x, y, z, vx, vy, vz]
    Eigen::MatrixXd P;   // 协方差矩阵 6×6
    int id;
    rclcpp::Time last_update;
  };

  // EKF 预测步（恒速运动模型）
  void predict(TrackState & track, double dt);

  // EKF 更新步（观测：质心位置 [x, y, z]）
  void update(TrackState & track, const Eigen::Vector3d & measurement);

  // 匈牙利算法：聚类→轨迹关联
  std::vector<std::pair<int,int>> associateClustersToTracks(
    const std::vector<Eigen::Vector3d> & clusters,
    const std::vector<TrackState> & tracks);

  std::vector<TrackState> tracks_;
  int next_track_id_ = 0;
  Eigen::MatrixXd Q_, R_;        // 过程噪声 / 观测噪声协方差
};
```

### 4.5 CBBATaskAllocator（module_5）

```cpp
// include/module_5_multi_robot/cbba_task_allocator.hpp
class CBBATaskAllocator : public rclcpp::Node {
public:
  explicit CBBATaskAllocator(const rclcpp::NodeOptions & options);

  // 触发一轮 CBBA 竞价（服务回调）
  void allocateCallback(
    const std::shared_ptr<module_5_multi_robot::srv::AllocateTasks::Request> req,
    std::shared_ptr<module_5_multi_robot::srv::AllocateTasks::Response> res);

private:
  // 计算机器人 i 执行任务 j 的竞价值（路径代价倒数 × 优先级）
  double computeBid(int robot_id, int task_id) const;

  // CBBA 共识迭代（广播 + 更新赢者列表）
  bool consensusIteration(int max_iter = 50);

  std::vector<std::string> robot_ids_;
  std::map<int, std::vector<int>> winning_bids_;   // task_id → [winner_robot_id, bid_value]
  double discount_factor_;                           // 路径代价折扣因子 γ ∈ (0,1]
};
```

### 4.6 ScenarioRunner（module_6）

```cpp
// include/module_6_testing/scenario_runner.hpp
class ScenarioRunner : public rclcpp::Node {
public:
  explicit ScenarioRunner(const rclcpp::NodeOptions & options);

  // 加载并执行 OpenSCENARIO 场景文件
  bool loadScenario(const std::string & xosc_path);
  void runScenario();

  // 注册测试断言回调（在场景结束时触发）
  using AssertCallback = std::function<bool(const ScenarioResult &)>;
  void registerAssertion(const std::string & name, AssertCallback cb);

private:
  struct ScenarioResult {
    bool completed;
    double elapsed_time;
    int collision_count;
    double goal_reached_distance;
  };

  // esmini C API 句柄
  void * esmini_handle_ = nullptr;

  // 场景帧更新循环（10ms 步长）
  rclcpp::TimerBase::SharedPtr step_timer_;
  void stepScenario();

  std::vector<std::pair<std::string, AssertCallback>> assertions_;
};
```

### 4.7 Python 辅助接口示例

```python
# src/module_3_mapping/src/frontier_explorer_node.py
class FrontierExplorer(Node):
    def __init__(self) -> None:
        super().__init__("frontier_explorer")
        self._map_sub = self.create_subscription(
            OccupancyGrid, "/map", self._map_callback, qos_profile_sensor_data
        )
        self._goal_pub = self.create_publisher(PoseStamped, "/explore/goal", 10)
        self._nav_client = ActionClient(self, NavigateToPose, "navigate_to_pose")

    def _map_callback(self, msg: OccupancyGrid) -> None:
        frontiers = self._extract_frontiers(msg)
        best = self._score_frontiers(frontiers, msg)
        if best is not None:
            self._send_goal(best)

    def _extract_frontiers(self, grid: OccupancyGrid) -> list[tuple[int, int]]:
        """BFS 提取 free-unknown 边界单元格坐标列表."""
        ...

    def _score_frontiers(
        self, frontiers: list[tuple[int, int]], grid: OccupancyGrid
    ) -> geometry_msgs.msg.Pose | None:
        """信息增益 × 距离倒数加权，返回最优 Frontier 位姿."""
        ...
```

---

## 5. 运行方式

### 5.1 环境初始化

```bash
# 克隆仓库并初始化工作空间
git clone <repo_url> ros_nav_system && cd ros_nav_system
bash scripts/setup_workspace.sh     # 安装依赖 + rosdep install + colcon build

# 或使用 Docker（推荐隔离环境）
docker-compose -f docker/docker-compose.yml up --build
```

### 5.2 模块独立启动

```bash
# 加载 ROS 2 环境
source /opt/ros/humble/setup.bash
source install/setup.bash

# ── 模块1：定位 ──────────────────────────────────────────────────────
ros2 launch module_1_localization localization.launch.py \
  params_file:=src/module_1_localization/config/amcl_params.yaml

# ── 模块2：路径规划 ──────────────────────────────────────────────────
ros2 launch module_2_planning planning_stack.launch.py \
  use_hybrid_astar:=true \
  params_file:=src/module_2_planning/config/hybrid_astar_params.yaml

# ── 模块3：语义建图 ──────────────────────────────────────────────────
ros2 launch module_3_mapping semantic_mapping.launch.py \
  model_path:=src/module_3_mapping/models/yolov8n.onnx

# ── 模块4：动态避障 ──────────────────────────────────────────────────
ros2 launch module_4_dynamic_avoidance dynamic_avoidance.launch.py \
  params_file:=src/module_4_dynamic_avoidance/config/teb_params.yaml

# ── 模块5：多机协同 ──────────────────────────────────────────────────
# 先启动仿真
ros2 launch module_5_multi_robot multi_robot_sim.launch.py robot_count:=3
# 再启动舰队管理
ros2 launch module_5_multi_robot fleet_bringup.launch.py

# ── 模块6：测试平台 ──────────────────────────────────────────────────
ros2 launch module_6_testing test_platform.launch.py \
  scenario:=src/module_6_testing/scenarios/dynamic_obstacle.xosc
```

### 5.3 全系统仿真联调

```bash
# 一键启动仿真 + 全栈导航
bash scripts/run_simulation.sh --world warehouse --robots 1

# 多机场景
bash scripts/run_simulation.sh --world multi_robot_arena --robots 3
```

### 5.4 测试

```bash
# 全量测试（C++ GTest + Python pytest）
bash scripts/run_tests.sh

# 仅运行某模块测试
colcon test --packages-select module_1_localization
colcon test-result --verbose

# 单独运行 Python 测试
pytest src/module_3_mapping/test/ -v --tb=short

# 生成 IEEE 829 测试报告
ros2 run module_6_testing scenario_runner \
  --ros-args -p scenario_path:=scenarios/sensor_degradation.xosc
# 报告输出：src/module_6_testing/reports/test_report.xml
```

### 5.5 LifecycleNode 管理（模块1）

```bash
# 查看定位节点状态
ros2 lifecycle get /localization_node

# 状态转换
ros2 lifecycle set /localization_node configure
ros2 lifecycle set /localization_node activate
ros2 lifecycle set /localization_node deactivate   # 暂停，保留地图
ros2 lifecycle set /localization_node cleanup      # 释放资源
```

### 5.6 参数热更新示例

```bash
# 运行时调整 AMCL 粒子数
ros2 param set /amcl max_particles 8000

# 调整 Hybrid A* 步长
ros2 param set /hybrid_astar_planner step_size 0.3

# 查看所有可调参数
ros2 param list /hybrid_astar_planner
```

---

*本计划文档由架构设计阶段生成，代码实现时各接口签名可根据实际依赖版本微调。*
