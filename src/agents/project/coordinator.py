# -*- coding: utf-8 -*-
"""
ProjectCoordinator — 任务书生成的主协调器。

负责：
1. 协调 TaskGenerator 执行生成流程
2. 管理 WebSocket 回调传递
3. 保存会话状态
"""

from pathlib import Path
from typing import Any, Callable

from src.logging.logger import get_logger

from .agents.task_generator import TaskGenerator
from .session_manager import ProjectSessionManager

logger = get_logger("ProjectCoordinator")


class ProjectCoordinator:
    """
    协调任务书 / 课程大纲生成全流程。

    Args:
        output_dir: 本次会话的输出目录
        language: 提示词语言（"zh" 或 "en"）
        kb_name: 知识库名称（可选）
        web_search_enabled: 是否启用网络搜索
        mode: 生成模式，"task"（任务书）或 "syllabus"（课程大纲）
    """

    def __init__(
        self,
        output_dir: str,
        language: str = "zh",
        kb_name: str | None = None,
        web_search_enabled: bool = False,
        mode: str = "task",
    ):
        self.output_dir = Path(output_dir)
        self.language = language
        self.kb_name = kb_name
        self.web_search_enabled = web_search_enabled
        self.mode = mode
        self._ws_callback: Callable | None = None

    def set_ws_callback(self, callback: Callable):
        self._ws_callback = callback

    async def _send(self, msg: dict[str, Any]):
        if self._ws_callback:
            await self._ws_callback(msg)

    async def generate_task_document(
        self,
        theme: str,
        reference_structure: dict[str, Any],
    ) -> dict[str, Any]:
        """
        生成任务书文档。

        Returns:
            {"content": str, "md_path": str, "docx_path": str}
        """
        generator = TaskGenerator(
            output_dir=str(self.output_dir),
            language=self.language,
            mode=self.mode,
        )

        result = await generator.generate(
            theme=theme,
            reference_structure=reference_structure,
            kb_name=self.kb_name,
            web_search=self.web_search_enabled,
            ws_callback=self._send,
        )

        await self._send({
            "type": "token_stats",
            "stats": {"calls": 0, "tokens": 0},  # TODO: wire up real stats in later phase
        })

        return result


__all__ = ["ProjectCoordinator"]
