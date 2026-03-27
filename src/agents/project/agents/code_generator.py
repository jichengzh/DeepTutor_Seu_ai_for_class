# -*- coding: utf-8 -*-
"""
CodeGenerator — 三阶段代码仓库生成器。

认证：通过 subprocess 调用本机 claude CLI，复用 ~/.claude/.credentials.json
中的 OAuth Token（Claude Pro 订阅）。不注入 ANTHROPIC_API_KEY，不指定模型，
DeepTutor 自动跟随 Claude Code 当前配置的模型。
"""

import asyncio
import json
import os
import shutil
from pathlib import Path
from typing import Callable

from src.agents.project.agents.requirement_extractor import RequirementSpec
from src.logging.logger import get_logger

logger = get_logger("CodeGenerator")

DIFFICULTY_LIMITS: dict[str, dict[str, int]] = {
    "low":    {"max_py_files": 10,  "max_lines": 3000},
    "medium": {"max_py_files": 25,  "max_lines": 7000},
    "high":   {"max_py_files": 40,  "max_lines": 10000},
}


def _find_claude_bin() -> str:
    """查找 claude CLI 路径：优先 PATH，降级到 ~/.local/bin/claude。"""
    found = shutil.which("claude")
    if found:
        return found
    fallback = os.path.expanduser("~/.local/bin/claude")
    return fallback


def _find_codex_bin() -> str:
    """查找 codex CLI 路径：优先 PATH，降级到 VSCode 扩展路径。"""
    found = shutil.which("codex")
    if found:
        return found
    # 搜索 VSCode Server 扩展中的 codex 二进制（版本号可能更新）
    import glob as _glob
    candidates = sorted(
        _glob.glob(os.path.expanduser(
            "~/.vscode-server/extensions/openai.chatgpt-*/bin/linux-x86_64/codex"
        )),
        reverse=True,
    )
    if candidates:
        return candidates[0]
    return os.path.expanduser("~/.nvm/versions/node/v20.20.1/bin/codex")


# ── 模块级 prompt 构建函数（供 CodeGenerator 和 CodexGenerator 共用）─────────

def _flatten_str_list(items: list) -> list[str]:
    """将可能嵌套的列表扁平化为字符串列表，防止 LLM 返回嵌套列表导致 join 报错。"""
    result: list[str] = []
    for item in items:
        if isinstance(item, list):
            result.extend(_flatten_str_list(item))
        else:
            result.append(str(item))
    return result


def _build_planning_prompt(spec: "RequirementSpec", difficulty: str = "medium") -> str:
    modules_desc = "\n".join(
        f"- 模块 {m.id}「{m.title}」：{'; '.join(_flatten_str_list(m.objectives))}"
        for m in spec.modules
    )
    limits = DIFFICULTY_LIMITS.get(difficulty, DIFFICULTY_LIMITS["medium"])
    difficulty_label = {"low": "低", "medium": "中", "high": "高"}.get(difficulty, difficulty)
    return (
        f"请为以下项目制定详细的代码架构计划，只创建一个文件 PLAN.md。\n\n"
        f"项目主题：{spec.theme}\n"
        f"技术栈：{', '.join(_flatten_str_list(spec.tech_stack))}\n"
        f"运行环境：{spec.environment}\n\n"
        f"需要覆盖的模块：\n{modules_desc}\n\n"
        "请在 PLAN.md 中包含：\n"
        "1. 项目目录结构（完整文件树，含每个文件的用途说明）\n"
        "2. 技术选型说明\n"
        "3. 各模块与代码文件的对应关系（模块 ID → 文件列表）\n"
        "4. 主要类/函数接口定义\n"
        "5. 运行方式（入口命令）\n\n"
        "只创建 PLAN.md，不创建其他文件。\n\n"
        f"=== 强制约束（难度：{difficulty_label}级） ===\n"
        f"1. 编程语言：仅允许 Python（.py）和 Shell 脚本（.sh/.bash）\n"
        f"   - 禁止生成任何 C/C++/Java/Rust/Go 源文件\n"
        f"   - 所有功能必须用 Python 实现\n"
        f"2. 规模限制：\n"
        f"   - Python 文件总数 ≤ {limits['max_py_files']} 个\n"
        f"   - 全部代码总行数 ≤ {limits['max_lines']} 行\n"
        f"   - 请在目录结构设计时就控制文件数量，不要过度拆分模块\n"
    )


def _build_coding_prompt(spec: "RequirementSpec", plan_content: str, difficulty: str = "medium") -> str:
    modules_checklist = "\n".join(
        f"- [ ] 模块 {m.id}「{m.title}」\n"
        f"      技术要求：{'; '.join(_flatten_str_list(m.technical_requirements))}\n"
        f"      交付物：{'; '.join(_flatten_str_list(m.deliverables))}"
        for m in spec.modules
    )
    limits = DIFFICULTY_LIMITS.get(difficulty, DIFFICULTY_LIMITS["medium"])
    difficulty_label = {"low": "低", "medium": "中", "high": "高"}.get(difficulty, difficulty)
    return (
        "请根据以下项目计划，完整实现所有代码文件。\n\n"
        f"=== 架构计划（PLAN.md）===\n{plan_content}\n\n"
        f"=== 需要覆盖的模块 ===\n{modules_checklist}\n\n"
        "实现要求：\n"
        "1. 先执行 `git init` 初始化仓库\n"
        "2. 按 PLAN.md 的目录结构创建所有文件\n"
        "3. 每个模块必须对应至少一个实现文件，不允许遗漏\n"
        "4. README.md 必须包含：\n"
        "   a. 项目介绍（与任务书对应）\n"
        "   b. 环境要求（Python 版本、OS）\n"
        "   c. 安装步骤（逐行可执行命令）\n"
        "   d. 运行示例（带预期输出）\n"
        "   e. 项目结构说明\n"
        "   f. 各模块功能说明\n"
        "5. requirements.txt 列出所有依赖（含版本号）\n"
        "6. 代码须有注释，关键函数须有 docstring\n"
        "7. 每个模块包含基础单元测试（tests/ 目录）\n"
        "8. 全部写完后，运行 `pip install -r requirements.txt` 并验证主模块可导入\n"
        "9. 如发现错误立即修复，确保最终状态代码可运行\n\n"
        f"技术栈：{', '.join(_flatten_str_list(spec.tech_stack))}\n"
        f"环境：{spec.environment}\n\n"
        f"=== 强制约束（难度：{difficulty_label}级） ===\n"
        f"1. 编程语言：仅允许 Python（.py）和 Shell 脚本（.sh/.bash）\n"
        f"   - 禁止生成任何 C/C++/Java/Rust/Go 源文件\n"
        f"   - 所有功能必须用 Python 实现\n"
        f"2. 规模限制：\n"
        f"   - Python 文件总数 ≤ {limits['max_py_files']} 个\n"
        f"   - 全部代码总行数 ≤ {limits['max_lines']} 行\n"
        f"   - 每个函数不超过 50 行，优先用标准库\n"
        f"3. 完成后运行以下命令自检：\n"
        f"   find . -name '*.py' | wc -l   # 应 ≤ {limits['max_py_files']}\n"
        f"   cat $(find . -name '*.py') | wc -l   # 应 ≤ {limits['max_lines']}\n"
        f"   如超出限制，请合并文件或精简代码直到满足要求\n"
    )


def _build_coverage_map(spec: "RequirementSpec", repo_dir: Path) -> dict:
    all_files = [
        str(p.relative_to(repo_dir))
        for p in repo_dir.rglob("*")
        if p.is_file() and ".git" not in p.parts
    ]
    coverage: dict[str, list[str]] = {}
    for module in spec.modules:
        keywords = [module.id.lower(), module.title.lower().replace(" ", "_")]
        matched = [f for f in all_files if any(kw in f.lower() for kw in keywords)]
        coverage[module.id] = matched or ["(请查看 PLAN.md 了解文件映射)"]
    return coverage


def _build_file_tree(base_dir: Path) -> list:
    result = []
    try:
        items = sorted(base_dir.iterdir(), key=lambda p: (p.is_file(), p.name))
    except PermissionError:
        return result
    for item in items:
        if item.name.startswith(".git"):
            continue
        node: dict = {
            "name": item.name,
            "path": str(item.relative_to(base_dir)),
            "type": "directory" if item.is_dir() else "file",
        }
        if item.is_dir():
            node["children"] = _build_file_tree(item)
        result.append(node)
    return result


class CodeGenerator:
    """
    三阶段代码仓库生成器：
      B. 架构规划 — claude CLI 只写 PLAN.md
      C. 代码生成 — claude CLI 按 PLAN.md 实现全部代码
      D. 错误修复 — 验证失败时最多重试 2 次
    """

    CLAUDE_BIN: str = _find_claude_bin()

    def __init__(self) -> None:
        if not os.path.isfile(self.CLAUDE_BIN):
            raise EnvironmentError(
                f"找不到 claude CLI（{self.CLAUDE_BIN}）。"
                "请运行：npm install -g @anthropic-ai/claude-code && claude login"
            )

    # ── public ───────────────────────────────────────────────────────────────

    async def generate(
        self,
        spec: RequirementSpec,
        output_dir: str,
        ws_callback: Callable,
        difficulty: str = "medium",
    ) -> dict:
        """
        执行 B→C→D 三个阶段，返回：
          {"repo_path", "file_tree", "coverage_map", "verify_passed", "verify_report"}
        """
        from src.agents.project.agents.code_verifier import CodeVerifier

        repo_dir = Path(output_dir) / "repo"
        repo_dir.mkdir(parents=True, exist_ok=True)

        # 阶段 B：架构规划
        await ws_callback({"type": "phase", "phase": "planning", "content": "正在制定项目架构..."})
        await self._run_planning_phase(spec, repo_dir, ws_callback, difficulty)

        # 阶段 C：代码生成
        await ws_callback({"type": "phase", "phase": "coding", "content": "Claude Agent 开始编写代码..."})
        await self._run_coding_phase(spec, repo_dir, ws_callback, difficulty)

        # 阶段 D：验证（最多修复 2 次）
        await ws_callback({"type": "phase", "phase": "verify", "content": "正在验证代码可运行性..."})
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

        # 覆盖率映射
        coverage_map = self._build_coverage_map(spec, repo_dir)
        await ws_callback({"type": "coverage", "map": coverage_map})

        return {
            "repo_path": str(repo_dir),
            "file_tree": self._build_file_tree(repo_dir),
            "coverage_map": coverage_map,
            "verify_passed": verify_result["passed"],
            "verify_report": verify_result["report"],
        }

    # ── phases ────────────────────────────────────────────────────────────────

    async def _run_planning_phase(
        self, spec: RequirementSpec, repo_dir: Path, ws_callback: Callable,
        difficulty: str = "medium",
    ) -> None:
        prompt = self._build_planning_prompt(spec, difficulty)
        await self._run_agent(prompt, str(repo_dir), ["Write"], ws_callback)

        if not (repo_dir / "PLAN.md").exists():
            # 若 claude 没有生成 PLAN.md，写一个最小占位
            (repo_dir / "PLAN.md").write_text(
                f"# {spec.theme} 项目计划\n\n技术栈：{', '.join(spec.tech_stack)}\n",
                encoding="utf-8",
            )
            await ws_callback({"type": "status", "content": "PLAN.md 未生成，使用最小占位"})

    async def _run_coding_phase(
        self, spec: RequirementSpec, repo_dir: Path, ws_callback: Callable,
        difficulty: str = "medium",
    ) -> None:
        plan_path = repo_dir / "PLAN.md"
        plan_content = plan_path.read_text(encoding="utf-8") if plan_path.exists() else ""
        prompt = self._build_coding_prompt(spec, plan_content, difficulty)
        await self._run_agent(
            prompt, str(repo_dir), ["Read", "Write", "Edit", "Bash", "Glob"], ws_callback
        )

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
        await self._run_agent(
            prompt, str(repo_dir), ["Read", "Write", "Edit", "Bash"], ws_callback
        )

    # ── claude CLI subprocess ─────────────────────────────────────────────────

    async def _run_agent(
        self,
        prompt: str,
        cwd: str,
        allowed_tools: list[str],
        ws_callback: Callable,
    ) -> None:
        """
        调用本机 claude CLI subprocess。
        关键：env 原样传递 os.environ，不注入 ANTHROPIC_API_KEY，
        确保 claude 使用 ~/.claude/.credentials.json 的 OAuth 认证。
        不指定 --model，使用 Claude Code 当前配置的模型。
        """
        process = await asyncio.create_subprocess_exec(
            self.CLAUDE_BIN,
            "-p", prompt,
            "--allowedTools", ",".join(allowed_tools),
            "--output-format", "stream-json",
            "--verbose",
            cwd=cwd,
            env=dict(os.environ),  # uvloop requires plain dict, not _Environ
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )

        assert process.stdout is not None
        async for raw_line in process.stdout:
            line = raw_line.decode(errors="replace").strip()
            if not line:
                continue
            try:
                await self._handle_cli_event(json.loads(line), ws_callback)
            except json.JSONDecodeError:
                pass

        await process.wait()

        if process.returncode not in (0, 1, None):
            # 退出码 1 在 claude CLI 中有时表示"任务完成但有警告"，不视为致命错误
            assert process.stderr is not None
            stderr_bytes = await process.stderr.read()
            stderr_text = stderr_bytes.decode(errors="replace")[:500]
            raise RuntimeError(
                f"claude CLI 退出码 {process.returncode}: {stderr_text}"
            )
        elif process.returncode == 1:
            assert process.stderr is not None
            stderr_bytes = await process.stderr.read()
            stderr_text = stderr_bytes.decode(errors="replace")[:200]
            logger.warning(f"claude CLI 退出码 1（非致命）: {stderr_text[:200]}")

    async def _handle_cli_event(self, event: dict, ws_callback: Callable) -> None:
        """将 claude CLI stream-json 事件转换为前端 agent_log 格式。

        claude CLI v2.1.81 stream-json 格式：
          {"type":"assistant","message":{"content":[{"type":"tool_use","name":"Write",...}]}}
        工具调用结果：
          {"type":"tool_result","tool_use_id":"...","content":"..."}  (顶层，部分版本)
          或嵌套在 user message 中
        """
        event_type = event.get("type", "")

        if event_type == "tool_use":
            # 旧格式（保留兼容）
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
            content = str(event.get("content", ""))[:300]
            await ws_callback({
                "type": "agent_log",
                "log_type": "tool_result",
                "tool": None,
                "path": "",
                "content": content,
            })

        elif event_type in ("message", "assistant"):
            # 新格式：工具调用和文本都在 message.content 数组中
            msg = event.get("message", event)
            for block in msg.get("content", []):
                if not isinstance(block, dict):
                    continue
                block_type = block.get("type", "")
                if block_type == "tool_use":
                    name = block.get("name", "")
                    inp = block.get("input", {})
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
                elif block_type == "text":
                    text = block["text"][:500]
                    await ws_callback({
                        "type": "agent_log",
                        "log_type": "message",
                        "tool": None,
                        "path": "",
                        "content": text,
                    })

    # ── prompt builders（委托给模块级函数）────────────────────────────────────

    def _build_planning_prompt(self, spec: RequirementSpec, difficulty: str = "medium") -> str:
        return _build_planning_prompt(spec, difficulty)

    def _build_coding_prompt(self, spec: RequirementSpec, plan_content: str, difficulty: str = "medium") -> str:
        return _build_coding_prompt(spec, plan_content, difficulty)

    # ── helpers（委托给模块级函数）────────────────────────────────────────────

    def _build_coverage_map(self, spec: RequirementSpec, repo_dir: Path) -> dict:
        return _build_coverage_map(spec, repo_dir)

    def _build_file_tree(self, base_dir: Path) -> list:
        return _build_file_tree(base_dir)


# ==============================================================================
# CodexGenerator — 使用 Codex CLI 的三阶段代码生成器
# ==============================================================================

class CodexGenerator:
    """
    使用 Codex CLI 的三阶段代码生成器。
    认证：优先使用传入的 api_key；为 None 时依赖 ~/.codex/ OAuth token（codex login）。
    Codex 输出 plain text，不支持 stream-json，直接逐行作为 agent_log 推送。
    """

    CODEX_BIN: str = _find_codex_bin()

    def __init__(self, api_key: str | None = None) -> None:
        if not os.path.isfile(self.CODEX_BIN):
            raise EnvironmentError(
                f"找不到 codex CLI（{self.CODEX_BIN}）。"
                "请运行：npm install -g @openai/codex && codex login"
            )
        self._api_key = api_key

    # ── public ───────────────────────────────────────────────────────────────

    async def generate(
        self,
        spec: RequirementSpec,
        output_dir: str,
        ws_callback: Callable,
        difficulty: str = "medium",
    ) -> dict:
        """执行 B→C→D 三个阶段，返回与 CodeGenerator 相同的结构。"""
        from src.agents.project.agents.code_verifier import CodeVerifier

        repo_dir = Path(output_dir) / "repo"
        repo_dir.mkdir(parents=True, exist_ok=True)

        # 阶段 B：架构规划
        await ws_callback({"type": "phase", "phase": "planning", "content": "Codex 正在制定项目架构..."})
        await self._run_planning_phase(spec, repo_dir, ws_callback, difficulty)

        # 阶段 C：代码生成
        await ws_callback({"type": "phase", "phase": "coding", "content": "Codex Agent 开始编写代码..."})
        await self._run_coding_phase(spec, repo_dir, ws_callback, difficulty)

        # 阶段 D：验证（最多修复 2 次）
        await ws_callback({"type": "phase", "phase": "verify", "content": "正在验证代码可运行性..."})
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

        coverage_map = _build_coverage_map(spec, repo_dir)
        await ws_callback({"type": "coverage", "map": coverage_map})

        return {
            "repo_path": str(repo_dir),
            "file_tree": _build_file_tree(repo_dir),
            "coverage_map": coverage_map,
            "verify_passed": verify_result["passed"],
            "verify_report": verify_result["report"],
        }

    # ── phases ────────────────────────────────────────────────────────────────

    async def _run_planning_phase(
        self, spec: RequirementSpec, repo_dir: Path, ws_callback: Callable,
        difficulty: str = "medium",
    ) -> None:
        prompt = _build_planning_prompt(spec, difficulty)
        await self._run_agent(prompt, str(repo_dir), ws_callback)

        if not (repo_dir / "PLAN.md").exists():
            (repo_dir / "PLAN.md").write_text(
                f"# {spec.theme} 项目计划\n\n技术栈：{', '.join(spec.tech_stack)}\n",
                encoding="utf-8",
            )
            await ws_callback({"type": "status", "content": "PLAN.md 未生成，使用最小占位"})

    async def _run_coding_phase(
        self, spec: RequirementSpec, repo_dir: Path, ws_callback: Callable,
        difficulty: str = "medium",
    ) -> None:
        plan_path = repo_dir / "PLAN.md"
        plan_content = plan_path.read_text(encoding="utf-8") if plan_path.exists() else ""
        prompt = _build_coding_prompt(spec, plan_content, difficulty)
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

    # ── codex subprocess ──────────────────────────────────────────────────────

    async def _run_agent(
        self,
        prompt: str,
        cwd: str,
        ws_callback: Callable,
    ) -> None:
        """
        调用 codex CLI subprocess（非交互式 exec 子命令）。
        认证：api_key 不为 None 时注入 OPENAI_API_KEY；
              否则尝试 ~/.codex/auth.json；
              最终依赖环境变量或 codex login OAuth token。
        使用 --json 获取 JSONL 事件流。
        """
        env = dict(os.environ)
        if self._api_key:
            env["OPENAI_API_KEY"] = self._api_key
        elif not env.get("OPENAI_API_KEY"):
            auth_file = Path.home() / ".codex" / "auth.json"
            if auth_file.exists():
                try:
                    auth_data = json.loads(auth_file.read_text())
                    key = auth_data.get("OPENAI_API_KEY") or auth_data.get("api_key")
                    if key:
                        env["OPENAI_API_KEY"] = key
                except Exception:
                    pass

        process = await asyncio.create_subprocess_exec(
            self.CODEX_BIN,
            "exec",              # 非交互式子命令
            "--json",            # JSONL 事件流输出
            "--color", "never",  # 禁用 ANSI 颜色转义码
            "-s", "workspace-write",  # 允许写入工作目录
            prompt,
            cwd=cwd,
            env=env,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )

        assert process.stdout is not None
        async for raw_line in process.stdout:
            line = raw_line.decode(errors="replace").strip()
            if not line:
                continue
            try:
                event = json.loads(line)
                await self._handle_codex_event(event, ws_callback)
            except json.JSONDecodeError:
                # 纯文本输出（进度信息、警告等）
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

    async def _handle_codex_event(self, event: dict, ws_callback: Callable) -> None:
        """将 Codex JSONL 事件转换为前端 agent_log 格式。"""
        event_type = event.get("type", "")
        if event_type == "message":
            content = str(event.get("content", ""))[:500]
            await ws_callback({
                "type": "agent_log", "log_type": "message",
                "tool": None, "path": "", "content": content,
            })
        elif event_type in ("exec", "shell"):
            cmd = event.get("command") or event.get("cmd") or str(event)[:200]
            await ws_callback({
                "type": "agent_log", "log_type": "tool_use",
                "tool": "Bash", "path": "", "content": f"$ {cmd}",
            })
        elif event_type == "patch":
            for f in event.get("files", []):
                path = f.get("path", "") if isinstance(f, dict) else str(f)
                await ws_callback({
                    "type": "agent_log", "log_type": "tool_use",
                    "tool": "Write", "path": path, "content": f"Write: {path}",
                })
                if path:
                    await ws_callback({"type": "file_created", "path": path})
        else:
            await ws_callback({
                "type": "agent_log", "log_type": "text",
                "tool": None, "path": "", "content": str(event)[:300],
            })
