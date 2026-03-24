# -*- coding: utf-8 -*-
"""
CodeVerifier — 对生成的代码仓库执行冒烟测试。

检查顺序：
1. pip install -r requirements.txt（如存在）
2. 主模块可导入（python -c "import <main_module>"）
3. pytest tests/（如存在）
"""

import asyncio
from pathlib import Path
from typing import Callable

from src.logging.logger import get_logger

logger = get_logger("CodeVerifier")


class CodeVerifier:
    TIMEOUT_PIP = 120
    TIMEOUT_IMPORT = 30
    TIMEOUT_PYTEST = 60

    def __init__(self, repo_dir: Path, ws_callback: Callable) -> None:
        self.repo_dir = repo_dir
        self.ws_callback = ws_callback

    async def verify(self) -> dict:
        """
        Returns {"passed": bool, "report": str}.
        逐步停止：任意一步失败立即返回。
        """
        # Step 1: pip install
        if (self.repo_dir / "requirements.txt").exists():
            ok, out = await self._run(
                "pip install -r requirements.txt -q", self.TIMEOUT_PIP
            )
            if not ok:
                return {"passed": False, "report": f"依赖安装失败:\n{out}"}

        # Step 2: 主模块导入
        main_module = self._detect_main_module()
        if main_module:
            ok, out = await self._run(
                f'python -c "import {main_module}; print(\'import OK\')"',
                self.TIMEOUT_IMPORT,
            )
            if not ok:
                return {
                    "passed": False,
                    "report": f"主模块导入失败 ({main_module}):\n{out}",
                }

        # Step 3: pytest（如存在）
        if (self.repo_dir / "tests").exists():
            ok, out = await self._run(
                "python -m pytest tests/ -x -q --tb=short 2>&1 | head -50",
                self.TIMEOUT_PYTEST,
            )
            if not ok:
                return {"passed": False, "report": f"测试失败:\n{out}"}

        return {"passed": True, "report": "所有检查通过"}

    # ── private ───────────────────────────────────────────────────────────────

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
            msg = f"命令超时（>{timeout}s）：{cmd}"
            await self.ws_callback({
                "type": "agent_log",
                "log_type": "error",
                "tool": "Bash",
                "path": "",
                "content": msg,
            })
            return False, msg

    def _detect_main_module(self) -> str | None:
        """检测主模块名（main.py / app.py / run.py / src/__init__.py）。"""
        for name in ("main", "app", "run"):
            if (self.repo_dir / f"{name}.py").exists():
                return name
        src = self.repo_dir / "src"
        if src.is_dir() and (src / "__init__.py").exists():
            return "src"
        return None
