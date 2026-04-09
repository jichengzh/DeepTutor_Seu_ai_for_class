"""Tests for Syllabus Generation Mode (课程大纲生成模式)."""

from pathlib import Path
from unittest.mock import patch
import pytest


@pytest.fixture
def syllabus_prompt_zh():
    """Mock syllabus prompt content for Chinese."""
    return {
        "system": "你是一个专业的课程大纲设计专家。",
        "section_prompts": {
            "cover": "生成课程大纲封面信息，主题：{theme}",
            "objectives": "为主题\"{theme}\"生成课程简介与教学目标章节。",
            "prerequisites": "为主题\"{theme}\"生成前置知识要求章节。",
            "content_structure": "为主题\"{theme}\"生成课程内容及学时分配章节。",
            "teaching_methods": "为主题\"{theme}\"生成教学方法与手段章节。",
            "grading_scheme": "为主题\"{theme}\"生成课程考核与成绩评定章节。",
            "teaching_materials": "为主题\"{theme}\"生成教材与参考资料章节。",
            "schedule": "为主题\"{theme}\"生成教学进度安排章节。",
        },
    }


def test_syllabus_initialization(tmp_path, syllabus_prompt_zh):
    """测试课程大纲模式初始化：加载正确的提示词文件和章节顺序。"""
    from src.agents.project.agents.task_generator import (
        TaskGenerator,
        SECTION_ORDER_SYLLABUS_ZH,
    )

    # Mock YAML loading
    with patch(
        "src.agents.project.agents.task_generator.yaml.safe_load",
        return_value=syllabus_prompt_zh,
    ):
        gen = TaskGenerator(output_dir=str(tmp_path), language="zh", mode="syllabus")

        # 验证 mode 正确设置
        assert gen.mode == "syllabus"
        assert gen.language == "zh"

        # 验证章节顺序是课程大纲章节（8个章节）
        assert len(gen._section_order) == 8

        # 验证章节键与预期的课程大纲章节一致
        section_keys = [key for key, _ in gen._section_order]
        expected_keys = [
            "cover",
            "objectives",
            "prerequisites",
            "content_structure",
            "teaching_methods",
            "grading_scheme",
            "teaching_materials",
            "schedule",
        ]
        assert section_keys == expected_keys


def test_syllabus_section_order_differs_from_task():
    """验证课程大纲和任务书使用不同的章节顺序。"""
    from src.agents.project.agents.task_generator import SECTION_ORDER_ZH, SECTION_ORDER_SYLLABUS_ZH

    # 任务书：9个章节
    assert len(SECTION_ORDER_ZH) == 9
    task_keys = [key for key, _ in SECTION_ORDER_ZH]

    # 课程大纲：8个章节
    assert len(SECTION_ORDER_SYLLABUS_ZH) == 8
    syllabus_keys = [key for key, _ in SECTION_ORDER_SYLLABUS_ZH]

    # 验证关键差异
    assert "details" in task_keys  # 任务书有 details 章节
    assert "details" not in syllabus_keys  # 课程大纲没有

    assert "prerequisites" in syllabus_keys  # 课程大纲有 prerequisites 章节
    assert "prerequisites" not in task_keys  # 任务书没有

    assert "teaching_methods" in syllabus_keys  # 课程大纲有 teaching_methods
    assert "teaching_methods" not in task_keys  # 任务书没有


def test_syllabus_generation_with_mock_llm(tmp_path, syllabus_prompt_zh):
    """测试课程大纲生成流程（使用 mock LLM）。"""
    from src.agents.project.agents.task_generator import TaskGenerator

    sections_generated = {}

    async def fake_stream(*args, **kwargs):
        # 模拟每个章节返回一些内容
        prompt = kwargs.get("prompt", args[0] if args else "")
        section_content = f"## 测试课程大纲章节内容\n这是生成的内容。\n"

        # 模拟流式输出
        for chunk in [
            section_content[i : i + 10] for i in range(0, len(section_content), 10)
        ]:
            yield chunk

    logs = []

    async def fake_callback(msg):
        logs.append(msg)
        if msg.get("type") == "section":
            sections_generated[msg.get("section")] = msg.get("content")

    with patch(
        "src.agents.project.agents.task_generator.yaml.safe_load",
        return_value=syllabus_prompt_zh,
    ):
        with patch(
            "src.agents.project.agents.task_generator.llm_factory.stream",
            fake_stream,
        ):
            gen = TaskGenerator(output_dir=str(tmp_path), language="zh", mode="syllabus")

            import asyncio

            result = asyncio.run(gen.generate(
                theme="机器学习导论",
                reference_structure={
                    "sections": {
                        "cover": {"title": "封面信息", "content": "参考封面"},
                        "objectives": {"title": "课程目标", "content": "参考目标"},
                    }
                },
                kb_name=None,
                web_search=False,
                ws_callback=fake_callback,
            ))

    # 验证返回结果
    assert "content" in result
    assert "md_path" in result
    assert "docx_path" in result
    assert "pdf_path" in result

    # 验证文件已创建
    assert Path(result["md_path"]).exists()
    assert Path(result["docx_path"]).exists()

    # 验证生成了8个章节
    assert len(sections_generated) == 8

    # 验证章节包含课程大纲特有章节
    assert "prerequisites" in sections_generated
    assert "teaching_methods" in sections_generated


def test_syllabus_english_mode(tmp_path):
    """测试英文课程大纲生成模式。"""
    from src.agents.project.agents.task_generator import (
        TaskGenerator,
        SECTION_ORDER_SYLLABUS_EN,
    )

    syllabus_prompt_en = {
        "system": "You are a professional syllabus design expert.",
        "section_prompts": {
            "cover": "Generate course cover for theme: {theme}",
            "objectives": "Generate course objectives for theme: {theme}",
            "prerequisites": "Generate prerequisites for theme: {theme}",
            "content_structure": "Generate course content structure for theme: {theme}",
            "teaching_methods": "Generate teaching methods for theme: {theme}",
            "grading_scheme": "Generate grading scheme for theme: {theme}",
            "teaching_materials": "Generate teaching materials for theme: {theme}",
            "schedule": "Generate teaching schedule for theme: {theme}",
        },
    }

    async def fake_stream(*args, **kwargs):
        yield "Test syllabus content\n"

    async def noop_callback(msg):
):
        pass

    with patch(
        "src.agents.project.agents.task_generator.yaml.safe_load",
        return_value=syllabus_prompt_en,
    ):
        with patch(
            "src.agents.project.agents.task_generator.llm_factory.stream",
            fake_stream,
        ):
            gen = TaskGenerator(output_dir=str(tmp_path), language="en", mode="syllabus")

            # 验证英文章节顺序
            assert gen._section_order == SECTION_ORDER_SYLLABUS_EN
            assert len(gen._section_order) == 8

            import asyncio

            result = asyncio.run(gen.generate(
                theme="Introduction to Machine Learning",
                reference_structure={},
                kb_name=None,
                web_search=False,
                ws_callback=noop_callback,
            ))

            assert "content" in result
            assert Path(result["md_path"]).exists()


def test_syllabus_pdf_conversion_attempt(tmp_path, syllabus_prompt_zh):
    """测试课程大纲模式下尝试 PDF 转换（如果 LibreOffice 不存在，应优雅降级）。"""
    from src.agents.project.agents.task_generator import TaskGenerator

    async def fake_stream(*args, **kwargs):
        yield "Test content\n"

    async def noop_callback(msg):
        pass

    with patch(
        "src.agents.project.agents.task_generator.yaml.safe_load",
        return_value=syllabus_prompt_zh,
    ):
        with patch(
            "src.agents.project.agents.task_generator.llm_factory.stream",
            fake_stream,
        ):
            gen = TaskGenerator(output_dir=str(tmp_path), language="zh", mode="syllabus")

            import asyncio

            result = asyncio.run(gen.generate(
                theme="测试课程",
                reference_structure={},
                kb_name=None,
                web_search=False,
                ws_callback=noop_callback,
            ))

            # docx 应该总是创建
            assert Path(result["docx_path"]).exists()

            # 在没有 LibreOffice 的情况下，PDF 可能不存在
            # 这不应该导致生成失败
            assert "content" in result


def test_task_mode_unaffected(tmp_path):
    """验证任务书模式（mode="task"）不受课程大纲改动影响。"""
    from src.agents.project.agents.task_generator import (
        TaskGenerator,
        SECTION_ORDER_ZH,
    )

    task_prompt_zh = {
        "system": "你是一个专业的课程任务书设计专家。",
        "section_prompts": {
            "cover": "生成任务书封面信息",
            "objectives": "生成课程背景与目标",
            "modules": "生成模块概述",
            "details": "生成各模块设计内容",
            "requirements": "生成作业要求",
            "deliverables": "生成提交成果",
            "grading": "生成成绩考核",
            "schedule": "生成时间安排",
            "references": "生成参考资源",
        },
    }

    async def fake_stream(*args, **kwargs):
        yield "Test content\n"

    async def noop_callback(msg):
        pass

    with patch(
        "src.agents.project.agents.task_generator.yaml.safe_load",
        return_value=task_prompt_zh,
    ):
        with patch(
            "src.agents.project.agents.task_generator.llm_factory.stream",
            fake_stream,
        ):
            # 使用默认 mode="task"
            gen = TaskGenerator(output_dir=str(tmp_path), language="zh")

            # 验证仍然是任务书模式
            assert gen.mode == "task"
            assert gen._section_order == SECTION_ORDER_ZH
            assert len(gen._section_order) == 9


def test_syllabus_with_reference_structure_sections(tmp_path, syllabus_prompt_zh):
    """测试使用参考课程大纲结构动态生成章节。"""
    from src.agents.project.agents.task_generator import TaskGenerator

    async def fake_stream(*args, **kwargs):
        yield "Test content\n"

    logs = []

    async def fake_callback(msg):
        logs.append(msg)

    # 参考结构包含课程大纲特有章节
    reference_structure = {
        "sections": {
            "cover": {"title": "封面信息"},
            "objectives": {"title": "课程目标"},
            "prerequisites": {"title": "前置知识"},
            "content_structure": {"title": "课程内容"},
            "teaching_methods": {"title": "教学方法"},
            "grading_scheme": {"title": "考核方式"},
            "teaching_materials": {"title": "参考资料"},
            "schedule": {"title": "进度安排"},
        }
    }

    with patch(
        "src.agents.project.agents.task_generator.yaml.safe_load",
        return_value=syllabus_prompt_zh,
    ):
        with patch(
            "src.agents.project.agents.task_generator.llm_factory.stream",
            fake_stream,
        ):
            gen = TaskGenerator(output_dir=str(tmp_path), language="zh", mode="syllabus")

            import asyncio

            result = asyncio.run(gen.generate(
                theme="测试课程",
                reference_structure=reference_structure,
                kb_name=None,
                web_search=False,
                ws_callback=fake_callback,
            ))

            # 验证生成成功
            assert "content" in result
            assert Path(result["md_path"]).exists()

            # 验证所有8个标准章节都被处理
            section_messages = [l for l in logs if l.get("type") == "section"]
            assert len(section_messages) == 8


def test_syllabus_error_recovery(tmp_path, syllabus_prompt_zh):
    """测试课程大纲生成中某章节失败时的错误恢复。"""
    from src.agents.project.agents.task_generator import TaskGenerator

    call_generation_count = 0

    async def flaky_stream(*args, **kwargs):
        nonlocal call_generation_count
        call_generation_count += 1

        # 第3个章节（"content_structure"）失败
        if call_generation_count == 3:
            raise RuntimeError("模拟 LLM 错误")

        yield "Content\n"

    logs = []

    async def fake_callback(msg):
        logs.append(msg)

    with patch(
        "src.agents.project.agents.task_generator.yaml.safe_load",
        return_value=syllabus_prompt_zh,
    ):
        with patch(
            "src.agents.project.agents.task_generator.llm_factory.stream",
            flaky_stream,
        ):
            gen = TaskGenerator(output_dir=str(tmp_path), language="zh", mode="syllabus")

            import asyncio

            result = asyncio.run(gen.generate(
                theme="测试课程",
                reference_structure={},
                kb_name=None,
                web_search=False,
                ws_callback=fake_callback,
            ))

            # 验证流程完成
            assert "content" in result

            # 验证错误被记录
            error_logs = [
                l for l in logs if l.get("type") == "log" and "出错" in l.get("content", "")
            ]
            assert len(error_logs) >= 1
