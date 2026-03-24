"""Tests for TaskGenerator."""

import asyncio
from pathlib import Path
from unittest.mock import patch

import pytest


@pytest.mark.asyncio
async def test_generate_without_rag_or_search(tmp_path):
    """无 RAG 无 Web 搜索时能完成生成流程。"""
    from src.agents.project.agents.task_generator import TaskGenerator

    logs = []

    async def fake_callback(msg):
        logs.append(msg)

    async def fake_stream(*args, **kwargs):
        yield "## 测试章节内容\n"

    with patch("src.agents.project.agents.task_generator.llm_factory.stream", fake_stream):
        gen = TaskGenerator(output_dir=str(tmp_path), language="zh")
        result = await gen.generate(
            theme="测试主题",
            reference_structure={},
            kb_name=None,
            web_search=False,
            ws_callback=fake_callback,
        )

    assert "content" in result
    assert "md_path" in result
    assert "docx_path" in result
    assert Path(result["md_path"]).exists()
    # 状态消息应提示无外部知识源
    status_msgs = [l["content"] for l in logs if l.get("type") == "status"]
    assert any("无外部知识源" in m or "未选择" in m for m in status_msgs)


@pytest.mark.asyncio
async def test_generate_produces_markdown_with_sections(tmp_path):
    """生成的 markdown 应包含内容。"""
    from src.agents.project.agents.task_generator import TaskGenerator

    async def fake_stream(*args, **kwargs):
        # Return a predictable section header
        prompt = kwargs.get("prompt", args[0] if args else "")
        yield "## 章节内容示例\n这是生成的内容。\n"

    async def noop_callback(msg):
        pass

    with patch("src.agents.project.agents.task_generator.llm_factory.stream", fake_stream):
        gen = TaskGenerator(output_dir=str(tmp_path), language="zh")
        result = await gen.generate(
            theme="ROS 机器人导航",
            reference_structure={},
            kb_name=None,
            web_search=False,
            ws_callback=noop_callback,
        )

    assert len(result["content"]) > 50


@pytest.mark.asyncio
async def test_generate_section_error_continues(tmp_path):
    """某章节生成失败时，不中断整体流程。"""
    from src.agents.project.agents.task_generator import TaskGenerator

    call_count = 0

    async def flaky_stream(*args, **kwargs):
        nonlocal call_count
        call_count += 1
        if call_count == 3:  # 第 3 个章节抛异常
            raise RuntimeError("simulated LLM error")
        yield "内容\n"

    logs = []

    async def fake_callback(msg):
        logs.append(msg)

    with patch("src.agents.project.agents.task_generator.llm_factory.stream", flaky_stream):
        gen = TaskGenerator(output_dir=str(tmp_path), language="zh")
        result = await gen.generate(
            theme="测试",
            reference_structure={},
            kb_name=None,
            web_search=False,
            ws_callback=fake_callback,
        )

    # 应能完成并返回结果
    assert "content" in result
    # 错误日志应记录
    error_logs = [l for l in logs if l.get("type") == "log" and "出错" in l.get("content", "")]
    assert len(error_logs) >= 1
