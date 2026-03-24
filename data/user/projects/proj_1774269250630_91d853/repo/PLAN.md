# ROS机器人自主导航系统开发与实操实习 — 架构计划

> **运行环境**：Ubuntu 22.04 + ROS2 Humble（主线）/ Ubuntu 20.04 + ROS1 Noetic（兼容层）
> **技术栈**：C++17, Python 3.10, ROS2 Humble, ROS1 Noetic, CMake, colcon

---

## 1. 项目目录结构

```
ros_nav_internship/
├── README.md
├── PLAN.md                          # 本文件
├── .gitignore
├── docker/
│   ├── Dockerfile.humble            # Ubuntu 22.04 + ROS2 Humble 镜像
│   ├── Dockerfile.noetic            # Ubuntu 20.04 + ROS1 Noetic 镜像
│   └── docker-compose.yml           # 多容器编排（仿真 + 机器人 + 测试）
│
├── scripts/
│   ├── setup_env.sh                 # 一键配置开发环境（依赖安装、工作空间初始化）
│   ├── build_all.sh                 # 全量构建脚本
│   └── run_sim.sh                   # 启动 Gazebo 仿真快捷入口
│
├── src/                             # ROS2 工作空间 src 目录
│   │
│   ├── module_1_localization/       # ── 模块1：多传感器融合定位 ──────────────────
│   │   ├── CMakeLists.txt
│   │   ├── package.xml
│   │   ├── include/
│   │   │   └── module_1_localization/
│   │   │       ├── sensor_synchronizer.hpp     # 激光/IMU/编码器时间同步器接口
│   │   │       ├── tf_aligner.hpp              # 多坐标系对齐工具类接口
│   │   │       ├── amcl_likelihood_field.hpp   # 重写的似然域模型接口
│   │   │       └── localization_lifecycle.hpp  # LifecycleNode 封装接口
│   │   ├── src/
│   │   │   ├── sensor_synchronizer.cpp         # 基于 message_filters 的精确时间同步
│   │   │   ├── tf_aligner.cpp                  # 静态/动态 TF 广播与坐标变换对齐
│   │   │   ├── amcl_likelihood_field.cpp       # 适配非理想环境的 LFM 重写实现
│   │   │   └── localization_lifecycle.cpp      # LifecycleNode 状态机（configure/activate/…）
│   │   ├── launch/
│   │   │   └── localization.launch.py          # 定位节点组启动文件
│   │   ├── config/
│   │   │   ├── amcl_params.yaml                # AMCL 粒子滤波器参数
│   │   │   └── sensor_sync_params.yaml         # 时间同步容差与队列配置
│   │   └── test/
│   │       ├── test_sensor_sync.cpp            # 时间同步单元测试
│   │       └── test_tf_aligner.cpp             # 坐标系对齐单元测试
│   │
│   ├── module_2_planning/           # ── 模块2：分层导航栈路径规划 ───────────────
│   │   ├── CMakeLists.txt
│   │   ├── package.xml
│   │   ├── include/
│   │   │   └── module_2_planning/
│   │   │       ├── hybrid_astar_planner.hpp    # 改进型 Hybrid A* 全局规划器插件接口
│   │   │       ├── dynamic_obstacle_predictor.hpp  # 动态障碍物预测模块接口
│   │   │       ├── dwb_predictor_plugin.hpp    # DWB 控制器预测插件接口
│   │   │       ├── path_searcher.hpp           # 路径搜索组件接口（解耦层）
│   │   │       ├── trajectory_optimizer.hpp    # 轨迹优化组件接口（解耦层）
│   │   │       └── velocity_clipper.hpp        # 速度裁剪组件接口（解耦层）
│   │   ├── src/
│   │   │   ├── hybrid_astar_planner.cpp        # Nav2 GlobalPlanner 插件实现
│   │   │   ├── dynamic_obstacle_predictor.cpp  # 基于卡尔曼滤波的短时轨迹预测
│   │   │   ├── dwb_predictor_plugin.cpp        # 将预测结果注入 DWB 评分函数
│   │   │   ├── path_searcher.cpp               # 独立 ROS2 组件：搜索结果发布到话题
│   │   │   ├── trajectory_optimizer.cpp        # 独立 ROS2 组件：接收路径输出平滑轨迹
│   │   │   └── velocity_clipper.cpp            # 独立 ROS2 组件：最终速度指令裁剪
│   │   ├── launch/
│   │   │   └── planning.launch.py              # 规划栈启动文件
│   │   ├── config/
│   │   │   ├── hybrid_astar_params.yaml        # Hybrid A* 搜索参数（分辨率/转向约束）
│   │   │   └── dwb_params.yaml                 # DWB 控制器与预测插件参数
│   │   └── test/
│   │       ├── test_hybrid_astar.cpp           # 路径规划单元测试（含非完整约束验证）
│   │       └── test_trajectory_opt.cpp         # 轨迹优化输出平滑度测试
│   │
│   ├── module_3_mapping/            # ── 模块3：语义增强建图与探索 ───────────────
│   │   ├── CMakeLists.txt
│   │   ├── package.xml
│   │   ├── include/
│   │   │   └── module_3_mapping/
│   │   │       ├── frontier_explorer.hpp       # Frontier 主动探索控制器接口
│   │   │       ├── semantic_annotator.hpp      # YOLOv8 语义标注与地图注册接口
│   │   │       ├── semantic_map_merger.hpp     # 多趟建图语义一致性合并接口
│   │   │       └── map_metadata_store.hpp      # 语义元数据持久化存储接口
│   │   ├── src/
│   │   │   ├── frontier_explorer.cpp           # 基于占据栅格的 Frontier 检测与选择
│   │   │   └── semantic_map_merger.cpp         # 语义标签冲突消解与地图合并算法
│   │   ├── scripts/
│   │   │   ├── semantic_annotator.py           # YOLOv8 推理 + 语义坐标反向投影
│   │   │   ├── map_metadata_store.py           # SQLite/JSON 语义元数据读写工具
│   │   │   └── slam_toolbox_bridge.py          # 将语义注解写入 slam_toolbox 地图元数据
│   │   ├── models/
│   │   │   └── yolov8n.pt                      # YOLOv8 预训练权重（占位，实际通过脚本下载）
│   │   ├── launch/
│   │   │   ├── exploration.launch.py           # Frontier 探索启动
│   │   │   └── semantic_mapping.launch.py      # 语义建图全栈启动
│   │   ├── config/
│   │   │   ├── frontier_params.yaml            # Frontier 评估权重与探索半径
│   │   │   └── yolov8_params.yaml              # 检测置信度阈值与类别过滤
│   │   └── test/
│   │       ├── test_frontier_detection.cpp     # Frontier 检测覆盖率单元测试
│   │       └── test_semantic_merger.py         # 语义一致性合并 Python 单元测试
│   │
│   ├── module_4_obstacle/           # ── 模块4：动态障碍物感知与避障 ────────────
│   │   ├── CMakeLists.txt
│   │   ├── package.xml
│   │   ├── include/
│   │   │   └── module_4_obstacle/
│   │   │       ├── euclidean_cluster.hpp       # 欧氏聚类点云分割接口
│   │   │       ├── obstacle_ekf.hpp            # 6DoF EKF 障碍物状态估计接口
│   │   │       ├── obstacle_layer_ext.hpp      # 扩展 ObstacleLayer（碰撞锥判定）接口
│   │   │       └── teb_nonlinear_plugin.hpp    # TEB 非线性速度/转向约束插件接口
│   │   ├── src/
│   │   │   ├── euclidean_cluster.cpp           # PCL 欧氏聚类 + 边界框提取
│   │   │   ├── obstacle_ekf.cpp                # 6DoF EKF：位置/速度/加速度联合估计
│   │   │   ├── obstacle_layer_ext.cpp          # 重写 updateBounds，支持碰撞锥计算
│   │   │   └── teb_nonlinear_plugin.cpp        # 硬限幅约束：线速度/角加速度上限注入
│   │   ├── launch/
│   │   │   └── obstacle_avoidance.launch.py    # 障碍物感知闭环启动文件
│   │   ├── config/
│   │   │   ├── ekf_params.yaml                 # EKF 过程/观测噪声协方差配置
│   │   │   ├── cluster_params.yaml             # 聚类半径、最小点数参数
│   │   │   └── teb_nonlinear_params.yaml       # 速度约束上下限与加速度硬限幅值
│   │   └── test/
│   │       ├── test_ekf_tracking.cpp           # EKF 状态估计精度测试
│   │       └── test_collision_cone.cpp         # 碰撞锥安全判定测试（含边界条件）
│   │
│   ├── module_5_multi_robot/        # ── 模块5：多机器人协同导航 ─────────────────
│   │   ├── CMakeLists.txt
│   │   ├── package.xml
│   │   ├── include/
│   │   │   └── module_5_multi_robot/
│   │   │       ├── cbba_allocator.hpp          # CBBA 任务分配器接口
│   │   │       ├── fleet_executor.hpp          # 轻量级舰队执行器接口
│   │   │       ├── cross_robot_tf.hpp          # 跨机器人 TF 链路动态维护接口
│   │   │       ├── rstc_preplanner.hpp         # RSTC 预规划避让接口
│   │   │       └── conflict_resolver.hpp       # 二级冲突消解（侧向偏移）接口
│   │   ├── src/
│   │   │   ├── cbba_allocator.cpp              # 共识拍卖算法：任务竞标与分配
│   │   │   ├── fleet_executor.cpp              # 任务队列管理与机器人指令分发
│   │   │   ├── cross_robot_tf.cpp              # 动态 TF 广播：robot_i/base_link → map
│   │   │   ├── rstc_preplanner.cpp             # 时空预规划：路径段时间窗分配
│   │   │   └── conflict_resolver.cpp           # 碰撞预测 + 侧向偏移指令生成
│   │   ├── scripts/
│   │   │   └── task_logger.py                  # 任务状态与冲突事件持久化（SQLite）
│   │   ├── launch/
│   │   │   ├── multi_robot_sim.launch.py       # 多机器人仿真启动（Gazebo 多实例）
│   │   │   └── fleet_manager.launch.py         # 舰队管理节点启动
│   │   ├── config/
│   │   │   ├── cbba_params.yaml                # 任务价值函数与竞标衰减参数
│   │   │   └── conflict_params.yaml            # 冲突检测距离阈值与偏移量配置
│   │   └── test/
│   │       ├── test_cbba_allocation.cpp        # CBBA 收敛性与最优性测试
│   │       └── test_conflict_resolution.cpp    # 冲突消解场景回归测试
│   │
│   └── module_6_testing/            # ── 模块6：鲁棒性验证与测试平台 ────────────
│       ├── CMakeLists.txt
│       ├── package.xml
│       ├── include/
│       │   └── module_6_testing/
│       │       ├── scenario_runner.hpp         # OpenSCENARIO 场景执行器接口
│       │       ├── rt_stress_tester.hpp        # 实时性压力测试接口
│       │       ├── safety_boundary_checker.hpp # ISO 26262 ASIL-B 安全边界检查接口
│       │       └── report_generator.hpp        # IEEE 829 XML 报告生成器接口
│       ├── src/
│       │   ├── scenario_runner.cpp             # 解析 .xosc 文件，驱动 Gazebo/CARLA 场景
│       │   ├── rt_stress_tester.cpp            # 高频话题注入 + 延迟/抖动统计
│       │   └── safety_boundary_checker.cpp     # ASIL-B：最小安全距离/速度上限断言
│       ├── scripts/
│       │   ├── report_generator.py             # 聚合测试结果，生成 test_report.xml
│       │   ├── perf_analyzer.py                # 回调延迟、CPU/内存占用分析
│       │   └── run_all_tests.sh                # 全量测试流水线入口脚本
│       ├── scenarios/
│       │   ├── basic_navigation.xosc           # 基础点到点导航场景
│       │   ├── dynamic_obstacle_avoidance.xosc # 动态障碍物场景
│       │   ├── multi_robot_coordination.xosc   # 多机器人协同场景
│       │   └── emergency_stop.xosc             # 紧急停车安全场景
│       ├── launch/
│       │   └── test_platform.launch.py         # 测试平台全栈启动
│       ├── config/
│       │   ├── test_params.yaml                # 测试超时、重试次数、容忍阈值
│       │   └── asil_thresholds.yaml            # ASIL-B 安全边界数值配置
│       └── results/
│           └── .gitkeep                        # 测试报告输出目录（不纳入版本控制）
│
└── docs/
    ├── architecture_overview.md     # 系统架构总览
    ├── module_interfaces.md         # 模块间接口约定
    └── diagrams/
        ├── system_architecture.drawio   # 系统架构图源文件
        └── data_flow.drawio             # 数据流图源文件
```

---

## 2. 技术选型说明

| 层次 | 选型 | 理由 |
|------|------|------|
| **中间件** | ROS2 Humble (主) / ROS1 Noetic (兼容) | Humble 为当前 LTS 版本，Noetic 用于存量硬件兼容 |
| **语言** | C++17（性能关键路径）/ Python 3.10（算法原型与脚本） | C++ 满足实时性要求；Python 加速 YOLOv8、日志、报告等模块开发 |
| **构建系统** | colcon + CMake 3.22 | ROS2 官方构建工具，支持并行构建与测试隔离 |
| **定位** | Nav2 AMCL（重写 LFM）+ robot_localization EKF | AMCL 成熟可靠；robot_localization 提供标准多传感器融合框架 |
| **规划** | Hybrid A*（替换 NavFn）+ DWB 控制器 | Hybrid A* 天然支持非完整约束；DWB 插件架构易于扩展预测功能 |
| **建图** | slam_toolbox（在线/离线 SLAM）+ YOLOv8（语义） | slam_toolbox 支持大规模地图与闭环；YOLOv8n 推理速度满足在线标注需求 |
| **避障** | PCL 欧氏聚类 + 6DoF EKF + TEB（改进） | PCL 点云处理成熟；TEB 支持非线性约束扩展 |
| **多机协同** | CBBA（共识拍卖）+ 自研 fleet_executor | CBBA 去中心化友好，收敛性有理论保证 |
| **仿真** | Gazebo Ignition + CARLA（可选）| Ignition 与 ROS2 原生集成；CARLA 支持 OpenSCENARIO |
| **测试** | GTest（C++）+ pytest（Python）+ OpenSCENARIO | 覆盖单元/集成/场景三个层次；OpenSCENARIO 为 ISO 标准场景描述格式 |
| **数据持久化** | SQLite（任务日志）+ YAML（参数配置）| 轻量、无服务器依赖，便于嵌入式部署 |
| **报告** | IEEE 829 XML（JUnit 兼容格式）| 与 CI/CD 系统（Jenkins/GitHub Actions）直接对接 |

---

## 3. 模块与代码文件对应关系

### module_1 — 基于AMCL的多传感器融合定位实践

| 文件 | 职责 |
|------|------|
| `src/sensor_synchronizer.cpp` | 使用 `message_filters::TimeSynchronizer` 实现激光雷达/IMU/编码器精确时间对齐 |
| `src/tf_aligner.cpp` | 广播 `base_link→laser`、`base_link→imu` 静态变换，运行时动态校正外参偏差 |
| `src/amcl_likelihood_field.cpp` | 重写 `LikelihoodFieldModel::sensorUpdate()`，引入自适应噪声模型适配走廊、玻璃等非理想环境 |
| `src/localization_lifecycle.cpp` | 实现 `nav2_util::LifecycleNode` 的 `on_configure/on_activate/on_deactivate/on_cleanup` 回调 |
| `config/amcl_params.yaml` | 粒子数、运动模型参数、激光模型参数 |
| `launch/localization.launch.py` | 组合启动 AMCL、robot_localization、TF 广播节点 |

### module_2 — 分层导航栈的路径规划定制开发

| 文件 | 职责 |
|------|------|
| `src/hybrid_astar_planner.cpp` | 实现 `nav2_core::GlobalPlanner` 插件，使用分析型扩展 + RS 曲线后处理 |
| `src/dynamic_obstacle_predictor.cpp` | 订阅障碍物追踪话题，输出未来 N 帧预测占据格 |
| `src/dwb_predictor_plugin.cpp` | 实现 `dwb_core::TrajectoryCritic`，将预测占据格纳入轨迹评分 |
| `src/path_searcher.cpp` | 独立组件，发布 `/raw_path`（未平滑路径） |
| `src/trajectory_optimizer.cpp` | 独立组件，订阅 `/raw_path`，发布 `/optimized_path`（B样条平滑） |
| `src/velocity_clipper.cpp` | 独立组件，订阅 `/cmd_vel_raw`，发布 `/cmd_vel`（速度裁剪后） |
| `config/hybrid_astar_params.yaml` | 网格分辨率、转向半径、Dubins/RS 切换阈值 |

### module_3 — 语义增强的自主探索与建图闭环

| 文件 | 职责 |
|------|------|
| `src/frontier_explorer.cpp` | 基于 WFD 算法检测 Frontier，按信息增益排序，发布导航目标点 |
| `src/semantic_map_merger.cpp` | 跨趟语义标签 IOU 匹配，消解矛盾标签，输出一致性语义地图 |
| `scripts/semantic_annotator.py` | 订阅相机图像，YOLOv8 推理，将检测框反投影至世界坐标，写入语义话题 |
| `scripts/slam_toolbox_bridge.py` | 调用 slam_toolbox `SerializePoseGraph` 服务，将语义坐标写入地图元数据 |
| `scripts/map_metadata_store.py` | SQLite CRUD：语义对象 ID、类别、位置、置信度的读写接口 |

### module_4 — 动态障碍物感知与实时避障策略实现

| 文件 | 职责 |
|------|------|
| `src/euclidean_cluster.cpp` | PCL `EuclideanClusterExtraction`，输出带 ID 的障碍物边界框话题 |
| `src/obstacle_ekf.cpp` | 6DoF EKF：状态向量 `[x,y,z,vx,vy,vz]`，融合聚类观测，输出平滑轨迹 |
| `src/obstacle_layer_ext.cpp` | 重写 `ObstacleLayer::updateBounds()`，用碰撞锥半径替代固定膨胀半径 |
| `src/teb_nonlinear_plugin.cpp` | 实现 `teb_local_planner::TebConstraintInterface`，注入 `v_max(θ)` 非线性上限与 `α̈_max` 硬限幅 |
| `config/ekf_params.yaml` | 过程噪声 Q、观测噪声 R、初始协方差 P₀ |
| `config/teb_nonlinear_params.yaml` | 速度-转角耦合函数参数、加速度上限 |

### module_5 — 多机器人协同导航与任务分配系统开发

| 文件 | 职责 |
|------|------|
| `src/cbba_allocator.cpp` | CBBA 主循环：竞标阶段（本地价值最大化）+ 共识阶段（广播同步） |
| `src/fleet_executor.cpp` | 维护每台机器人任务队列，通过 action client 下发 `NavigateToPose` 目标 |
| `src/cross_robot_tf.cpp` | 订阅各机器人 `odom` 话题，维护 `map→robot_i/base_link` 动态 TF 链路 |
| `src/rstc_preplanner.cpp` | 时空路段预规划：为每段路径分配时间窗，避免空间-时间冲突 |
| `src/conflict_resolver.cpp` | 预测碰撞时间 TTC，触发侧向偏移量计算，发布优先级偏移指令 |
| `scripts/task_logger.py` | SQLite 记录任务 ID、分配机器人、开始/完成时间戳、冲突事件 |

### module_6 — 导航系统鲁棒性验证与形式化测试平台构建

| 文件 | 职责 |
|------|------|
| `src/scenario_runner.cpp` | 解析 `.xosc` 文件，通过 Gazebo/CARLA ROS bridge 驱动场景，收集 pass/fail 断言 |
| `src/rt_stress_tester.cpp` | 以 1000 Hz 注入合成传感器数据，统计回调延迟分布（P50/P95/P99） |
| `src/safety_boundary_checker.cpp` | 运行时断言：`d_obstacle > d_min_asil`，`v < v_max_asil`，违反则记录 ASIL-B 事件 |
| `scripts/report_generator.py` | 聚合 GTest XML + pytest XML + 场景结果，输出 IEEE 829 `test_report.xml` |
| `scripts/perf_analyzer.py` | 分析 `ros2 topic hz/delay` 数据，绘制延迟热图，输出性能摘要 |
| `scenarios/*.xosc` | OpenSCENARIO 1.2 格式场景定义文件 |

---

## 4. 主要类/函数接口定义

### module_1 核心接口

```cpp
// sensor_synchronizer.hpp
class SensorSynchronizer : public rclcpp::Node {
public:
  explicit SensorSynchronizer(const rclcpp::NodeOptions& options);
private:
  // 精确时间策略同步（容差 ≤ 10ms）
  void syncCallback(
    const sensor_msgs::msg::LaserScan::ConstSharedPtr& scan,
    const sensor_msgs::msg::Imu::ConstSharedPtr& imu,
    const nav_msgs::msg::Odometry::ConstSharedPtr& odom);

  using SyncPolicy = message_filters::sync_policies::ApproximateTime<
    sensor_msgs::msg::LaserScan,
    sensor_msgs::msg::Imu,
    nav_msgs::msg::Odometry>;
  std::shared_ptr<message_filters::Synchronizer<SyncPolicy>> sync_;
};

// amcl_likelihood_field.hpp
class AdaptiveLikelihoodFieldModel {
public:
  // 构建自适应似然场（融合反射强度与几何距离）
  void buildField(const nav_msgs::msg::OccupancyGrid& map,
                  const LFMConfig& config);
  // 非理想环境下的观测概率计算
  double computeObservationProb(
    const sensor_msgs::msg::LaserScan& scan,
    const geometry_msgs::msg::Pose& hypothesis) const;

private:
  // 动态噪声模型：根据局部地图质量调整 σ_hit
  double adaptiveSigmaHit(const geometry_msgs::msg::Point& p) const;
};

// localization_lifecycle.hpp
class LocalizationLifecycleNode : public nav2_util::LifecycleNode {
public:
  explicit LocalizationLifecycleNode(const rclcpp::NodeOptions& options);
  nav2_util::CallbackReturn on_configure(const rclcpp_lifecycle::State&) override;
  nav2_util::CallbackReturn on_activate(const rclcpp_lifecycle::State&) override;
  nav2_util::CallbackReturn on_deactivate(const rclcpp_lifecycle::State&) override;
  nav2_util::CallbackReturn on_cleanup(const rclcpp_lifecycle::State&) override;
  nav2_util::CallbackReturn on_shutdown(const rclcpp_lifecycle::State&) override;
};
```

### module_2 核心接口

```cpp
// hybrid_astar_planner.hpp
class HybridAStarPlanner : public nav2_core::GlobalPlanner {
public:
  void configure(const rclcpp_lifecycle::LifecycleNode::WeakPtr& parent,
                 std::string name,
                 std::shared_ptr<tf2_ros::Buffer> tf,
                 std::shared_ptr<nav2_costmap_2d::Costmap2DROS> costmap_ros) override;
  nav_msgs::msg::Path createPlan(
    const geometry_msgs::msg::PoseStamped& start,
    const geometry_msgs::msg::PoseStamped& goal) override;

private:
  // 分析型扩展：6 种转向基元 + RS 曲线后处理
  std::vector<Node3D> expand(const Node3D& current);
  // 启发值 = holonomic_cost + dubins_shot_cost
  double heuristic(const Node3D& node, const Node3D& goal) const;
};

// dynamic_obstacle_predictor.hpp
class DynamicObstaclePredictor : public rclcpp::Node {
public:
  // 输出：未来 horizon_steps 帧的预测占据格列表
  std::vector<PredictedOccupancy> predict(
    const visualization_msgs::msg::MarkerArray& tracked_obstacles,
    int horizon_steps, double dt) const;
};

// velocity_clipper.hpp
class VelocityClipper : public rclcpp::Node {
public:
  // 裁剪规则：v ≤ v_max_linear，|ω| ≤ ω_max，|Δv/Δt| ≤ a_max
  geometry_msgs::msg::Twist clip(
    const geometry_msgs::msg::Twist& raw_cmd,
    const VelocityLimits& limits) const;
};
```

### module_3 核心接口

```cpp
// frontier_explorer.hpp
class FrontierExplorer : public rclcpp::Node {
public:
  // 返回按信息增益降序排列的 Frontier 列表
  std::vector<Frontier> detectFrontiers(
    const nav_msgs::msg::OccupancyGrid& map) const;
  // 选择最优 Frontier 并发布导航目标
  void selectAndPublishGoal(const std::vector<Frontier>& frontiers);

private:
  // WFD（Wave Front Detector）算法实现
  std::vector<geometry_msgs::msg::Point> wfd(
    const nav_msgs::msg::OccupancyGrid& map,
    const geometry_msgs::msg::Point& robot_pos) const;
};

// semantic_map_merger.hpp
class SemanticMapMerger : public rclcpp::Node {
public:
  // 合并两趟建图的语义标签，IOU > threshold 则合并，否则保留两者
  SemanticMap merge(const SemanticMap& map_a,
                    const SemanticMap& map_b,
                    double iou_threshold = 0.5) const;
  // 冲突消解：相同位置不同标签时采用置信度加权投票
  SemanticLabel resolveConflict(
    const std::vector<SemanticLabel>& candidates) const;
};
```

### module_4 核心接口

```cpp
// obstacle_ekf.hpp
class ObstacleEKF {
public:
  // 状态向量 x = [x, y, z, vx, vy, vz]^T
  void predict(double dt);
  void update(const Eigen::Vector3d& measurement);
  Eigen::VectorXd getState() const;
  Eigen::MatrixXd getCovariance() const;

private:
  Eigen::VectorXd x_;    // 状态均值（6×1）
  Eigen::MatrixXd P_;    // 状态协方差（6×6）
  Eigen::MatrixXd Q_;    // 过程噪声（6×6）
  Eigen::MatrixXd R_;    // 观测噪声（3×3）
  Eigen::MatrixXd F_;    // 状态转移矩阵（6×6）
  Eigen::MatrixXd H_;    // 观测矩阵（3×6）
};

// obstacle_layer_ext.hpp
class ObstacleLayerExt : public nav2_costmap_2d::ObstacleLayer {
public:
  // 重写：用碰撞锥半径 r_cone(v, d) 替代固定膨胀半径
  void updateBounds(double robot_x, double robot_y, double robot_yaw,
                    double* min_x, double* min_y,
                    double* max_x, double* max_y) override;

private:
  // 碰撞锥半径：r = v_rel * t_react + r_robot
  double computeConeRadius(double v_relative, double reaction_time) const;
};

// teb_nonlinear_plugin.hpp
class TebNonlinearConstraints : public teb_local_planner::TebConstraintInterface {
public:
  // 非线性速度上限：v_max(θ) = v_max_straight * cos(θ/2)
  double maxLinearVelocity(double steering_angle) const override;
  // 角加速度硬限幅：|α̈| ≤ alpha_ddot_max
  double maxAngularAcceleration() const override;
};
```

### module_5 核心接口

```cpp
// cbba_allocator.hpp
class CBBAAllocator : public rclcpp::Node {
public:
  // 为 num_robots 台机器人分配 tasks，返回分配方案
  TaskAssignment allocate(const std::vector<Task>& tasks,
                          const std::vector<RobotState>& robots);

private:
  // 竞标阶段：每台机器人独立计算本地最优任务集合
  void biddingPhase(int robot_id);
  // 共识阶段：广播竞标向量，消解分配冲突
  void consensusPhase();
  // 价值函数：考虑距离、优先级、电量余量
  double taskValue(const Task& task, const RobotState& robot) const;
};

// conflict_resolver.hpp
class ConflictResolver : public rclcpp::Node {
public:
  // 预测碰撞时间（TTC），触发避让
  std::optional<double> computeTTC(
    const RobotState& robot_a,
    const RobotState& robot_b) const;
  // 生成侧向偏移指令（右侧优先规则）
  geometry_msgs::msg::PoseStamped computeLateralOffset(
    const RobotState& robot,
    double offset_distance) const;
};
```

### module_6 核心接口

```cpp
// scenario_runner.hpp
class ScenarioRunner : public rclcpp::Node {
public:
  // 加载并执行 OpenSCENARIO 文件，返回测试结果
  ScenarioResult runScenario(const std::string& xosc_path);
  // 批量执行目录下所有场景
  std::vector<ScenarioResult> runAll(const std::string& scenarios_dir);
};

// rt_stress_tester.hpp
class RTStressTester : public rclcpp::Node {
public:
  // 以指定频率注入合成数据，持续 duration 秒
  void runStressTest(double inject_hz, double duration_s);
  // 获取延迟统计（单位：ms）
  LatencyStats getStats() const;  // p50, p95, p99, max

private:
  std::vector<double> latency_samples_;
};

// safety_boundary_checker.hpp
class SafetyBoundaryChecker : public rclcpp::Node {
public:
  // ASIL-B 断言：违反则记录事件，可配置为触发紧急停止
  void checkMinDistance(double actual_dist, double min_dist_asil);
  void checkMaxVelocity(double actual_vel, double max_vel_asil);
  bool hasAsilViolation() const;
  std::vector<AsilEvent> getViolationLog() const;
};
```

```python
# scripts/report_generator.py
class TestReportGenerator:
    def aggregate_results(self, gtest_xml: str, pytest_xml: str,
                          scenario_results: list[dict]) -> dict:
        """聚合所有测试结果为统一数据结构"""

    def generate_ieee829_xml(self, aggregated: dict,
                              output_path: str) -> None:
        """生成符合 IEEE 829 标准的 test_report.xml"""

    def compute_coverage_metrics(self, results: dict) -> dict:
        """计算功能/性能/安全三维度覆盖率"""
```

---

## 5. 运行方式

### 环境准备

```bash
# 克隆仓库并初始化工作空间
git clone <repo_url> ros_nav_internship
cd ros_nav_internship
bash scripts/setup_env.sh          # 安装依赖、配置 rosdep

# 构建全部模块
cd ~/ros2_ws
colcon build --symlink-install --packages-select \
  module_1_localization \
  module_2_planning \
  module_3_mapping \
  module_4_obstacle \
  module_5_multi_robot \
  module_6_testing
source install/setup.bash
```

### 模块1 — 定位

```bash
ros2 launch module_1_localization localization.launch.py \
  use_sim_time:=true \
  params_file:=src/module_1_localization/config/amcl_params.yaml
```

### 模块2 — 路径规划

```bash
ros2 launch module_2_planning planning.launch.py \
  use_sim_time:=true \
  planner_plugin:=HybridAStarPlanner \
  params_file:=src/module_2_planning/config/hybrid_astar_params.yaml
```

### 模块3 — 语义建图与探索

```bash
# 启动 SLAM + Frontier 探索
ros2 launch module_3_mapping exploration.launch.py use_sim_time:=true

# 同步启动语义标注（独立终端）
ros2 launch module_3_mapping semantic_mapping.launch.py \
  model_path:=src/module_3_mapping/models/yolov8n.pt
```

### 模块4 — 动态避障

```bash
ros2 launch module_4_obstacle obstacle_avoidance.launch.py \
  use_sim_time:=true \
  params_file:=src/module_4_obstacle/config/ekf_params.yaml
```

### 模块5 — 多机器人协同

```bash
# 启动多机器人仿真（Gazebo 3台机器人）
ros2 launch module_5_multi_robot multi_robot_sim.launch.py robot_count:=3

# 启动舰队管理（独立终端）
ros2 launch module_5_multi_robot fleet_manager.launch.py \
  params_file:=src/module_5_multi_robot/config/cbba_params.yaml
```

### 模块6 — 测试平台

```bash
# 运行全量测试流水线
bash src/module_6_testing/scripts/run_all_tests.sh

# 或分步执行：
# 1. 场景化测试
ros2 launch module_6_testing test_platform.launch.py \
  scenarios_dir:=src/module_6_testing/scenarios/

# 2. 实时性压力测试
ros2 run module_6_testing rt_stress_tester \
  --ros-args -p inject_hz:=1000.0 -p duration_s:=60.0

# 3. 生成 IEEE 829 报告
python3 src/module_6_testing/scripts/report_generator.py \
  --gtest-xml results/gtest_results.xml \
  --pytest-xml results/pytest_results.xml \
  --scenario-dir results/scenarios/ \
  --output results/test_report.xml
```

### Docker 一键启动（推荐）

```bash
# ROS2 Humble 完整仿真环境
docker-compose -f docker/docker-compose.yml up humble_sim

# ROS1 Noetic 兼容环境
docker-compose -f docker/docker-compose.yml up noetic_compat
```

---

## 附：模块依赖关系

```
module_1_localization
        ↓
module_2_planning ←── module_4_obstacle
        ↓                    ↓
module_3_mapping      module_5_multi_robot
        ↓                    ↓
        └──────┬─────────────┘
               ↓
      module_6_testing（依赖全部模块）
```

> **说明**：箭头表示运行时数据依赖，module_6 在测试时需要其他所有模块处于激活状态。各模块可独立构建与单元测试，集成测试需按上图顺序启动。
