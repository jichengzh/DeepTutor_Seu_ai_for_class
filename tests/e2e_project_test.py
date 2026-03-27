#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
端到端测试：任务书生成 + 代码仓库生成
运行方式：conda run -n deeptutor python tests/e2e_project_test.py
"""
import asyncio
import json
import os
import sys
from pathlib import Path

import httpx
import websockets

# 绕过本地代理（http_proxy/https_proxy 会导致 localhost 请求失败）
os.environ.pop("http_proxy", None)
os.environ.pop("https_proxy", None)
os.environ.pop("HTTP_PROXY", None)
os.environ.pop("HTTPS_PROXY", None)

BASE_URL     = "http://localhost:8001/api/v1"
WS_BASE      = "ws://localhost:8001/api/v1"
DOCX_PATH    = Path("/home/jcz/DeepTutor/book/嵌入式开发暑期实习任务书.docx")
THEME        = "无人机感知机器人实习"
KB_NAME      = "raspberry_pi"
PROJECTS_DIR = Path("/home/jcz/DeepTutor/data/user/projects")


# ── Step 1: 上传参考文档 ──────────────────────────────────────────────────────
def upload_reference() -> dict:
    print(f"\n[Step 1] 上传参考文档: {DOCX_PATH.name}")
    with httpx.Client(timeout=30) as client:
        with open(DOCX_PATH, "rb") as f:
            resp = client.post(
                f"{BASE_URL}/project/upload-reference",
                files={"files": (DOCX_PATH.name, f,
                       "application/vnd.openxmlformats-officedocument.wordprocessingml.document")},
            )
    resp.raise_for_status()
    data = resp.json()
    sections = list(data["structure"].get("sections", {}).keys())
    print(f"  ✓ 识别章节({data['section_count']}个): {sections}")
    return data["structure"]


# ── Step 2: WebSocket 生成任务书 ──────────────────────────────────────────────
async def generate_task(reference_structure: dict) -> tuple[str, str]:
    print(f"\n[Step 2] 生成任务书 (主题={THEME}, KB={KB_NAME}, WebSearch=True)")
    uri = f"{WS_BASE}/project/generate-task"
    session_id = None
    task_content = ""
    section_chars = 0

    async with websockets.connect(uri, max_size=10_000_000, ping_timeout=600) as ws:
        await ws.send(json.dumps({
            "theme": THEME,
            "reference_structure": reference_structure,
            "kb_name": KB_NAME,
            "web_search": True,
            "session_id": None,
        }))

        async for raw in ws:
            msg = json.loads(raw)
            t = msg.get("type")

            if t == "status":
                print(f"  [status] {msg['content']}")

            elif t == "section":
                sec = msg.get("section", "")
                content = msg.get("content", "")
                print(f"  [section] {sec}: {len(content)} chars")

            elif t == "chunk":
                section_chars += len(msg.get("content", ""))
                if section_chars % 500 < len(msg.get("content", "")):
                    print(".", end="", flush=True)

            elif t == "complete":
                session_id = msg.get("session_id")
                task_content = msg.get("task_content", "")
                print(f"\n  ✓ 任务书完成 session_id={session_id}, {len(task_content)} chars")
                break

            elif t == "error":
                print(f"\n  ✗ 错误: {msg.get('content')}")
                sys.exit(1)

    return session_id, task_content


# ── Step 3: WebSocket 生成代码仓库 ────────────────────────────────────────────
async def generate_code(session_id: str, task_content: str) -> dict:
    print(f"\n[Step 3] 生成代码仓库 (difficulty=low, cli=claude)")
    uri = f"{WS_BASE}/project/generate-code"
    result = {}

    async with websockets.connect(uri, max_size=10_000_000, ping_timeout=1800) as ws:
        await ws.send(json.dumps({
            "session_id": session_id,
            "task_content": task_content,
            "difficulty": "low",
            "cli_tool": "claude",
        }))

        async for raw in ws:
            msg = json.loads(raw)
            t = msg.get("type")

            if t == "phase":
                print(f"  [phase:{msg.get('phase')}] {msg.get('content','')}")

            elif t == "agent_log":
                log_type = msg.get("log_type", "")
                if log_type == "tool_use":
                    print(f"  [tool] {msg.get('tool','')}  {msg.get('path','')}")
                elif log_type == "message":
                    print(f"  [msg] {msg.get('content','')[:120]}")

            elif t == "file_created":
                print(f"  [file+] {msg.get('path')}")

            elif t == "verify_result":
                passed = msg.get("passed")
                report = msg.get("report", "")[:200]
                print(f"  [verify] passed={passed}  {report}")

            elif t == "complete":
                result = msg
                print(f"  ✓ 代码生成完成")
                break

            elif t == "error":
                print(f"\n  ✗ 错误: {msg.get('content')}")
                sys.exit(1)

    return result


# ── Step 4: 验证输出目录 ──────────────────────────────────────────────────────
def verify_output(session_id: str):
    print(f"\n[Step 4] 验证输出目录")
    proj_dir = PROJECTS_DIR / session_id
    repo_dir = proj_dir / "repo"

    checks = {
        "generated_task.md":   (proj_dir / "generated_task.md").exists(),
        "generated_task.docx": (proj_dir / "generated_task.docx").exists(),
        "repo/ 目录":          repo_dir.exists(),
        "PLAN.md":             (repo_dir / "PLAN.md").exists() if repo_dir.exists() else False,
        "README.md":           (repo_dir / "README.md").exists() if repo_dir.exists() else False,
        ".py 文件存在":         any(repo_dir.rglob("*.py")) if repo_dir.exists() else False,
        "requirements.txt":    (repo_dir / "requirements.txt").exists() if repo_dir.exists() else False,
    }

    all_ok = True
    for name, ok in checks.items():
        status = "✓" if ok else "✗"
        print(f"  {status} {name}")
        if not ok:
            all_ok = False

    if repo_dir.exists():
        py_files = list(repo_dir.rglob("*.py"))
        print(f"\n  Python 文件({len(py_files)}个):")
        for f in sorted(py_files)[:15]:
            print(f"    {f.relative_to(repo_dir)}")

    print(f"\n{'✅ 全部验证通过' if all_ok else '⚠️  部分文件缺失'}")
    print(f"输出路径: {proj_dir}")


# ── Main ──────────────────────────────────────────────────────────────────────
async def main():
    try:
        with httpx.Client(timeout=5) as c:
            c.get(f"{BASE_URL}/")
    except Exception:
        print("✗ 后端未运行，请先启动: conda run -n deeptutor uvicorn src.api.main:app --port 8001")
        sys.exit(1)

    structure = upload_reference()
    session_id, task_content = await generate_task(structure)
    await generate_code(session_id, task_content)
    verify_output(session_id)


if __name__ == "__main__":
    asyncio.run(main())
