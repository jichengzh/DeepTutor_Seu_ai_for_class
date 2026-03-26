# CLAUDE_codex.md — Codex CLI 集成实现指南

## 功能目标

在 Project Creator 代码生成流程中增加 **Codex CLI** 作为备选工具，
允许用户在 Step 1 界面选择使用 Claude CLI 或 Codex CLI 进行代码构建。
Codex 支持两种认证方式：API Key 和 login OAuth。

---

## 涉及文件

| 文件 | 修改类型 |
|------|---------|
| `src/agents/project/agents/code_generator.py` | 提取共用 prompt 函数，新增 `CodexGenerator` 类 |
| `src/api/routers/project.py` | 解析 `cli_tool`、`codex_api_key`，按需实例化生成器 |
| `web/context/GlobalContext.tsx` | `ProjectState` 增加 `cliTool`、`codexApiKey` 字段 |
| `web/app/project/page.tsx` | Step 1 增加 CLI 选择器 + Codex API Key 输入框 |

---

## 任务执行流程

### 1. 开始前：制定计划

本次改动涉及后端新增一个类 + 前端两处 UI，整体思路：
1. 将 `code_generator.py` 中的 `_build_planning_prompt` / `_build_coding_prompt` 等私有方法提取为**模块级函数**，供 `CodeGenerator` 和 `CodexGenerator` 共用
2. 新增 `CodexGenerator` 类，复用相同的三阶段结构，但 `_run_agent` 调用 `codex -q` 而非 `claude -p`
3. 路由层根据 `cli_tool` 字段选择不同生成器
4. 前端 GlobalContext 增加两个字段，page.tsx 增加 UI

### 2. 编码中：只做必要的事

- **不** 修改 `CodeGenerator` 原有逻辑，仅提取共用函数
- **不** 为 Codex 实现 stream-json 解析（Codex 输出 plain text，直接转发即可）
- **不** 添加 Codex 模型选择等额外配置，只做 CLI tool 切换

---

## 详细实现步骤

### Step 1 — `code_generator.py`

#### 1a. 新增 Codex 查找函数

在文件顶部（`_find_claude_bin` 之后）增加：

```python
def _find_codex_bin() -> str:
    """查找 codex CLI 路径：优先 PATH，降级到 nvm 已知路径。"""
    found = shutil.which("codex")
    if found:
        return found
    fallback = os.path.expanduser(
        "~/.nvm/versions/node/v18.20.8/bin/codex"
    )
    return fallback
```

#### 1b. 提取共用 prompt 构建函数（模块级）

将现有 `CodeGenerator._build_planning_prompt`、`_build_coding_prompt`、
`_build_fix_prompt`（若存在）改为模块级函数，同时在 `CodeGenerator` 中
保留原有调用（直接改为调用模块级函数即可，签名不变）。

#### 1c. 新增 `CodexGenerator` 类

```python
class CodexGenerator:
    """
    使用 Codex CLI 的三阶段代码生成器。
    认证：优先使用传入的 api_key；为 None 时依赖 ~/.codex/ OAuth token。
    Codex 输出 plain text，不支持 stream-json，直接作为 agent_log 推送。
    """

    CODEX_BIN: str = _find_codex_bin()

    def __init__(self, api_key: str | None = None) -> None:
        if not os.path.isfile(self.CODEX_BIN):
            raise EnvironmentError(
                f"找不到 codex CLI（{self.CODEX_BIN}）。"
                "请运行：npm install -g @openai/codex && codex login"
            )
        self._api_key = api_key

    # ── public ──────────────────────────────────────────────────────────────

    async def generate(
        self,
        spec: RequirementSpec,
        output_dir: str,
        ws_callback: Callable,
        difficulty: str = "medium",
    ) -> dict:
        from src.agents.project.agents.code_verifier import CodeVerifier

        repo_dir = Path(output_dir) / "repo"
        repo_dir.mkdir(parents=True, exist_ok=True)

        await ws_callback({"type": "phase", "phase": "planning",
                           "content": "Codex 正在制定项目架构..."})
        await self._run_planning_phase(spec, repo_dir, ws_callback, difficulty)

        await ws_callback({"type": "phase", "phase": "coding",
                           "content": "Codex Agent 开始编写代码..."})
        await self._run_coding_phase(spec, repo_dir, ws_callback, difficulty)

        await ws_callback({"type": "phase", "phase": "verify",
                           "content": "正在验证代码可运行性..."})
        verifier = CodeVerifier(repo_dir, ws_callback)
        verify_result = await verifier.verify()

        for attempt in range(2):
            if verify_result["passed"]:
                break
            await ws_callback({
                "type": "status",
                "content": f"检测到错误（第 {attempt + 1} 次修复）：{verify_result['report'][:200]}",
            })
            await self._fix_errors(verify_result["report"], repo_dir, ws_callback)
            verify_result = await verifier.verify()

        await ws_callback({"type": "verify_result", **verify_result})
        coverage_map = _build_coverage_map(spec, repo_dir)   # 模块级函数
        await ws_callback({"type": "coverage", "map": coverage_map})

        return {
            "repo_path": str(repo_dir),
            "file_tree": _build_file_tree(repo_dir),          # 模块级函数
            "coverage_map": coverage_map,
            "verify_passed": verify_result["passed"],
            "verify_report": verify_result["report"],
        }

    # ── phases ───────────────────────────────────────────────────────────────

    async def _run_planning_phase(
        self, spec: RequirementSpec, repo_dir: Path,
        ws_callback: Callable, difficulty: str = "medium",
    ) -> None:
        prompt = _build_planning_prompt(spec, difficulty)   # 模块级
        await self._run_agent(prompt, str(repo_dir), ws_callback)

        if not (repo_dir / "PLAN.md").exists():
            (repo_dir / "PLAN.md").write_text(
                f"# {spec.theme} 项目计划\n\n技术栈：{', '.join(spec.tech_stack)}\n",
                encoding="utf-8",
            )
            await ws_callback({"type": "status", "content": "PLAN.md 未生成，使用最小占位"})

    async def _run_coding_phase(
        self, spec: RequirementSpec, repo_dir: Path,
        ws_callback: Callable, difficulty: str = "medium",
    ) -> None:
        plan_path = repo_dir / "PLAN.md"
        plan_content = plan_path.read_text(encoding="utf-8") if plan_path.exists() else ""
        prompt = _build_coding_prompt(spec, plan_content, difficulty)  # 模块级
        await self._run_agent(prompt, str(repo_dir), ws_callback)

    async def _fix_errors(
        self, error_report: str, repo_dir: Path, ws_callback: Callable
    ) -> None:
        prompt = (
            "请修复以下错误，使项目能够正常运行：\n\n"
            f"{error_report}\n\n"
            "要求：\n"
            "1. 最小化改动，只修复报告中的错误\n"
            "2. 如有依赖缺失，更新 requirements.txt\n"
            "3. 修复后确保主模块可以被 Python 导入\n"
        )
        await self._run_agent(prompt, str(repo_dir), ws_callback)

    # ── codex subprocess ─────────────────────────────────────────────────────

    async def _run_agent(
        self, prompt: str, cwd: str, ws_callback: Callable
    ) -> None:
        """
        调用 codex CLI subprocess。
        认证：api_key 不为 None 时注入 OPENAI_API_KEY；
              否则依赖 ~/.codex/ 存储的 OAuth token（codex login）。
        Codex 不支持 stream-json，逐行将 stdout 作为 agent_log 推送。
        """
        env = dict(os.environ)
        if self._api_key:
            env["OPENAI_API_KEY"] = self._api_key

        process = await asyncio.create_subprocess_exec(
            self.CODEX_BIN,
            "-q", prompt,
            cwd=cwd,
            env=env,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )

        assert process.stdout is not None
        async for raw_line in process.stdout:
            line = raw_line.decode(errors="replace").strip()
            if line:
                await ws_callback({
                    "type": "agent_log",
                    "log_type": "text",
                    "tool": None,
                    "path": "",
                    "content": line,
                })

        await process.wait()
        if process.returncode not in (0, 1, None):
            assert process.stderr is not None
            stderr = (await process.stderr.read()).decode(errors="replace")[:500]
            raise RuntimeError(
                f"codex CLI 退出码 {process.returncode}: {stderr}"
            )
        elif process.returncode == 1:
            assert process.stderr is not None
            stderr = (await process.stderr.read()).decode(errors="replace")[:200]
            logger.warning(f"codex CLI 退出码 1（非致命）: {stderr}")
```

---

### Step 2 — `project.py` 路由

在 `websocket_generate_code` 中，`data` 解析部分增加两行，
并将 `CodeGenerator` 实例化改为按 `cli_tool` 分支：

```python
session_id    = data.get("session_id", "").strip()
task_content  = data.get("task_content", "")
difficulty    = data.get("difficulty", "medium")
cli_tool      = data.get("cli_tool", "claude")       # ← 新增
codex_api_key = data.get("codex_api_key") or None    # ← 新增

# ...（session 校验不变）...

if cli_tool == "codex":
    from src.agents.project.agents.code_generator import CodexGenerator
    try:
        generator = CodexGenerator(api_key=codex_api_key)
    except EnvironmentError as e:
        await websocket.send_json({"type": "error", "content": str(e)})
        return
else:
    from src.agents.project.agents.code_generator import CodeGenerator
    try:
        generator = CodeGenerator()
    except EnvironmentError as e:
        await websocket.send_json({"type": "error", "content": str(e)})
        return
```

---

### Step 3 — `GlobalContext.tsx`

#### 3a. 接口增加字段

```typescript
interface ProjectState {
  // ...existing fields...
  cliTool: "claude" | "codex";
  codexApiKey: string;
}
```

#### 3b. 默认值

```typescript
const DEFAULT_PROJECT_STATE: ProjectState = {
  // ...existing defaults...
  cliTool: "claude",
  codexApiKey: "",
};
```

#### 3c. startCodeGeneration 发送时附带新字段

```typescript
ws.send(JSON.stringify({
  session_id:    state.sessionId,
  task_content:  state.taskContent,
  difficulty:    state.difficulty,
  cli_tool:      state.cliTool,
  codex_api_key: state.codexApiKey || undefined,
}));
```

---

### Step 4 — `page.tsx` Step 1 UI

在难度选择器（`difficulty` 按钮组）的 `</div>` 结束标签**之后**，
生成代码按钮**之前**，插入以下两个块：

```tsx
{/* CLI 工具选择 */}
<div className="mt-3">
  <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">
    代码生成工具
  </label>
  <div className="flex gap-2">
    {(["claude", "codex"] as const).map((tool) => {
      const labels = { claude: "Claude CLI", codex: "Codex CLI" };
      const hints  = { claude: "OAuth 认证（Claude Pro）", codex: "OpenAI Codex" };
      return (
        <button
          key={tool}
          onClick={() => setProjectState(p => ({ ...p, cliTool: tool }))}
          className={cn(
            "flex-1 rounded-lg border px-3 py-2 text-sm text-center transition-colors",
            projectState.cliTool === tool
              ? "border-blue-500 bg-blue-50 text-blue-700 dark:bg-blue-900/30 dark:text-blue-300"
              : "border-gray-200 text-gray-600 hover:border-gray-300 dark:border-gray-600 dark:text-gray-400"
          )}
        >
          <div className="font-medium">{labels[tool]}</div>
          <div className="text-xs text-gray-400 mt-0.5">{hints[tool]}</div>
        </button>
      );
    })}
  </div>
</div>

{/* Codex API Key 输入（仅 Codex 模式显示） */}
{projectState.cliTool === "codex" && (
  <div className="mt-2">
    <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">
      OpenAI API Key（可选，留空则使用 login 认证）
    </label>
    <input
      type="password"
      placeholder="sk-..."
      value={projectState.codexApiKey}
      onChange={(e) => setProjectState(p => ({ ...p, codexApiKey: e.target.value }))}
      className="w-full rounded-lg border border-gray-200 px-3 py-2 text-sm
                 dark:border-gray-600 dark:bg-gray-800 dark:text-gray-200"
    />
  </div>
)}
```

---

## 完成后：边界情况与测试

### 正常路径
- Claude CLI 模式：原有流程不受影响
- Codex + API Key：`OPENAI_API_KEY` 注入 env，生成成功
- Codex + login：不传 key，依赖 `~/.codex/` OAuth token

### 边界值
- `cli_tool` 缺失 → 默认 `"claude"`，行为与旧版一致
- `codex_api_key` 为空字符串 → 等同于 `None`（`or None` 处理）
- Codex CLI 不存在 → `EnvironmentError` 通过 WebSocket 返回 error 消息

### 异常路径
- Codex 退出码非 0/1 → `RuntimeError`，WebSocket 返回 error
- Codex 不生成 PLAN.md → 自动写最小占位（与 Claude 流程一致）
- API Key 无效 → Codex 返回非零退出码，错误信息透传前端

### 验证命令
```bash
# 后端单元测试
python -m pytest tests/test_code_generator.py -v

# 手动验证：确认 CodexGenerator 可实例化
python -c "from src.agents.project.agents.code_generator import CodexGenerator; print(CodexGenerator.CODEX_BIN)"
```

---

## 每次纠错后：反思防范计划

> **反思模板**：[错误原因] → [今后避免的具体做法]

示例（实现过程中如遇错误，在此补充）：
> **反思**：Codex 不支持 `--output-format stream-json`，强制使用导致进程立即退出 →
> 今后集成新 CLI 工具前先确认其输出格式，不假设与 Claude CLI 相同
