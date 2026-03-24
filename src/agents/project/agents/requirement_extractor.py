# -*- coding: utf-8 -*-
"""
RequirementExtractor — 从 Markdown 任务书中提取结构化需求。

使用 DeepTutor 现有 LLM factory（DashScope/字节），不需要 Anthropic API key。
"""

import json
import re
from dataclasses import dataclass, field
from typing import Callable

from src.logging.logger import get_logger
from src.services.llm import factory as llm_factory

logger = get_logger("RequirementExtractor")


@dataclass
class TaskModule:
    """任务书中的一个实践模块"""

    id: str
    title: str
    objectives: list[str] = field(default_factory=list)
    technical_requirements: list[str] = field(default_factory=list)
    deliverables: list[str] = field(default_factory=list)


@dataclass
class RequirementSpec:
    """从任务书提取的结构化需求"""

    theme: str
    tech_stack: list[str] = field(default_factory=list)
    environment: str = "Python 3.10 / Ubuntu 22.04"
    install_requirements: list[str] = field(default_factory=list)
    modules: list[TaskModule] = field(default_factory=list)
    coverage_map: dict[str, list[str]] = field(default_factory=dict)


class RequirementExtractor:
    """
    使用现有 LLM（DashScope/字节）分析任务书，输出结构化需求。
    不依赖 Anthropic API key，Phase 1 已有的 LLM 配置即可用。
    """

    async def extract(self, task_content: str, ws_callback: Callable) -> RequirementSpec:
        """
        从 Markdown 任务书提取结构化需求。

        Returns:
            RequirementSpec 对象；LLM 返回无效 JSON 时返回最小化结构，不抛异常。
        """
        await ws_callback({"type": "phase", "phase": "analysis", "content": "正在分析任务书需求..."})

        prompt = self._build_extraction_prompt(task_content)
        try:
            response = await llm_factory.complete(
                prompt=prompt,
                system_prompt="你是一个课程任务书分析专家，擅长提取结构化信息。请严格按要求输出 JSON，不添加任何解释。",
                temperature=0.1,
            )
            spec = self._parse_response(response)
        except Exception as e:
            logger.warning(f"RequirementExtractor LLM call failed: {e}, using minimal spec")
            spec = RequirementSpec(theme="未知主题")

        module_count = len(spec.modules)
        tech = ", ".join(spec.tech_stack) if spec.tech_stack else "未知"
        await ws_callback({
            "type": "status",
            "content": f"需求分析完成：发现 {module_count} 个模块，技术栈：{tech}",
        })
        return spec

    # ── private ──────────────────────────────────────────────────────────────

    def _build_extraction_prompt(self, task_content: str) -> str:
        return f"""请分析以下课程实习任务书，提取结构化需求，以 JSON 格式输出：

{task_content[:8000]}

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

    def _parse_response(self, response: str) -> RequirementSpec:
        """解析 LLM 返回的 JSON。失败时返回最小化 RequirementSpec。"""
        # 提取第一个 {...} 块（防止 LLM 在 JSON 外附加文字）
        match = re.search(r"\{.*\}", response, re.DOTALL)
        if not match:
            logger.warning("No JSON found in LLM response")
            return RequirementSpec(theme="")

        try:
            data = json.loads(match.group())
        except json.JSONDecodeError as e:
            logger.warning(f"JSON parse error: {e}")
            return RequirementSpec(theme="")

        modules = []
        for i, m in enumerate(data.get("modules", []), start=1):
            modules.append(
                TaskModule(
                    id=m.get("id", f"module_{i}"),
                    title=m.get("title", f"模块{i}"),
                    objectives=m.get("objectives", []),
                    technical_requirements=m.get("technical_requirements", []),
                    deliverables=m.get("deliverables", []),
                )
            )

        return RequirementSpec(
            theme=data.get("theme", ""),
            tech_stack=data.get("tech_stack", []),
            environment=data.get("environment", "Python 3.10 / Ubuntu 22.04"),
            install_requirements=data.get("install_requirements", []),
            modules=modules,
        )
