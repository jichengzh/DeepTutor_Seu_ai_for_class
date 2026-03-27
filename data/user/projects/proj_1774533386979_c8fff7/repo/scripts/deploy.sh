#!/usr/bin/env bash
# 无人机智能感知与自主导航系统 — 一键部署脚本
# 环境: Ubuntu 22.04, Python 3.10, ROS2 Humble, Jetson Orin Nano / JetPack 5.1.2
set -e

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PYTHON=${PYTHON:-python3.10}
VENV_DIR="${PROJECT_DIR}/.venv"

echo "=== 无人机感知系统部署脚本 ==="
echo "项目目录: ${PROJECT_DIR}"

# ─── 步骤1: Python虚拟环境 ───
if [ ! -d "${VENV_DIR}" ]; then
    echo "[1/5] 创建Python虚拟环境..."
    ${PYTHON} -m venv "${VENV_DIR}"
fi
source "${VENV_DIR}/bin/activate"
echo "[1/5] 虚拟环境已激活: ${VENV_DIR}"

# ─── 步骤2: 安装Python依赖 ───
echo "[2/5] 安装Python依赖..."
pip install --upgrade pip --quiet
pip install -r "${PROJECT_DIR}/requirements.txt" --quiet
echo "[2/5] 依赖安装完成"

# ─── 步骤3: 验证模块可导入 ───
echo "[3/5] 验证模块可导入..."
cd "${PROJECT_DIR}"
for module in module1_hardware module2_training module3_perception \
              module4_collaborative module5_mapping module6_avoidance; do
    ${PYTHON} -c "import sys; sys.path.insert(0,'src'); import ${module}; print('  OK: ${module}')"
done
echo "[3/5] 所有模块验证通过"

# ─── 步骤4: 运行单元测试 ───
echo "[4/5] 运行单元测试..."
${PYTHON} -m pytest tests/test_modules.py -v --tb=short -q
echo "[4/5] 单元测试完成"

# ─── 步骤5: 代码规模自检 ───
echo "[5/5] 代码规模自检..."
PY_COUNT=$(find "${PROJECT_DIR}/src" "${PROJECT_DIR}/tests" -name '*.py' | wc -l)
TOTAL_LINES=$(cat $(find "${PROJECT_DIR}/src" "${PROJECT_DIR}/tests" -name '*.py') | wc -l)
echo "  Python文件数: ${PY_COUNT} (限制≤10)"
echo "  总代码行数:   ${TOTAL_LINES} (限制≤3000)"
if [ "${PY_COUNT}" -gt 10 ]; then
    echo "  警告: Python文件数超出限制!"
fi
if [ "${TOTAL_LINES}" -gt 3000 ]; then
    echo "  警告: 总代码行数超出限制!"
fi
echo "[5/5] 自检完成"

echo ""
echo "=== 部署成功 ==="
echo "运行示例:"
echo "  source ${VENV_DIR}/bin/activate"
echo "  python src/module1_hardware.py   # 硬件集成验证"
echo "  python src/module6_avoidance.py  # 安全飞控验证"
echo "  pytest tests/ -v                 # 完整测试套件"
