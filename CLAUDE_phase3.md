# CLAUDE_phase3.md — Phase 3: 代码仓库生成（Claude Code Agent）

## 规则

- **不写兼容性代码**，除非用户明确要求。
- 需求不明确时向用户提问，不猜测。
- 每完成一个文件，立即运行对应测试，确认通过再继续。
- 发现 bug 时：先写能重现的测试 → 修复 → 确认通过 → 反思根因。
- **前置条件**：本机已安装 `claude` CLI（`npm install -g @anthropic-ai/claude-code`）且已登录（`claude login`）。**不需要 `ANTHROPIC_API_KEY`**。
- **认证方式**：复用系统 Claude Code 的 OAuth Token（`~/.claude/.credentials.json`）。DeepTutor 以 subprocess 调用 `claude`，不注入任何 API key，CLI 自动读取本机凭据。
- **模型跟随**：DeepTutor 不指定模型，Claude Code 用什么模型（Pro 默认模型 / 用户手动切换的模型）代码生成就用什么模型，无需在 DeepTutor 侧配置。
- **不使用 DashScope / 字节 API key**：代码生成走本机 Claude Code OAuth；任务书生成（Phase 1）继续用 DashScope/字节。两套认证并存，互不干扰。

---

## Phase 3 任务计划

按顺序实现，每步完成后立即测试：

| 步骤 | 文件 | 操作 | 依赖 |
|------|------|------|------|
| 1 | `src/agents/project/agents/requirement_extractor.py` | 从任务书提取结构化需求清单 | Phase 1 完成 |
| 2 | `src/agents/project/agents/code_generator.py` | 实现三阶段代码生成（分析→规划→生成+验证） | claude CLI / claude-agent-sdk |
| 3 | `src/agents/project/agents/code_verifier.py` | 运行冒烟测试，检查代码可执行性 | subprocess |
| 4 | `src/api/routers/project.py` — `generate-code` WebSocket | 替换占位符，接入 CodeGenerator | code_generator |
| 5 | `web/context/GlobalContext.tsx` | 补全 `startCodeGeneration`、`agentLogs`、`generatedFiles` 字段 | Phase 2 完成 |
| 6 | `web/app/project/page.tsx` — Step 4 | 替换"即将推出"占位，实现 Agent 日志 + 文件树 + 下载 zip | GlobalContext |

---

## 新增 / 修改文件列表

```
src/
└── agents/project/
    └── agents/
        ├── requirement_extractor.py    ← 新建：从任务书提取需求清单
        ├── code_generator.py           ← 改写：三阶段生成 + 验证
        └── code_verifier.py            ← 新建：冒烟测试验证
src/api/routers/
    └── project.py                      ← 修改：generate-code WebSocket 实现
web/
├── context/GlobalContext.tsx           ← 修改：补全 startCodeGeneration 等
└── app/project/page.tsx                ← 修改：Step 4 完整实现
tests/
    └── test_code_generator.py          ← 新建：单元测试
```

---

## 前置条件检查

```bash
# 1. 确认 claude CLI 已安装
which claude && claude --version
# 期望：/home/jcz/.local/bin/claude  +  2.x.x (Claude Code)

# 2. 确认 OAuth 凭据存在（有 claude login 过的机器上应该有此文件）
ls -la ~/.claude/.credentials.json
# 期望：存在，且 claudeAiOauth.subscriptionType 为 "pro"

# 3. 确认 claude 可以非交互式运行（关键测试）
timeout 30 claude -p "say hello in 3 words" --output-format json 2>&1
# 期望：返回 JSON，内含 result 字段

# 4. 确认 DeepTutor 的 PATH 能找到 claude
conda run -n deeptutor which claude
# 期望：同 which claude 的结果；若找不到，需将 claude 路径加入 conda env 的 PATH

# 如 claude 未安装：
npm install -g @anthropic-ai/claude-code
claude login    # 在浏览器完成 OAuth 授权
```

> **注意**：代码生成不需要 `ANTHROPIC_API_KEY` 环境变量，也不需要 `claude-agent-sdk` Python 包（放弃 SDK 方案，统一走 CLI subprocess）。

---

## 代码生成三阶段流程

```
┌──────────────────────────────────────────────────────┐
│  阶段 A — 需求分析（RequirementExtractor, ~5s）       │
│  输入：task_content (Markdown 任务书)                 │
│  输出：RequirementSpec（结构化需求，含模块列表）        │
│  方式：纯 LLM 调用（用 DeepTutor 现有 LLM factory）  │
│  ↓                                                   │
│  阶段 B — 架构规划（Claude Code agent, ~30s）         │
│  输入：RequirementSpec                               │
│  Claude 工具：Write（只写 PLAN.md 一个文件）          │
│  输出：PLAN.md（文件树 + 技术栈 + 模块说明）          │
│  ↓                                                   │
│  阶段 C — 代码生成（Claude Code agent, ~3-5min）      │
│  输入：RequirementSpec + PLAN.md                     │
│  Claude 工具：Write / Edit / Bash / Read / Glob      │
│  输出：完整项目文件树                                 │
│  ↓                                                   │
│  阶段 D — 验证（CodeVerifier, ~30s）                  │
│  运行：pip install -r requirements.txt               │
│        python -c "import <main_module>"              │
│        pytest tests/ -x -q（如存在）                 │
│  输出：verification_report（passed/failed + 错误日志）│
└──────────────────────────────────────────────────────┘
```

---

## 关键数据结构

### RequirementSpec

```python
@dataclass
class TaskModule:
    """任务书中的一个实践模块"""
    id: str           # "module_1"
    title: str        # "树莓派基础环境搭建"
    objectives: list[str]   # 该模块的学习目标
    technical_requirements: list[str]  # 技术要求
    deliverables: list[str]  # 该模块需提交的成果

@dataclass
class RequirementSpec:
    """从任务书提取的结构化需求"""
    theme: str
    tech_stack: list[str]      # ["Python", "ROS2", "OpenCV"]
    modules: list[TaskModule]  # 各实践模块
    install_requirements: list[str]  # 依赖包列表
    environment: str           # "Python 3.10 / Ubuntu 22.04 / ROS2 Humble"
    # 需求追踪：任务书章节 → 代码文件（生成后回填）
    coverage_map: dict[str, list[str]]  # {"module_1": ["src/env_setup.py", "README.md"]}
```

### WebSocket 消息协议（generate-code）

```
客户端 → 服务端：
{
  "session_id": "proj_xxx",
  "task_content": "## 课程背景\n..."   // 完整 Markdown 任务书
}

服务端 → 客户端（顺序）：
{"type": "phase",    "phase": "analysis", "content": "正在分析任务书需求..."}
{"type": "phase",    "phase": "planning", "content": "正在制定项目架构..."}
{"type": "phase",    "phase": "coding",   "content": "Claude Agent 开始编写代码..."}
{"type": "phase",    "phase": "verify",   "content": "正在验证代码可运行性..."}

{"type": "agent_log", "log_type": "tool_use",    "tool": "Write", "path": "src/main.py",    "content": "写入 src/main.py"}
{"type": "agent_log", "log_type": "tool_use",    "tool": "Bash",  "path": "",               "content": "运行: pip install -r requirements.txt"}
{"type": "agent_log", "log_type": "tool_result", "tool": "Bash",  "path": "",               "content": "Successfully installed 5 packages"}
{"type": "agent_log", "log_type": "message",     "tool": null,    "path": "",               "content": "PLAN.md 已生成，开始编写代码..."}
{"type": "agent_log", "log_type": "error",       "tool": "Bash",  "path": "",               "content": "ModuleNotFoundError: No module named 'cv2'"}

{"type": "file_created", "path": "src/main.py"}
{"type": "file_created", "path": "requirements.txt"}

{"type": "verify_result", "passed": true,  "report": "所有检查通过"}
{"type": "verify_result", "passed": false, "report": "ImportError: ...", "fixed": true}

{"type": "coverage",  "map": {"module_1": ["src/env_setup.py"], "module_2": ["src/ros_node.py"]}}

{"type": "complete",
 "session_id": "proj_xxx",
 "repo_path": "data/user/projects/proj_xxx/repo",
 "file_tree": [...],
 "coverage_map": {...},
 "verify_passed": true}

{"type": "error", "content": "ANTHROPIC_API_KEY 未配置"}
```

---

## 1. RequirementExtractor（需求提取器）

**文件**：`src/agents/project/agents/requirement_extractor.py`

**职责**：调用 DeepTutor 现有 LLM factory（不用 Anthropic key），从 Markdown 任务书中提取 `RequirementSpec`。

```python
class RequirementExtractor:
    """
    使用现有 LLM（DashScope/字节）分析任务书，输出结构化需求。
    不依赖 Anthropic API key，Phase 1 已有的 LLM 配置即可用。
    """

    async def extract(self, task_content: str, ws_callback: Callable) -> RequirementSpec:
        """
        从 Markdown 任务书提取结构化需求。

        流程：
        1. 调用 LLM，要求输出 JSON 格式的 RequirementSpec
        2. 解析 JSON，构建 RequirementSpec 对象
        3. 返回 RequirementSpec
        """
        await ws_callback({"type": "phase", "phase": "analysis", "content": "正在分析任务书需求..."})
        prompt = self._build_extraction_prompt(task_content)
        # 调用现有 LLM factory（非流式，等待完整 JSON）
        response = await llm_factory.complete(prompt, ...)
        spec = self._parse_response(response)
        await ws_callback({"type": "status", "content": f"需求分析完成：发现 {len(spec.modules)} 个模块，技术栈：{', '.join(spec.tech_stack)}"})
        return spec

    def _build_extraction_prompt(self, task_content: str) -> str:
        return f"""请分析以下课程实习任务书，提取结构化需求，以 JSON 格式输出：

{task_content}

输出格式（严格 JSON，不含注释）：
{{
  "theme": "主题名称",
  "tech_stack": ["Python", "ROS2"],
  "environment": "Python 3.10 / Ubuntu 22.04",
  "install_requirements": ["numpy", "opencv-python"],
  "modules": [
    {{
      "id": "module_1",
      "title": "模块名称",
      "objectives": ["目标1", "目标2"],
      "technical_requirements": ["技术要求1"],
      "deliverables": ["代码文件", "报告"]
    }}
  ]
}}

只输出 JSON，不要解释。"""
```

---

## 2. CodeGenerator（代码生成器）

**文件**：`src/agents/project/agents/code_generator.py`

**注意**：优先使用 `claude-agent-sdk`（方案 A），fallback 到 `claude` CLI subprocess（方案 B）。两种方案都需要 `ANTHROPIC_API_KEY`。

```python
class CodeGenerator:
    """
    三阶段代码仓库生成器。
    通过 subprocess 调用本机 `claude` CLI，复用 ~/.claude/.credentials.json 中的
    OAuth Token（Claude Pro 订阅），不需要 ANTHROPIC_API_KEY。
    DeepTutor 不指定模型，Claude Code 当前使用什么模型就用什么模型。
    """

    CLAUDE_BIN = shutil.which("claude") or os.path.expanduser("~/.local/bin/claude")

    def __init__(self):
        if not os.path.isfile(self.CLAUDE_BIN):
            raise EnvironmentError(
                f"找不到 claude CLI（{self.CLAUDE_BIN}）。"
                "请运行：npm install -g @anthropic-ai/claude-code && claude login"
            )

    async def generate(
        self,
        spec: RequirementSpec,
        output_dir: str,
        ws_callback: Callable
    ) -> dict:
        repo_dir = Path(output_dir) / "repo"
        repo_dir.mkdir(parents=True, exist_ok=True)

        # 阶段 B：架构规划
        await ws_callback({"type": "phase", "phase": "planning", "content": "正在制定项目架构..."})
        await self._run_planning_phase(spec, repo_dir, ws_callback)

        # 阶段 C：代码生成
        await ws_callback({"type": "phase", "phase": "coding", "content": "Claude Agent 开始编写代码..."})
        await self._run_coding_phase(spec, repo_dir, ws_callback)

        # 阶段 D：验证
        await ws_callback({"type": "phase", "phase": "verify", "content": "正在验证代码可运行性..."})
        verifier = CodeVerifier(repo_dir, ws_callback)
        verify_result = await verifier.verify()

        # 如验证失败，让 Claude 自动修复（最多 2 次）
        if not verify_result["passed"]:
            await self._fix_errors(verify_result["report"], repo_dir, ws_callback)
            verify_result = await verifier.verify()

        # 构建覆盖率映射
        coverage_map = self._build_coverage_map(spec, repo_dir)
        await ws_callback({"type": "coverage", "map": coverage_map})

        file_tree = self._build_file_tree(repo_dir)
        return {
            "repo_path": str(repo_dir),
            "file_tree": file_tree,
            "coverage_map": coverage_map,
            "verify_passed": verify_result["passed"],
            "verify_report": verify_result["report"],
        }

    async def _run_planning_phase(self, spec: RequirementSpec, repo_dir: Path, ws_callback: Callable):
        """让 Claude 只生成 PLAN.md（架构规划文档）"""
        prompt = self._build_planning_prompt(spec)
        await self._run_agent(
            prompt=prompt,
            cwd=str(repo_dir),
            allowed_tools=["Write"],   # 规划阶段只允许 Write，防止跑偏
            ws_callback=ws_callback
        )

    async def _run_coding_phase(self, spec: RequirementSpec, repo_dir: Path, ws_callback: Callable):
        """让 Claude 读取 PLAN.md 并实现所有代码"""
        plan_content = (repo_dir / "PLAN.md").read_text(encoding="utf-8") if (repo_dir / "PLAN.md").exists() else ""
        prompt = self._build_coding_prompt(spec, plan_content)
        await self._run_agent(
            prompt=prompt,
            cwd=str(repo_dir),
            allowed_tools=["Read", "Write", "Edit", "Bash", "Glob"],
            ws_callback=ws_callback
        )

    async def _fix_errors(self, error_report: str, repo_dir: Path, ws_callback: Callable):
        """让 Claude 自动修复验证阶段报告的错误"""
        await ws_callback({"type": "status", "content": "检测到错误，Claude Agent 正在自动修复..."})
        prompt = f"""请修复以下错误，使项目能够正常运行：

{error_report}

要求：
1. 最小化改动，只修复报告中的错误
2. 确保修复后 `python -c "import <main_module>"` 能通过
3. 如有依赖缺失，更新 requirements.txt
"""
        await self._run_agent(
            prompt=prompt,
            cwd=str(repo_dir),
            allowed_tools=["Read", "Write", "Edit", "Bash"],
            ws_callback=ws_callback
        )

    async def _run_agent(self, prompt: str, cwd: str, allowed_tools: list[str], ws_callback: Callable):
        """
        调用本机 claude CLI subprocess。
        - 不注入 ANTHROPIC_API_KEY，CLI 自动读取 ~/.claude/.credentials.json 的 OAuth Token
        - 不指定 --model，复用 Claude Code 当前配置的模型（Pro 默认 / 用户手动切换）
        """
        await self._run_via_cli(prompt, cwd, allowed_tools, ws_callback)

    async def _run_via_cli(self, prompt: str, cwd: str, allowed_tools: list[str], ws_callback: Callable):
        """
        subprocess 调用 claude CLI。
        关键：env 直接用 os.environ（不覆盖，不添加 ANTHROPIC_API_KEY），
        确保 claude 使用本机 ~/.claude/.credentials.json 的 OAuth 认证。
        """
        process = await asyncio.create_subprocess_exec(
            self.CLAUDE_BIN,
            "-p", prompt,
            "--allowedTools", ",".join(allowed_tools),
            "--output-format", "stream-json",
            cwd=cwd,
            env=os.environ,   # 原样传递，不注入 API key
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE
        )
        async for line in process.stdout:
            line_str = line.decode().strip()
            if line_str:
                try:
                    await self._handle_cli_event(json.loads(line_str), ws_callback)
                except json.JSONDecodeError:
                    pass
        await process.wait()
        if process.returncode not in (0, None):
            stderr = await process.stderr.read()
            raise RuntimeError(f"claude CLI 退出码 {process.returncode}: {stderr.decode()[:500]}")

    async def _handle_cli_event(self, event: dict, ws_callback: Callable):
        """将 claude CLI stream-json 事件转换为前端格式"""
        event_type = event.get("type", "")
        if event_type == "tool_use":
            name = event.get("name", "")
            inp = event.get("input", {})
            path = inp.get("file_path") or inp.get("path") or ""
            await ws_callback({
                "type": "agent_log",
                "log_type": "tool_use",
                "tool": name,
                "path": path,
                "content": f"{name}: {path or '(no path)'}",
            })
            if name in ("Write", "Edit") and path:
                await ws_callback({"type": "file_created", "path": path})
        elif event_type == "tool_result":
            content = str(event.get("content", ""))[:200]
            await ws_callback({"type": "agent_log", "log_type": "tool_result", "tool": None, "path": "", "content": content})
        elif event_type in ("message", "assistant"):
            for block in event.get("content", []):
                if isinstance(block, dict) and block.get("type") == "text":
                    await ws_callback({"type": "agent_log", "log_type": "message", "tool": None, "path": "", "content": block["text"][:500]})

    def _build_planning_prompt(self, spec: RequirementSpec) -> str:
        modules_desc = "\n".join(
            f"- 模块 {m.id}「{m.title}」：{', '.join(m.objectives)}"
            for m in spec.modules
        )
        return f"""请为以下项目制定详细的代码架构计划，只输出一个文件 PLAN.md。

项目主题：{spec.theme}
技术栈：{', '.join(spec.tech_stack)}
运行环境：{spec.environment}

需要覆盖的模块：
{modules_desc}

请在 PLAN.md 中包含：
1. 项目目录结构（完整文件树，含每个文件的用途说明）
2. 技术选型说明
3. 各模块与代码文件的对应关系（模块 ID → 文件列表）
4. 主要类/函数接口定义
5. 运行方式（入口命令）

只创建 PLAN.md，不创建其他文件。"""

    def _build_coding_prompt(self, spec: RequirementSpec, plan_content: str) -> str:
        modules_checklist = "\n".join(
            f"- [ ] 模块 {m.id}「{m.title}」\n"
            f"      技术要求：{', '.join(m.technical_requirements)}\n"
            f"      交付物：{', '.join(m.deliverables)}"
            for m in spec.modules
        )
        return f"""请根据以下项目计划，完整实现所有代码文件。

=== 架构计划（PLAN.md）===
{plan_content}

=== 需要覆盖的模块 ===
{modules_checklist}

实现要求：
1. 先执行 `git init` 初始化仓库
2. 按 PLAN.md 的目录结构创建所有文件
3. 每个模块必须对应至少一个实现文件，不允许遗漏
4. README.md 必须包含：
   a. 项目介绍（与任务书对应）
   b. 环境要求（Python 版本、OS）
   c. 安装步骤（逐行可执行命令）
   d. 运行示例（带预期输出）
   e. 项目结构说明
   f. 各模块功能说明
5. requirements.txt 列出所有依赖（含版本号）
6. 代码须有中英文注释，关键函数须有 docstring
7. 每个模块包含基础单元测试（tests/ 目录）
8. 全部写完后，运行 `pip install -r requirements.txt` 并验证主模块可导入
9. 如发现错误立即修复，确保最终状态代码可运行

技术栈：{', '.join(spec.tech_stack)}
环境：{spec.environment}"""

    def _build_coverage_map(self, spec: RequirementSpec, repo_dir: Path) -> dict:
        """构建「任务书模块 → 代码文件」覆盖率映射"""
        all_files = [
            str(p.relative_to(repo_dir))
            for p in repo_dir.rglob("*")
            if p.is_file() and ".git" not in p.parts
        ]
        # 简单关键词匹配（精确匹配由 LLM 在 PLAN.md 中定义）
        coverage = {}
        for module in spec.modules:
            keywords = [module.id, module.title.lower().replace(" ", "_")]
            matched = [f for f in all_files if any(kw in f.lower() for kw in keywords)]
            coverage[module.id] = matched if matched else ["(未找到直接对应文件，请查看 PLAN.md)"]
        return coverage

    def _build_file_tree(self, base_dir: Path) -> list:
        result = []
        for item in sorted(base_dir.iterdir()):
            if item.name.startswith(".git"):
                continue
            node = {
                "name": item.name,
                "path": str(item.relative_to(base_dir)),
                "type": "directory" if item.is_dir() else "file",
            }
            if item.is_dir():
                node["children"] = self._build_file_tree(item)
            result.append(node)
        return result
```

---

## 3. CodeVerifier（代码验证器）

**文件**：`src/agents/project/agents/code_verifier.py`

```python
class CodeVerifier:
    """
    对生成的代码仓库执行冒烟测试：
    1. pip install -r requirements.txt
    2. 主模块可导入（python -c "import <main_module>"）
    3. 如有 tests/ 目录，运行 pytest -x -q
    """
    TIMEOUT = 120   # seconds，pip install 可能较慢

    def __init__(self, repo_dir: Path, ws_callback: Callable):
        self.repo_dir = repo_dir
        self.ws_callback = ws_callback

    async def verify(self) -> dict:
        """返回 {"passed": bool, "report": str}"""
        steps = []

        # Step 1: pip install
        if (self.repo_dir / "requirements.txt").exists():
            ok, out = await self._run("pip install -r requirements.txt -q", timeout=self.TIMEOUT)
            steps.append(("pip install", ok, out))
            if not ok:
                return {"passed": False, "report": f"依赖安装失败:\n{out}"}

        # Step 2: 主模块导入
        main_module = self._detect_main_module()
        if main_module:
            ok, out = await self._run(f"python -c \"import {main_module}; print('import OK')\"", timeout=30)
            steps.append(("import check", ok, out))
            if not ok:
                return {"passed": False, "report": f"主模块导入失败 ({main_module}):\n{out}"}

        # Step 3: 运行 pytest（如存在）
        if (self.repo_dir / "tests").exists():
            ok, out = await self._run("python -m pytest tests/ -x -q --tb=short 2>&1 | head -50", timeout=60)
            steps.append(("pytest", ok, out))
            if not ok:
                return {"passed": False, "report": f"测试失败:\n{out}"}

        passed_steps = ", ".join(s[0] for s in steps if s[1])
        return {"passed": True, "report": f"所有检查通过：{passed_steps}"}

    async def _run(self, cmd: str, timeout: int) -> tuple[bool, str]:
        try:
            proc = await asyncio.create_subprocess_shell(
                cmd,
                cwd=str(self.repo_dir),
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.STDOUT,
            )
            stdout, _ = await asyncio.wait_for(proc.communicate(), timeout=timeout)
            output = stdout.decode(errors="replace")[:1000]
            await self.ws_callback({
                "type": "agent_log",
                "log_type": "tool_use" if proc.returncode == 0 else "error",
                "tool": "Bash",
                "path": "",
                "content": f"$ {cmd}\n{output}",
            })
            return proc.returncode == 0, output
        except asyncio.TimeoutError:
            return False, f"命令超时（>{timeout}s）：{cmd}"

    def _detect_main_module(self) -> str | None:
        """检测主模块名（查找 main.py / app.py / src/__init__.py 等）"""
        candidates = ["main", "app", "run"]
        for name in candidates:
            if (self.repo_dir / f"{name}.py").exists():
                return name
        src = self.repo_dir / "src"
        if src.is_dir() and (src / "__init__.py").exists():
            return "src"
        return None
```

---

## 4. 路由修改 `src/api/routers/project.py`

替换现有的 `websocket_generate_code` 占位符（当前返回 "not yet implemented"）：

```python
@router.websocket("/project/generate-code")
async def websocket_generate_code(websocket: WebSocket):
    """流式代码仓库生成。"""
    await websocket.accept()
    log_queue: asyncio.Queue = asyncio.Queue()

    async def ws_callback(msg: dict):
        await log_queue.put(msg)

    async def log_pusher():
        while True:
            entry = await log_queue.get()
            if entry is None:
                break
            try:
                await websocket.send_json(entry)
            except Exception:
                break

    try:
        data = await websocket.receive_json()
        session_id = data.get("session_id")
        task_content = data.get("task_content", "")

        if not session_id:
            await websocket.send_json({"type": "error", "content": "session_id 必填"})
            return

        session_mgr = get_project_session_manager()
        session = session_mgr.get_session(session_id)
        if not session:
            await websocket.send_json({"type": "error", "content": f"会话不存在：{session_id}"})
            return

        output_dir = str(PROJECTS_DIR / session_id)

        # 检查 claude CLI 是否可用（构造时验证）
        try:
            from src.agents.project.agents.code_generator import CodeGenerator
            generator = CodeGenerator()   # 构造时检查 claude CLI 是否存在
        except EnvironmentError as e:
            await websocket.send_json({"type": "error", "content": str(e)})
            return

        session_mgr.update_session(session_id, status="code_generating")

        pusher_task = asyncio.create_task(log_pusher())

        # 阶段 A：需求提取（用现有 LLM，不需要 Anthropic key）
        from src.agents.project.agents.requirement_extractor import RequirementExtractor
        extractor = RequirementExtractor()
        spec = await extractor.extract(task_content, ws_callback)

        # 阶段 B/C/D：代码生成 + 验证
        result = await generator.generate(
            spec=spec,
            output_dir=output_dir,
            ws_callback=ws_callback,
        )

        session_mgr.update_session(
            session_id,
            status="complete",
            repo_path=result["repo_path"],
        )

        await log_queue.put(None)
        await pusher_task

        await websocket.send_json({
            "type": "complete",
            "session_id": session_id,
            "repo_path": result["repo_path"],
            "file_tree": result["file_tree"],
            "coverage_map": result["coverage_map"],
            "verify_passed": result["verify_passed"],
        })

    except WebSocketDisconnect:
        await log_queue.put(None)
    except Exception as e:
        logger.exception("Code generation error")
        await log_queue.put(None)
        try:
            await websocket.send_json({"type": "error", "content": str(e)})
        except Exception:
            pass
    finally:
        await websocket.close()
```

---

## 5. GlobalContext 补全（Phase 3 新增字段）

**文件**：`web/context/GlobalContext.tsx`

### 5.1 在 `ProjectState` 中补全（Phase 2 已有，确认存在）

```typescript
// 以下字段 Phase 2 已有骨架，Phase 3 确认完整：
agentLogs: AgentLogEntry[];     // Claude Agent 操作日志
generatedFiles: FileTreeNode[]; // 生成的文件树
repoPath: string | null;
verifyPassed: boolean | null;   // Phase 3 新增
coverageMap: Record<string, string[]> | null;  // Phase 3 新增：模块→文件映射
```

### 5.2 `DEFAULT_PROJECT_STATE` 补全

```typescript
agentLogs: [],
generatedFiles: [],
repoPath: null,
verifyPassed: null,     // Phase 3 新增
coverageMap: null,      // Phase 3 新增
```

### 5.3 `GlobalContextType` 补全

```typescript
startCodeGeneration: () => void;   // Phase 2 已声明，Phase 3 实现
```

### 5.4 `startCodeGeneration` 实现

```typescript
const startCodeGeneration = useCallback(() => {
  if (projectWs.current) projectWs.current.close();

  setProjectState(prev => ({
    ...prev,
    step: "code_generating",
    agentLogs: [],
    generatedFiles: [],
    verifyPassed: null,
    coverageMap: null,
    error: null,
  }));

  const base = process.env.NEXT_PUBLIC_API_BASE || "";
  const wsBase = base.replace(/^http:/, "ws:").replace(/^https:/, "wss:");
  const ws = new WebSocket(`${wsBase}/api/v1/project/generate-code`);
  projectWs.current = ws;

  ws.onopen = () => {
    ws.send(JSON.stringify({
      session_id: projectStateRef.current.sessionId,
      task_content: projectStateRef.current.taskContent,
    }));
  };

  ws.onmessage = (event) => {
    const data = JSON.parse(event.data);
    switch (data.type) {
      case "phase":
        setProjectState(prev => ({
          ...prev,
          logs: [...prev.logs, { type: "status", content: data.content, timestamp: Date.now() }],
        }));
        break;
      case "agent_log":
        setProjectState(prev => ({
          ...prev,
          agentLogs: [...prev.agentLogs, {
            timestamp: new Date().toLocaleTimeString(),
            type: data.log_type,
            tool: data.tool,
            path: data.path,
            content: data.content,
          }],
        }));
        break;
      case "file_created":
        // 仅记录日志，文件树在 complete 时整体更新
        break;
      case "verify_result":
        setProjectState(prev => ({
          ...prev,
          logs: [...prev.logs, {
            type: data.passed ? "status" : "error",
            content: data.passed ? `验证通过：${data.report}` : `验证失败：${data.report}`,
            timestamp: Date.now(),
          }],
        }));
        break;
      case "coverage":
        setProjectState(prev => ({ ...prev, coverageMap: data.map }));
        break;
      case "complete":
        setProjectState(prev => ({
          ...prev,
          step: "complete",
          repoPath: data.repo_path,
          generatedFiles: data.file_tree,
          coverageMap: data.coverage_map,
          verifyPassed: data.verify_passed,
        }));
        break;
      case "error":
        setProjectState(prev => ({ ...prev, step: "task_review", error: data.content }));
        break;
    }
  };

  ws.onerror = () => {
    setProjectState(prev => ({ ...prev, step: "task_review", error: "WebSocket 连接错误" }));
  };
}, []);
```

### 5.5 Provider value 新增字段

```typescript
startCodeGeneration,   // 加入 value 对象
```

---

## 6. 前端 Step 4 完整实现（`web/app/project/page.tsx`）

替换当前 Step 4 的"即将推出"占位。

### 6.1 布局

```
┌──────────────── Step 4: 代码生成 ──────────────────────┐
│ ┌── 左 1/3：Agent 日志 ──────────┐                     │
│ │ [分析] [规划] [编码] [验证] 阶段指示                  │
│ │ 滚动日志列表：                                        │
│ │  ✏️ Write  src/main.py                               │
│ │  ⚡ Bash   pip install -r requirements.txt           │
│ │  ✅ tool_result  Successfully installed...           │
│ └─────────────────────────────────┘                   │
│ ┌── 右 2/3：文件树 + 覆盖率 ──────┐                     │
│ │ 文件树（实时更新）               │                     │
│ │ ├── src/                        │                     │
│ │ │   ├── main.py                 │                     │
│ │ │   └── ros_node.py             │                     │
│ │ ├── tests/                      │                     │
│ │ ├── requirements.txt            │                     │
│ │ └── README.md                   │                     │
│ │                                 │                     │
│ │ 需求覆盖率：                     │                     │
│ │  ✅ 模块1 → src/main.py         │                     │
│ │  ✅ 模块2 → src/ros_node.py     │                     │
│ └─────────────────────────────────┘                   │
│                                                        │
│ [验证状态] ✅ 代码可运行 / ❌ 存在错误（正在修复...）    │
│                           [↓ 下载 zip] [↑ 重新生成]   │
└────────────────────────────────────────────────────────┘
```

### 6.2 关键组件代码骨架

```typescript
// 阶段进度条
const CODE_PHASES = [
  { key: "analysis", label: "需求分析" },
  { key: "planning", label: "架构规划" },
  { key: "coding",   label: "代码生成" },
  { key: "verify",   label: "验证" },
];

// Agent 日志图标映射
const TOOL_ICONS: Record<string, string> = {
  Write: "✏️", Edit: "✏️", Bash: "⚡", Read: "📖", Glob: "🔍",
};

// 下载 zip 按钮
// GET /api/v1/project/{sessionId}/download-repo → 返回 zip 包
<button onClick={() => {
  window.location.href = apiUrl(`/api/v1/project/${projectState.sessionId}/download-repo`);
}}>
  ↓ 下载代码 zip
</button>

// 覆盖率展示
{projectState.coverageMap && Object.entries(projectState.coverageMap).map(([moduleId, files]) => (
  <div key={moduleId} className="flex items-start gap-2">
    <CheckCircle className="text-green-500 mt-0.5" size={16} />
    <div>
      <span className="font-medium">{moduleId}</span>
      <div className="text-xs text-gray-400">{files.join(", ")}</div>
    </div>
  </div>
))}
```

### 6.3 新增路由：下载 zip

在 `src/api/routers/project.py` 中新增：

```python
import zipfile, io

@router.get("/project/{session_id}/download-repo")
async def download_repo(session_id: str) -> StreamingResponse:
    """将生成的代码仓库打包为 zip 下载（排除 .git 目录）"""
    session_mgr = get_project_session_manager()
    session = session_mgr.get_session(session_id)
    if not session:
        raise HTTPException(404, "Session not found")

    repo_dir = PROJECTS_DIR / session_id / "repo"
    if not repo_dir.exists():
        raise HTTPException(404, "Repository not found")

    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        for file_path in repo_dir.rglob("*"):
            if ".git" in file_path.parts or not file_path.is_file():
                continue
            zf.write(file_path, file_path.relative_to(repo_dir))
    buf.seek(0)

    theme_slug = session.get("theme", "project").replace(" ", "_")[:30]
    filename = f"{theme_slug}_{session_id[:8]}.zip"

    from fastapi.responses import StreamingResponse
    return StreamingResponse(
        buf,
        media_type="application/zip",
        headers={"Content-Disposition": f'attachment; filename="{filename}"'},
    )
```

---

## 边缘情况

### code_generator.py — claude CLI 未安装 / 未登录
- `shutil.which("claude")` 返回 None 且 `~/.local/bin/claude` 不存在 → 构造函数抛 `EnvironmentError`，路由捕获后发送错误消息，提示用户安装并登录
- CLI 存在但 OAuth Token 已过期 → claude CLI 自动用 `refreshToken` 续期，无需 DeepTutor 介入
- 用户未登录（`~/.claude/.credentials.json` 不存在）→ claude CLI 进程会退出并报错，`process.returncode != 0`，捕获 stderr 后发送错误消息，提示运行 `claude login`

### code_generator.py — conda 环境中 PATH 找不到 claude
- `conda run -n deeptutor` 的 PATH 可能不含 `~/.local/bin/`，需确认：
  ```bash
  conda run -n deeptutor which claude
  ```
  若找不到，`CLAUDE_BIN` 会回退到硬编码的 `~/.local/bin/claude`（已在类属性中处理）

### code_generator.py — 生成超时
- Claude Agent 代码生成时间不定（复杂项目可能 10+ 分钟），不设全局超时
- WebSocket 心跳：每 30s 发送 `{"type": "ping"}`，前端忽略该消息类型，防止代理层断连

### code_generator.py — PLAN.md 未生成
- 规划阶段结束后检查 `PLAN.md` 是否存在，不存在则用 `spec` 构建默认计划并写入，不中断流程

### code_verifier.py — 无 requirements.txt
- 跳过 pip install 步骤，直接做主模块导入检查
- 无主模块（未找到 main.py / app.py）→ 跳过导入检查，记录警告日志，不判定为失败

### code_verifier.py — pip install 失败
- 最多重试 1 次（添加 `--ignore-requires-python` flag）
- 仍失败 → `verify_passed=False`，触发 `_fix_errors`

### requirement_extractor.py — 任务书格式不规范
- LLM 返回无效 JSON → `json.JSONDecodeError`，捕获后构建最小 `RequirementSpec`（theme 来自 session，modules 为空列表），继续生成

### download-repo — 仓库文件过大（>200MB）
- 流式 zip，不一次性读入内存（`StreamingResponse` + `io.BytesIO` 分块写入）
- 超过 500MB 时拒绝下载，返回 413

### 前端 Step 4 — agentLogs 条目过多
- 超过 500 条时，只保留最新 500 条（`setProjectState(prev => ({ ...prev, agentLogs: [...prev.agentLogs.slice(-499), newEntry] }))`）

---

## 测试用例 `tests/test_code_generator.py`

```python
import pytest
from unittest.mock import AsyncMock, MagicMock, patch
from pathlib import Path

from src.agents.project.agents.requirement_extractor import RequirementExtractor, RequirementSpec, TaskModule
from src.agents.project.agents.code_verifier import CodeVerifier


# ── RequirementExtractor ──────────────────────────────────────────────

class TestRequirementExtractor:
    @pytest.mark.asyncio
    async def test_extract_valid_task(self, tmp_path):
        """正常任务书 → 返回 RequirementSpec，含至少 1 个模块"""
        task = """
## 课程背景
本课程基于 ROS2 机器人导航...

## 模块一：环境搭建
目标：安装 ROS2 Humble
技术要求：Ubuntu 22.04, Python 3.10
"""
        extractor = RequirementExtractor()
        logs = []
        async def cb(msg): logs.append(msg)

        with patch("src.agents.project.agents.requirement_extractor.llm_factory") as mock_llm:
            mock_llm.complete = AsyncMock(return_value='''{
                "theme": "ROS2 导航",
                "tech_stack": ["Python", "ROS2"],
                "environment": "Python 3.10 / Ubuntu 22.04",
                "install_requirements": ["rclpy"],
                "modules": [{"id": "module_1", "title": "环境搭建",
                             "objectives": ["安装 ROS2"],
                             "technical_requirements": ["Ubuntu 22.04"],
                             "deliverables": ["安装截图"]}]
            }''')
            spec = await extractor.extract(task, cb)

        assert spec.theme == "ROS2 导航"
        assert len(spec.modules) == 1
        assert spec.modules[0].id == "module_1"

    @pytest.mark.asyncio
    async def test_extract_invalid_json_fallback(self):
        """LLM 返回无效 JSON → 构建最小 RequirementSpec，不抛异常"""
        extractor = RequirementExtractor()
        with patch("src.agents.project.agents.requirement_extractor.llm_factory") as mock_llm:
            mock_llm.complete = AsyncMock(return_value="这不是JSON")
            spec = await extractor.extract("任务书内容", AsyncMock())
        assert isinstance(spec, RequirementSpec)
        assert spec.modules == []


# ── CodeVerifier ──────────────────────────────────────────────────────

class TestCodeVerifier:
    @pytest.mark.asyncio
    async def test_verify_no_requirements(self, tmp_path):
        """无 requirements.txt → 跳过 pip，不判定为失败"""
        verifier = CodeVerifier(tmp_path, AsyncMock())
        result = await verifier.verify()
        assert result["passed"] is True

    @pytest.mark.asyncio
    async def test_detect_main_module_main_py(self, tmp_path):
        """存在 main.py → 检测到主模块 'main'"""
        (tmp_path / "main.py").write_text("print('hello')")
        verifier = CodeVerifier(tmp_path, AsyncMock())
        assert verifier._detect_main_module() == "main"

    @pytest.mark.asyncio
    async def test_detect_main_module_none(self, tmp_path):
        """无主模块文件 → 返回 None，跳过导入检查"""
        verifier = CodeVerifier(tmp_path, AsyncMock())
        assert verifier._detect_main_module() is None

    @pytest.mark.asyncio
    async def test_verify_passes_with_valid_main(self, tmp_path):
        """有效 main.py（无依赖）→ 验证通过"""
        (tmp_path / "main.py").write_text("def main(): pass")
        verifier = CodeVerifier(tmp_path, AsyncMock())
        result = await verifier.verify()
        assert result["passed"] is True


# ── CodeGenerator（集成冒烟测试，需真实 ANTHROPIC_API_KEY） ──────────

class TestCodeGeneratorUnit:
    def test_missing_claude_cli_raises(self, monkeypatch):
        """claude CLI 不存在 → 构造函数抛 EnvironmentError"""
        from src.agents.project.agents.code_generator import CodeGenerator
        monkeypatch.setattr(CodeGenerator, "CLAUDE_BIN", "/nonexistent/claude")
        with pytest.raises(EnvironmentError, match="claude CLI"):
            CodeGenerator()

    def test_build_coverage_map_with_matching_files(self, tmp_path, monkeypatch):
        """文件名含模块 ID → 覆盖率映射正确"""
        from src.agents.project.agents.code_generator import CodeGenerator
        # 让 CLAUDE_BIN 指向一个存在的文件（避免 EnvironmentError）
        monkeypatch.setattr(CodeGenerator, "CLAUDE_BIN", "/bin/sh")
        gen = CodeGenerator()
        (tmp_path / "module_1.py").write_text("")
        spec = MagicMock()
        spec.modules = [MagicMock(id="module_1", title="Test Module")]
        cmap = gen._build_coverage_map(spec, tmp_path)
        assert "module_1.py" in cmap["module_1"][0]
```

---

## 主动测试策略

```bash
# ── Step 1: 单元测试（无需 ANTHROPIC_API_KEY）──────────────
conda run -n deeptutor python -m pytest tests/test_code_generator.py -v -k "not integration"

# ── Step 2: TypeScript 编译检查 ───────────────────────────
export NVM_DIR="$HOME/.nvm" && source "$NVM_DIR/nvm.sh" && nvm use 20
cd web && npx tsc --noEmit 2>&1 | head -20

# ── Step 3: 后端启动 + WebSocket 冒烟测试 ─────────────────
# 不需要 ANTHROPIC_API_KEY，claude 已 OAuth 登录即可
conda run -n deeptutor python -m uvicorn src.api.main:app --port 8001 &
sleep 3

# 验证路由存在
curl -s --noproxy "*" http://localhost:8001/api/v1/openapi.json | python3 -c "
import json,sys; spec=json.load(sys.stdin)
paths=spec['paths']
assert '/api/v1/project/generate-code' in paths, 'route missing'
print('generate-code route: OK')
"

# 验证 API key 缺失时返回正确错误
# （临时取消 API key，用 wscat 或 Python websocket 测试）
python3 -c "
import asyncio, websockets, json, os

async def test():
    url = 'ws://localhost:8001/api/v1/project/generate-code'
    async with websockets.connect(url) as ws:
        await ws.send(json.dumps({'session_id': 'nonexistent', 'task_content': 'test'}))
        msg = json.loads(await ws.recv())
        print('Response:', msg)
        assert msg['type'] == 'error'
        print('Error handling: OK')
asyncio.run(test())
"

# ── Step 4: E2E Playwright 测试 ───────────────────────────
cd web && npm run dev &
sleep 5
npx playwright test tests/e2e/project.spec.ts --reporter=line

# ── Step 5: 完整集成测试（需 claude 已登录，运行时间 5-10 分钟）─
# 不需要 ANTHROPIC_API_KEY，确保 claude 已 oauth 登录即可
conda run -n deeptutor python -m pytest tests/test_code_generator.py -v -k "integration" --timeout=600
```

---

## Bug 处理协议

1. **复现**：写最小化的 pytest test 或 Python script，使其失败。
2. **修复**：只改最小范围代码，不动无关逻辑。
3. **验证**：运行测试，确认由红转绿。
4. **反思**：在本文件 [反思记录] 节追加一条。

---

## 反思记录

*每次纠错后在此追加，格式：`日期 · 问题简述 · 根因 · 预防`*

2026-03-23 · `process.stdout` 可能为 None，mypy 严格模式警告 · `asyncio.create_subprocess_exec` 返回的 `stdout` 类型为 `Optional[StreamReader]`，但 `PIPE` 时一定有值 · 在 subprocess 创建后紧跟 `assert process.stdout is not None` 消除类型警告，无需运行时降级

2026-03-23 · claude CLI 在系统 PATH 中存在但 conda env 的 PATH 不包含 `~/.local/bin/` · `shutil.which("claude")` 在 subprocess 中搜索的是调用进程的 PATH，conda env 继承了完整 PATH，实测 `CLAUDE_BIN=/home/jcz/.local/bin/claude` 正确 · `_find_claude_bin()` 优先 `shutil.which`，硬编码 `~/.local/bin/claude` 作为 fallback，构造时检查文件是否存在
