# -*- coding: utf-8 -*-
"""
Unit tests for Phase 3 backend:
  - RequirementExtractor
  - CodeVerifier
  - CodeGenerator (unit-level, no real claude CLI call)
"""

import asyncio
import json
import os
import shutil
from pathlib import Path
from unittest.mock import AsyncMock, MagicMock, patch

import pytest

from src.agents.project.agents.requirement_extractor import (
    RequirementExtractor,
    RequirementSpec,
    TaskModule,
)
from src.agents.project.agents.code_verifier import CodeVerifier
from src.agents.project.agents.code_generator import CodeGenerator, CodexGenerator, _find_codex_bin


# ── RequirementExtractor ──────────────────────────────────────────────────────


class TestRequirementExtractor:
    @pytest.mark.asyncio
    async def test_extract_valid_json(self):
        """LLM 返回合法 JSON → 正确解析为 RequirementSpec"""
        valid_json = json.dumps({
            "theme": "ROS2 导航实习",
            "tech_stack": ["Python", "ROS2"],
            "environment": "Python 3.10 / Ubuntu 22.04",
            "install_requirements": ["rclpy", "numpy"],
            "modules": [
                {
                    "id": "module_1",
                    "title": "环境搭建",
                    "objectives": ["安装 ROS2"],
                    "technical_requirements": ["Ubuntu 22.04"],
                    "deliverables": ["安装截图"],
                }
            ],
        })
        extractor = RequirementExtractor()
        with patch("src.agents.project.agents.requirement_extractor.llm_factory") as mock:
            mock.complete = AsyncMock(return_value=valid_json)
            spec = await extractor.extract("任务书内容", AsyncMock())

        assert spec.theme == "ROS2 导航实习"
        assert spec.tech_stack == ["Python", "ROS2"]
        assert len(spec.modules) == 1
        assert spec.modules[0].id == "module_1"
        assert spec.modules[0].title == "环境搭建"

    @pytest.mark.asyncio
    async def test_extract_json_wrapped_in_text(self):
        """LLM 在 JSON 前后有多余文字 → 仍能正确提取"""
        response = "这是分析结果：\n" + json.dumps({
            "theme": "ROS2",
            "tech_stack": ["Python"],
            "environment": "Ubuntu 22.04",
            "install_requirements": [],
            "modules": [],
        }) + "\n以上就是提取结果。"
        extractor = RequirementExtractor()
        with patch("src.agents.project.agents.requirement_extractor.llm_factory") as mock:
            mock.complete = AsyncMock(return_value=response)
            spec = await extractor.extract("任务书", AsyncMock())

        assert spec.theme == "ROS2"

    @pytest.mark.asyncio
    async def test_extract_invalid_json_returns_minimal_spec(self):
        """LLM 返回无效 JSON → 返回最小化 RequirementSpec，不抛异常"""
        extractor = RequirementExtractor()
        with patch("src.agents.project.agents.requirement_extractor.llm_factory") as mock:
            mock.complete = AsyncMock(return_value="这不是JSON内容")
            spec = await extractor.extract("任务书", AsyncMock())

        assert isinstance(spec, RequirementSpec)
        assert spec.modules == []

    @pytest.mark.asyncio
    async def test_extract_llm_exception_returns_minimal_spec(self):
        """LLM 调用抛异常 → 返回最小化 RequirementSpec，不向上传播"""
        extractor = RequirementExtractor()
        with patch("src.agents.project.agents.requirement_extractor.llm_factory") as mock:
            mock.complete = AsyncMock(side_effect=RuntimeError("network error"))
            spec = await extractor.extract("任务书", AsyncMock())

        assert isinstance(spec, RequirementSpec)

    @pytest.mark.asyncio
    async def test_extract_sends_phase_and_status_messages(self):
        """extract() 必须发送 phase=analysis 和 status 两条回调消息"""
        messages = []
        async def cb(msg): messages.append(msg)

        extractor = RequirementExtractor()
        with patch("src.agents.project.agents.requirement_extractor.llm_factory") as mock:
            mock.complete = AsyncMock(return_value=json.dumps({
                "theme": "Test", "tech_stack": [], "environment": "",
                "install_requirements": [], "modules": [],
            }))
            await extractor.extract("任务书", cb)

        types = [m.get("type") for m in messages]
        phases = [m.get("phase") for m in messages if m.get("type") == "phase"]
        assert "analysis" in phases
        assert "status" in types


# ── CodeVerifier ──────────────────────────────────────────────────────────────


class TestCodeVerifier:
    @pytest.mark.asyncio
    async def test_verify_empty_repo_passes(self, tmp_path):
        """空仓库（无 requirements.txt、无主模块、无 tests/）→ 通过"""
        v = CodeVerifier(tmp_path, AsyncMock())
        result = await v.verify()
        assert result["passed"] is True

    @pytest.mark.asyncio
    async def test_detect_main_py(self, tmp_path):
        """存在 main.py → 检测到主模块 'main'"""
        (tmp_path / "main.py").write_text("def main(): pass\n")
        v = CodeVerifier(tmp_path, AsyncMock())
        assert v._detect_main_module() == "main"

    @pytest.mark.asyncio
    async def test_detect_app_py(self, tmp_path):
        """存在 app.py → 检测到主模块 'app'"""
        (tmp_path / "app.py").write_text("x = 1\n")
        v = CodeVerifier(tmp_path, AsyncMock())
        assert v._detect_main_module() == "app"

    @pytest.mark.asyncio
    async def test_detect_src_package(self, tmp_path):
        """存在 src/__init__.py → 检测到主模块 'src'"""
        (tmp_path / "src").mkdir()
        (tmp_path / "src" / "__init__.py").write_text("")
        v = CodeVerifier(tmp_path, AsyncMock())
        assert v._detect_main_module() == "src"

    @pytest.mark.asyncio
    async def test_detect_no_main_returns_none(self, tmp_path):
        """无主模块文件 → 返回 None"""
        v = CodeVerifier(tmp_path, AsyncMock())
        assert v._detect_main_module() is None

    @pytest.mark.asyncio
    async def test_verify_valid_main_py_passes(self, tmp_path):
        """main.py 语法正确可导入 → 验证通过"""
        (tmp_path / "main.py").write_text("def hello(): return 'hi'\n")
        v = CodeVerifier(tmp_path, AsyncMock())
        result = await v.verify()
        assert result["passed"] is True

    @pytest.mark.asyncio
    async def test_verify_syntax_error_fails(self, tmp_path):
        """main.py 有语法错误 → 验证失败"""
        (tmp_path / "main.py").write_text("def bad syntax (\n")
        v = CodeVerifier(tmp_path, AsyncMock())
        result = await v.verify()
        assert result["passed"] is False


# ── CodeGenerator (unit-level) ────────────────────────────────────────────────


class TestCodeGeneratorUnit:
    def test_missing_claude_bin_raises(self, monkeypatch):
        """claude CLI 不存在 → 构造函数抛 EnvironmentError"""
        monkeypatch.setattr(CodeGenerator, "CLAUDE_BIN", "/nonexistent/path/claude")
        with pytest.raises(EnvironmentError, match="claude CLI"):
            CodeGenerator()

    def test_constructor_succeeds_when_bin_exists(self, monkeypatch, tmp_path):
        """claude CLI 存在 → 构造函数不抛异常"""
        fake_bin = tmp_path / "claude"
        fake_bin.touch()
        monkeypatch.setattr(CodeGenerator, "CLAUDE_BIN", str(fake_bin))
        gen = CodeGenerator()
        assert gen is not None

    def test_build_coverage_map_with_matching_files(self, monkeypatch, tmp_path):
        """文件名含模块 ID → 覆盖率映射含该文件"""
        fake_bin = tmp_path / "claude"
        fake_bin.touch()
        monkeypatch.setattr(CodeGenerator, "CLAUDE_BIN", str(fake_bin))
        gen = CodeGenerator()

        (tmp_path / "module_1.py").write_text("")
        (tmp_path / "module_2_helper.py").write_text("")

        spec = MagicMock()
        spec.modules = [
            MagicMock(id="module_1", title="Test Module"),
            MagicMock(id="module_2", title="Another Module"),
        ]
        cmap = gen._build_coverage_map(spec, tmp_path)

        assert any("module_1.py" in f for f in cmap["module_1"])
        assert any("module_2" in f for f in cmap["module_2"])

    def test_build_coverage_map_no_match_returns_placeholder(self, monkeypatch, tmp_path):
        """无匹配文件 → 覆盖率值含占位文本"""
        fake_bin = tmp_path / "claude"
        fake_bin.touch()
        monkeypatch.setattr(CodeGenerator, "CLAUDE_BIN", str(fake_bin))
        gen = CodeGenerator()

        spec = MagicMock()
        spec.modules = [MagicMock(id="module_99", title="Nonexistent")]
        cmap = gen._build_coverage_map(spec, tmp_path)

        assert "PLAN.md" in cmap["module_99"][0] or "查看" in cmap["module_99"][0]

    def test_build_file_tree_excludes_git(self, monkeypatch, tmp_path):
        """文件树排除 .git 目录"""
        fake_bin = tmp_path / "claude"
        fake_bin.touch()
        monkeypatch.setattr(CodeGenerator, "CLAUDE_BIN", str(fake_bin))
        gen = CodeGenerator()

        (tmp_path / ".git").mkdir()
        (tmp_path / "main.py").write_text("")

        tree = gen._build_file_tree(tmp_path)
        names = [n["name"] for n in tree]
        assert ".git" not in names
        assert "main.py" in names


# ── CodexGenerator (unit-level) ───────────────────────────────────────────────


class TestCodexGeneratorUnit:
    def test_find_codex_bin_finds_vscode_extension(self):
        """实际环境中能找到 VSCode 扩展中的 codex 二进制"""
        bin_path = _find_codex_bin()
        assert os.path.isfile(bin_path), f"codex 未找到: {bin_path}"

    def test_missing_codex_bin_raises(self, monkeypatch):
        """codex 二进制不存在 → 构造函数抛 EnvironmentError"""
        monkeypatch.setattr(CodexGenerator, "CODEX_BIN", "/nonexistent/codex")
        with pytest.raises(EnvironmentError, match="codex CLI"):
            CodexGenerator()

    def test_constructor_succeeds_when_bin_exists(self, monkeypatch, tmp_path):
        """codex CLI 存在 → 构造函数不抛异常"""
        fake_bin = tmp_path / "codex"
        fake_bin.touch()
        monkeypatch.setattr(CodexGenerator, "CODEX_BIN", str(fake_bin))
        gen = CodexGenerator()
        assert gen is not None

    @pytest.mark.asyncio
    async def test_run_agent_uses_exec_subcommand(self, monkeypatch, tmp_path):
        """_run_agent 必须使用 exec 子命令，不使用旧的 -q/-a 标志"""
        fake_bin = tmp_path / "codex"
        fake_bin.touch()
        monkeypatch.setattr(CodexGenerator, "CODEX_BIN", str(fake_bin))
        gen = CodexGenerator()

        captured: dict = {}

        async def fake_exec(*args, **kwargs):
            captured["args"] = list(args)

            async def _empty_aiter():
                return
                yield  # make it an async generator

            mock_proc = MagicMock()
            mock_proc.stdout = _empty_aiter()
            mock_proc.returncode = 0
            mock_proc.wait = AsyncMock()
            mock_proc.stderr.read = AsyncMock(return_value=b"")
            return mock_proc

        with patch("asyncio.create_subprocess_exec", fake_exec):
            await gen._run_agent("test prompt", str(tmp_path), AsyncMock())

        assert "exec" in captured["args"]
        assert "--json" in captured["args"]
        assert "-s" in captured["args"]
        assert "workspace-write" in captured["args"]
        assert "-q" not in captured["args"]
        assert "-a" not in captured["args"]

    @pytest.mark.asyncio
    async def test_handle_codex_event_message(self, monkeypatch, tmp_path):
        """message 类型事件 → agent_log message"""
        fake_bin = tmp_path / "codex"
        fake_bin.touch()
        monkeypatch.setattr(CodexGenerator, "CODEX_BIN", str(fake_bin))
        gen = CodexGenerator()

        logs: list = []
        async def cb(msg): logs.append(msg)

        await gen._handle_codex_event({"type": "message", "content": "Hello"}, cb)
        assert any(l.get("log_type") == "message" for l in logs)

    @pytest.mark.asyncio
    async def test_handle_codex_event_patch_emits_file_created(self, monkeypatch, tmp_path):
        """patch 类型事件 → file_created 回调"""
        fake_bin = tmp_path / "codex"
        fake_bin.touch()
        monkeypatch.setattr(CodexGenerator, "CODEX_BIN", str(fake_bin))
        gen = CodexGenerator()

        logs: list = []
        async def cb(msg): logs.append(msg)

        await gen._handle_codex_event(
            {"type": "patch", "files": [{"path": "main.py"}]}, cb
        )
        types = [l.get("type") for l in logs]
        assert "file_created" in types
