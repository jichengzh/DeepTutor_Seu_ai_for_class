# -*- coding: utf-8 -*-
"""
ReviewAgent — 章节审阅修改 + 反思提炼。

职责：
1. review_section：基于章节内容 + 修改意见 + 对话历史，流式返回修改建议
2. extract_reflections：从对话历史中提炼精炼的反思要点
"""

from typing import Any, AsyncIterator, Callable

from src.logging.logger import get_logger
from src.services.llm import factory as llm_factory

logger = get_logger("ReviewAgent")

REVIEW_SYSTEM_PROMPT = """\
你是一位专业的文档审阅助手。你的任务是根据审阅者的修改意见，对文档的某个章节进行修改。

工作方式：
1. 仔细阅读当前章节内容和审阅者的意见
2. 先简要说明你将如何修改（1-2句话）
3. 然后输出修改后的完整章节内容，用以下标记包裹：

<revised_section>
（修改后的完整 Markdown 章节内容）
</revised_section>

注意事项：
- 仅修改审阅者指出的问题，不要大幅重写未提及的部分
- 保持原文档的格式风格和语言
- 如果审阅者的意见不够明确，可以先询问再修改
- 修改要专业、准确
"""

REFLECTION_SYSTEM_PROMPT = """\
你是一位文档质量分析专家。请从以下审阅对话历史中提炼出通用的、可复用的文档编写规则。

要求：
- 每条规则必须精炼（一句话，不超过50个字）
- 只提取具有通用性的规则（适用于未来类似文档的生成）
- 不要提取仅与特定主题相关的细节
- 去除重复或过于相似的规则
- 最多输出 10 条规则

输出格式（每行一条，无序号无前缀）：
规则内容1
规则内容2
...
"""


async def review_section_stream(
    section_key: str,
    section_title: str,
    section_content: str,
    user_message: str,
    chat_history: list[dict[str, str]],
    reflection_text: str = "",
) -> AsyncIterator[str]:
    """
    流式生成章节修改建议。

    Args:
        section_key: 章节标识
        section_title: 章节标题
        section_content: 当前章节内容
        user_message: 审阅者本轮输入
        chat_history: 之前的对话记录 [{"role": "user"|"assistant", "content": "..."}]
        reflection_text: 全局反思文档文本（可选）

    Yields:
        流式文本 chunk
    """
    # 构建对话消息（system prompt 必须作为 messages[0] 传入）
    context = f"## 当前章节：{section_title}\n\n{section_content}\n"
    if reflection_text:
        context += f"\n## 历史审阅经验\n{reflection_text}\n"

    messages = [
        {"role": "system", "content": REVIEW_SYSTEM_PROMPT},
        {"role": "user", "content": context + "\n请阅读以上章节内容，等待审阅意见。"},
        {"role": "assistant", "content": f"好的，我已阅读「{section_title}」章节。请提出您的修改意见。"},
    ]

    # 加入历史对话
    for msg in chat_history:
        messages.append({"role": msg["role"], "content": msg["content"]})

    # 加入当前轮用户消息
    messages.append({"role": "user", "content": user_message})

    try:
        async for chunk in llm_factory.stream(
            prompt="",
            system_prompt="",
            messages=messages,
            temperature=0.5,
        ):
            yield chunk
    except Exception as e:
        logger.error(f"Review stream error for section '{section_key}': {e}")
        yield f"\n\n[错误：生成修改建议时出错 - {e}]"


async def extract_reflections(
    chat_histories: dict[str, list[dict[str, str]]],
    theme: str = "",
) -> list[str]:
    """
    从所有章节的对话历史中提炼反思要点。

    Args:
        chat_histories: {section_key: [对话记录]}
        theme: 文档主题（用于标注来源）

    Returns:
        反思规则列表
    """
    # 合并所有对话
    all_conversations = []
    for section_key, history in chat_histories.items():
        if not history:
            continue
        all_conversations.append(f"--- 章节: {section_key} ---")
        for msg in history:
            role = msg.get("role", "")
            content = msg.get("content", "")
            if not content:
                continue
            role_label = "审阅者" if role == "user" else "AI助手"
            all_conversations.append(f"{role_label}: {content}")

    if not all_conversations:
        return []

    conversation_text = "\n".join(all_conversations)

    prompt = (
        f"以下是文档「{theme}」的审阅对话历史：\n\n"
        f"{conversation_text}\n\n"
        "请从中提炼通用的文档编写规则。"
    )

    try:
        result = await llm_factory.complete(
            prompt=prompt,
            system_prompt=REFLECTION_SYSTEM_PROMPT,
            temperature=0.3,
        )
        # 按行拆分，过滤空行
        lines = [
            line.strip().lstrip("0123456789.-、）) ").strip()
            for line in result.strip().splitlines()
        ]
        return [line for line in lines if line and len(line) > 4]
    except Exception as e:
        logger.error(f"Reflection extraction error: {e}")
        return []


def parse_revised_section(response: str) -> str | None:
    """
    从 LLM 回复中提取 <revised_section> 标记内的内容。
    如果没有标记，返回 None。
    """
    start_tag = "<revised_section>"
    end_tag = "</revised_section>"
    start = response.find(start_tag)
    end = response.find(end_tag)
    if start != -1 and end != -1:
        return response[start + len(start_tag):end].strip()
    return None


__all__ = ["review_section_stream", "extract_reflections", "parse_revised_section"]
