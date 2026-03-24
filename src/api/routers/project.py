# -*- coding: utf-8 -*-
"""
Project Creator API Router

REST 端点：
  POST /project/upload-reference               上传参考任务书，返回解析结构
  GET  /project/sessions                       列出历史项目
  GET  /project/sessions/{session_id}          获取单个项目详情
  DELETE /project/sessions/{session_id}        删除项目
  GET  /project/{session_id}/download-task     下载任务书（md/docx）
  GET  /project/{session_id}/download-repo     下载代码仓库 zip

WebSocket 端点：
  WS /project/generate-task             流式生成任务书
  WS /project/generate-code             流式生成代码仓库（Claude Code CLI）
"""

import asyncio
import io
import uuid
import zipfile
from pathlib import Path

from fastapi import APIRouter, File, HTTPException, UploadFile, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, StreamingResponse

from src.agents.project import ProjectCoordinator, get_project_session_manager
from src.agents.project.agents.task_parser import TaskParser
from src.logging.logger import get_logger
from src.services.llm.config import get_llm_config
from src.services.settings.interface_settings import get_ui_language

logger = get_logger("ProjectAPI")
router = APIRouter()

PROJECT_ROOT = Path(__file__).resolve().parents[3]
PROJECTS_DIR = PROJECT_ROOT / "data" / "user" / "projects"
PROJECTS_DIR.mkdir(parents=True, exist_ok=True)

ALLOWED_EXTENSIONS = {".docx", ".pdf"}


def _get_project_output_dir(session_id: str) -> Path:
    d = PROJECTS_DIR / session_id
    d.mkdir(parents=True, exist_ok=True)
    return d


# ==============================================================================
# REST 端点
# ==============================================================================


@router.post("/project/upload-reference")
async def upload_reference(files: list[UploadFile] = File(...)):
    """
    上传参考任务书文件，解析并返回文档结构。
    支持 .docx 和 .pdf 格式。
    """
    if not files:
        raise HTTPException(status_code=400, detail="No files provided")

    upload_file = files[0]
    original_name = upload_file.filename or "upload"
    suffix = Path(original_name).suffix.lower()

    if suffix not in ALLOWED_EXTENSIONS:
        raise HTTPException(
            status_code=400,
            detail=f"Unsupported file type '{suffix}'. Supported: {sorted(ALLOWED_EXTENSIONS)}",
        )

    # Save with uuid filename to avoid special character issues
    safe_name = f"{uuid.uuid4().hex}{suffix}"
    upload_dir = PROJECT_ROOT / "data" / "user" / "project_uploads"
    upload_dir.mkdir(parents=True, exist_ok=True)
    save_path = upload_dir / safe_name

    content = await upload_file.read()
    save_path.write_bytes(content)

    # Parse
    parser = TaskParser()
    try:
        if suffix == ".docx":
            structure = parser.parse_docx(str(save_path))
        else:
            structure = parser.parse_pdf(str(save_path))
    except Exception as e:
        logger.error(f"Failed to parse uploaded file: {e}")
        raise HTTPException(status_code=422, detail=f"Failed to parse document: {e}")

    summary = parser.extract_structure_summary(structure)

    return {
        "reference_id": safe_name,
        "original_filename": original_name,
        "structure": structure,
        "structure_summary": summary,
        "section_count": len(structure.get("sections", {})),
    }


@router.get("/project/sessions")
async def list_sessions(limit: int = 20):
    """获取历史项目列表。"""
    mgr = get_project_session_manager()
    sessions = mgr.list_sessions(limit=limit)
    return {"sessions": sessions, "total": len(sessions)}


@router.get("/project/sessions/{session_id}")
async def get_session(session_id: str):
    """获取单个项目详情。"""
    mgr = get_project_session_manager()
    session = mgr.get_session(session_id)
    if not session:
        raise HTTPException(status_code=404, detail=f"Session '{session_id}' not found")
    return session


@router.delete("/project/sessions/{session_id}")
async def delete_session(session_id: str):
    """删除项目会话。"""
    mgr = get_project_session_manager()
    deleted = mgr.delete_session(session_id)
    if not deleted:
        raise HTTPException(status_code=404, detail=f"Session '{session_id}' not found")
    return {"deleted": session_id}


@router.get("/project/{session_id}/download-task")
async def download_task(session_id: str, format: str = "md"):
    """下载生成的任务书（md 或 docx 格式）。"""
    mgr = get_project_session_manager()
    session = mgr.get_session(session_id)
    if not session:
        raise HTTPException(status_code=404, detail=f"Session '{session_id}' not found")

    if format == "docx":
        file_path = session.get("task_docx_path")
        media_type = "application/vnd.openxmlformats-officedocument.wordprocessingml.document"
        filename = "generated_task.docx"
    else:
        file_path = session.get("task_md_path")
        media_type = "text/markdown"
        filename = "generated_task.md"

    if not file_path or not Path(file_path).exists():
        raise HTTPException(status_code=404, detail="Task document not yet generated")

    return FileResponse(path=file_path, media_type=media_type, filename=filename)


@router.get("/project/{session_id}/download-repo")
async def download_repo(session_id: str):
    """将生成的代码仓库打包为 zip 下载（排除 .git 目录）。"""
    mgr = get_project_session_manager()
    session = mgr.get_session(session_id)
    if not session:
        raise HTTPException(status_code=404, detail=f"Session '{session_id}' not found")

    repo_dir = PROJECTS_DIR / session_id / "repo"
    if not repo_dir.exists():
        raise HTTPException(status_code=404, detail="Code repository not yet generated")

    # 检查总大小，超过 500 MB 拒绝
    total_size = sum(
        f.stat().st_size
        for f in repo_dir.rglob("*")
        if f.is_file() and ".git" not in f.parts
    )
    if total_size > 500 * 1024 * 1024:
        raise HTTPException(status_code=413, detail="Repository too large to download (>500 MB)")

    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        for file_path in sorted(repo_dir.rglob("*")):
            if ".git" in file_path.parts or not file_path.is_file():
                continue
            zf.write(file_path, file_path.relative_to(repo_dir))
    buf.seek(0)

    import re as _re
    raw_theme = session.get("theme", "project") or "project"
    # Strip non-ASCII characters so the HTTP header stays latin-1 safe
    ascii_theme = _re.sub(r"[^\x00-\x7F]", "_", raw_theme).replace(" ", "_")[:30].strip("_") or "project"
    filename = f"{ascii_theme}_{session_id[:8]}.zip"

    return StreamingResponse(
        buf,
        media_type="application/zip",
        headers={"Content-Disposition": f'attachment; filename="{filename}"'},
    )


# ==============================================================================
# WebSocket 端点
# ==============================================================================


@router.websocket("/project/generate-task")
async def websocket_generate_task(websocket: WebSocket):
    """
    流式生成新任务书。

    Client → Server:
        {"theme": "...", "reference_structure": {...},
         "kb_name": "...", "web_search": true, "session_id": null}

    Server → Client (stream):
        {"type": "status"|"log"|"chunk"|"section"|"token_stats"|"complete"|"error", ...}
    """
    await websocket.accept()
    log_queue: asyncio.Queue = asyncio.Queue()
    pusher_task = None

    async def log_pusher():
        while True:
            entry = await log_queue.get()
            if entry is None:
                break
            try:
                await websocket.send_json(entry)
            except Exception:
                break

    try:
        data = await websocket.receive_json()
        theme = data.get("theme", "").strip()
        reference_structure = data.get("reference_structure") or {}
        kb_name = data.get("kb_name") or None
        web_search = bool(data.get("web_search", False))
        session_id = data.get("session_id") or None

        if not theme:
            await websocket.send_json({"type": "error", "content": "主题 (theme) 不能为空"})
            return

        # Create or reuse session
        mgr = get_project_session_manager()
        if not session_id:
            session_id = mgr.create_session(
                theme=theme,
                kb_name=kb_name,
                reference_structure=reference_structure,
            )
        mgr.update_session(session_id, status="task_generating")

        # Output directory for this session
        output_dir = _get_project_output_dir(session_id)

        # Coordinator
        language = get_ui_language()
        coordinator = ProjectCoordinator(
            output_dir=str(output_dir),
            language=language,
            kb_name=kb_name,
            web_search_enabled=web_search,
        )

        async def ws_callback(msg: dict):
            await log_queue.put(msg)

        coordinator.set_ws_callback(ws_callback)

        # Start pusher
        pusher_task = asyncio.create_task(log_pusher())

        # Run generation
        result = await coordinator.generate_task_document(
            theme=theme,
            reference_structure=reference_structure,
        )

        # Update session with result paths
        mgr.update_session(
            session_id,
            status="task_generated",
            task_md_path=result["md_path"],
            task_docx_path=result.get("docx_path"),
        )

        # Signal pusher to stop
        await log_queue.put(None)
        await pusher_task

        await websocket.send_json({
            "type": "complete",
            "session_id": session_id,
            "task_md_path": result["md_path"],
            "task_content": result["content"],
        })

    except WebSocketDisconnect:
        logger.info("WebSocket disconnected during task generation")
    except Exception as e:
        logger.exception(f"Task generation error: {e}")
        try:
            await websocket.send_json({"type": "error", "content": str(e)})
        except Exception:
            pass
    finally:
        if pusher_task and not pusher_task.done():
            await log_queue.put(None)
            try:
                await asyncio.wait_for(pusher_task, timeout=2.0)
            except Exception:
                pass
        try:
            await websocket.close()
        except Exception:
            pass


@router.websocket("/project/generate-code")
async def websocket_generate_code(websocket: WebSocket):
    """
    流式代码仓库生成（Phase 3）。
    通过本机 claude CLI（OAuth，Claude Pro）运行三阶段生成：
      A. 需求分析（LLM factory）
      B. 架构规划（claude CLI, Write only）
      C. 代码生成（claude CLI, 全工具）
      D. 验证 + 自动修复

    Client → Server: {"session_id": "proj_xxx", "task_content": "..."}
    Server → Client: phase / agent_log / file_created / verify_result / coverage / complete / error
    """
    await websocket.accept()
    log_queue: asyncio.Queue = asyncio.Queue()
    pusher_task = None

    async def ws_callback(msg: dict):
        await log_queue.put(msg)

    async def log_pusher():
        while True:
            entry = await log_queue.get()
            if entry is None:
                break
            try:
                await websocket.send_json(entry)
            except Exception:
                break

    try:
        data = await websocket.receive_json()
        session_id = data.get("session_id", "").strip()
        task_content = data.get("task_content", "")

        if not session_id:
            await websocket.send_json({"type": "error", "content": "session_id 必填"})
            return

        mgr = get_project_session_manager()
        session = mgr.get_session(session_id)
        if not session:
            await websocket.send_json(
                {"type": "error", "content": f"会话不存在：{session_id}"}
            )
            return

        output_dir = str(_get_project_output_dir(session_id))

        # 检查 claude CLI
        try:
            from src.agents.project.agents.code_generator import CodeGenerator
            generator = CodeGenerator()
        except EnvironmentError as e:
            await websocket.send_json({"type": "error", "content": str(e)})
            return

        mgr.update_session(session_id, status="code_generating")
        pusher_task = asyncio.create_task(log_pusher())

        # 阶段 A：需求提取（使用现有 LLM factory，不需要 Anthropic key）
        from src.agents.project.agents.requirement_extractor import RequirementExtractor
        extractor = RequirementExtractor()
        spec = await extractor.extract(task_content, ws_callback)

        # 阶段 B/C/D：代码生成 + 验证
        result = await generator.generate(
            spec=spec,
            output_dir=output_dir,
            ws_callback=ws_callback,
        )

        mgr.update_session(session_id, status="complete", repo_path=result["repo_path"])

        await log_queue.put(None)
        await pusher_task
        pusher_task = None

        await websocket.send_json({
            "type": "complete",
            "session_id": session_id,
            "repo_path": result["repo_path"],
            "file_tree": result["file_tree"],
            "coverage_map": result["coverage_map"],
            "verify_passed": result["verify_passed"],
        })

    except WebSocketDisconnect:
        logger.info("WebSocket disconnected during code generation")
    except Exception as e:
        logger.exception(f"Code generation error: {e}")
        try:
            await websocket.send_json({"type": "error", "content": str(e)})
        except Exception:
            pass
    finally:
        if pusher_task and not pusher_task.done():
            await log_queue.put(None)
            try:
                await asyncio.wait_for(pusher_task, timeout=2.0)
            except Exception:
                pass
        try:
            await websocket.close()
        except Exception:
            pass
