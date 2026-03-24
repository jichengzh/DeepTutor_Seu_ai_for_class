# Phase 5 — Python 限制 + 难度分级功能实现指南

## 背景

在现有 Project Creator 代码生成流程中新增两项约束：
1. **语言限制**：仅允许 Python（.py）+ Shell 脚本（.sh），禁止 C++/Java 等编译型语言
2. **难度分级**：用户在 Step 1 选择难度，控制生成的文件数量与总行数

| 难度 | py 文件上限 | 代码行数上限 |
|------|------------|------------|
| 低   | 10         | 3,000      |
| 中   | 25         | 7,000      |
| 高   | 40         | 10,000     |

---

## 实现计划（开始前阅读）

涉及 4 个文件，按顺序修改：

1. `src/agents/project/agents/code_generator.py` — 后端核心，提示词 + 参数
2. `src/api/routers/project.py` — WebSocket 路由解析新字段
3. `web/context/GlobalContext.tsx` — 前端状态管理
4. `web/app/project/page.tsx` — Step 1 UI

---

## 各文件修改说明

### 1. `code_generator.py`

**新增常量**（文件顶部）：
```python
DIFFICULTY_LIMITS: dict[str, dict[str, int]] = {
    "low":    {"max_py_files": 10,  "max_lines": 3000},
    "medium": {"max_py_files": 25,  "max_lines": 7000},
    "high":   {"max_py_files": 40,  "max_lines": 10000},
}
```

**`generate()` 签名**：
```python
async def generate(self, spec, output_dir, ws_callback, difficulty="medium") -> dict:
```

**`_run_planning_phase` / `_run_coding_phase`** — 增加 `difficulty` 参数并透传给各自的 prompt builder。

**`_build_planning_prompt(spec, difficulty)` 末尾追加约束块**：
```
=== 强制约束（难度：X级） ===
1. 编程语言：仅允许 Python（.py）和 Shell 脚本（.sh/.bash）
   - 禁止生成任何 C/C++/Java/Rust/Go 源文件
2. 规模限制：
   - Python 文件总数 ≤ N 个
   - 全部代码总行数 ≤ N 行
   - 目录结构设计时控制文件数量，不要过度拆分
```

**`_build_coding_prompt(spec, plan_content, difficulty)` 末尾追加相同约束块**，额外加：
```
3. 每个函数不超过 50 行，优先用标准库
4. 完成后运行自检命令确认未超出限制，超出则合并文件
```

---

### 2. `project.py`

在 `websocket_generate_code` 中：
```python
task_content = data.get("task_content", "")
difficulty = data.get("difficulty", "medium")   # 新增，默认 medium
```

调用 `generator.generate()` 时传入 `difficulty=difficulty`。

---

### 3. `GlobalContext.tsx`

`ProjectState` 接口增加：
```typescript
difficulty: "low" | "medium" | "high";
```

`DEFAULT_PROJECT_STATE` 中：
```typescript
difficulty: "medium",
```

`startCodeGeneration` 发送时附带：
```typescript
difficulty: projectStateRef.current.difficulty,
```

---

### 4. `page.tsx`（Step 1 UI）

在网络搜索开关下方，生成按钮上方，插入三按钮难度选择器。解构时增加 `difficulty`：
```tsx
const { ..., difficulty, ... } = projectState;
```

按钮组（低/中/高），选中时高亮蓝色边框，每个按钮显示文件数和行数提示。

---

## 边缘情况与测试用例

### 正常路径
- 选低难度 → 生成代码 → 验证 `*.py` 文件数 ≤ 10，总行数 ≤ 3000
- 选高难度 → 生成代码 → 验证 `*.py` 文件数 ≤ 40，总行数 ≤ 10000
- 不选难度（使用默认）→ 按中等限制生效

### 边界情况
- `difficulty` 字段缺失时（旧客户端）→ 后端默认 `"medium"`，不报错
- `difficulty` 传入非法值（如 `"extreme"`）→ `DIFFICULTY_LIMITS.get()` 降级到 `"medium"`
- 生成的代码恰好等于上限 → 应通过验证

### 异常路径
- 前端未发送 `difficulty` 字段 → 后端 `data.get("difficulty", "medium")` 兜底
- 约束块注入后提示词超长 → 不影响功能，claude CLI 正常处理

---

## 验证方式

完成修改后，主动执行以下验证：

**后端单元测试**：
```bash
cd /home/jcz/DeepTutor
conda run -n deeptutor python -m pytest tests/test_code_generator.py -q
```

**提示词内容检查**（不需要真实运行 claude）：
```python
from src.agents.project.agents.code_generator import CodeGenerator, DIFFICULTY_LIMITS
from src.agents.project.agents.requirement_extractor import RequirementSpec
gen = CodeGenerator.__new__(CodeGenerator)
spec = RequirementSpec(theme="测试", modules=[])
prompt = gen._build_planning_prompt(spec, "low")
assert "≤ 10 个" in prompt
assert "禁止生成任何 C/C++" in prompt
print("✓ 提示词约束注入正确")
```

**端到端验证**（生成完成后在 repo 目录执行）：
```bash
find repo/ -name "*.py" | wc -l        # 低难度应 ≤ 10
find repo/ -name "*.cpp" | wc -l       # 应为 0
cat $(find repo/ -name "*.py") | wc -l # 低难度应 ≤ 3000
```

**前端验证**：
- 打开 `/project`，Step 1 应显示三个难度按钮
- 点击切换时高亮状态正确更新
- 选择低难度后完成代码生成，Step 4 日志中应无 `.cpp` 文件创建事件

---

## Bug 处理规范

遇到 bug 时按以下流程处理，不跳步骤：

```
1. 写最小复现测试（单文件可运行）
2. 确认测试失败
3. 修复代码
4. 确认测试通过
5. 一句话反思：[原因] → [今后避免的做法]
```

### 常见潜在 Bug

**Bug**：`_build_planning_prompt` 调用时未传 `difficulty`，使用旧调用方式
**复现**：`gen._build_planning_prompt(spec)` → 应使用默认值 `"medium"` 正常工作
**反思**：新增参数时设置合理默认值，保证向后兼容旧调用方 →  函数签名改动后搜索所有调用点

**Bug**：前端 `difficulty` 字段在 `resetProject` 时未重置
**复现**：完成一次生成后点击重置，再次进入 Step 1，难度按钮应恢复 "中等" 高亮
**反思**：新增 State 字段时同步检查 reset/default 逻辑

---

## 不做的事

- 不为 `difficulty` 添加持久化存储（除非要求）
- 不添加自定义难度输入框（除非要求）
- 不修改任务书生成（Phase A）的提示词，仅约束代码生成阶段
