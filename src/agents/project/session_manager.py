# -*- coding: utf-8 -*-
"""
ProjectSessionManager — 项目会话持久化。

存储路径：data/user/project_sessions.json
格式与 SolverSessionManager 保持一致。
"""

import json
import time
import uuid
from pathlib import Path
from typing import Any


MAX_SESSIONS = 100


class ProjectSessionManager:
    """
    管理项目会话的持久化存储。

    每条会话包含：
    - session_id: 唯一标识符
    - theme: 新任务书主题
    - kb_name: 使用的知识库
    - status: init | task_generating | task_generated | code_generating | complete
    - task_md_path: 生成的 Markdown 文件路径
    - task_docx_path: 生成的 Word 文件路径
    - repo_path: 代码仓库路径
    - reference_structure: 解析出的参考文档结构
    - token_stats: token 统计
    - created_at / updated_at: 时间戳
    """

    def __init__(self, base_dir: str | None = None):
        if base_dir is None:
            # src/agents/project/session_manager.py → project root is 4 levels up
            project_root = Path(__file__).resolve().parents[3]
            base_dir_path = project_root / "data" / "user"
        else:
            base_dir_path = Path(base_dir)

        self.base_dir = base_dir_path
        self.base_dir.mkdir(parents=True, exist_ok=True)
        self.sessions_file = self.base_dir / "project_sessions.json"
        self._ensure_file()

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _ensure_file(self):
        if not self.sessions_file.exists():
            self._save_data({"version": "1.0", "sessions": []})

    def _load_data(self) -> dict[str, Any]:
        try:
            with open(self.sessions_file, encoding="utf-8") as f:
                return json.load(f)
        except (json.JSONDecodeError, FileNotFoundError):
            return {"version": "1.0", "sessions": []}

    def _save_data(self, data: dict[str, Any]):
        with open(self.sessions_file, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2, ensure_ascii=False)

    def _get_sessions(self) -> list[dict[str, Any]]:
        return self._load_data().get("sessions", [])

    def _save_sessions(self, sessions: list[dict[str, Any]]):
        data = self._load_data()
        data["sessions"] = sessions
        self._save_data(data)

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def create_session(
        self,
        theme: str,
        kb_name: str | None = None,
        reference_structure: dict[str, Any] | None = None,
    ) -> str:
        """
        创建新会话，返回 session_id。
        """
        session_id = f"proj_{int(time.time() * 1000)}_{uuid.uuid4().hex[:6]}"
        now = time.time()

        session: dict[str, Any] = {
            "session_id": session_id,
            "theme": theme[:200],
            "kb_name": kb_name or "",
            "status": "init",
            "task_md_path": None,
            "task_docx_path": None,
            "repo_path": None,
            "reference_structure": reference_structure or {},
            "token_stats": {
                "model": "Unknown",
                "calls": 0,
                "tokens": 0,
                "input_tokens": 0,
                "output_tokens": 0,
                "cost": 0.0,
            },
            "created_at": now,
            "updated_at": now,
        }

        sessions = self._get_sessions()
        sessions.insert(0, session)
        if len(sessions) > MAX_SESSIONS:
            sessions = sessions[:MAX_SESSIONS]
        self._save_sessions(sessions)

        return session_id

    def get_session(self, session_id: str) -> dict[str, Any] | None:
        for s in self._get_sessions():
            if s.get("session_id") == session_id:
                return s
        return None

    def update_session(self, session_id: str, **fields) -> dict[str, Any] | None:
        """
        更新会话字段，自动刷新 updated_at。
        支持更新任意字段（status, task_md_path, task_docx_path, repo_path, token_stats 等）。
        """
        sessions = self._get_sessions()
        for i, s in enumerate(sessions):
            if s.get("session_id") == session_id:
                for k, v in fields.items():
                    s[k] = v
                s["updated_at"] = time.time()
                # Move to front (most recently updated)
                sessions.pop(i)
                sessions.insert(0, s)
                self._save_sessions(sessions)
                return s
        return None

    def list_sessions(self, limit: int = 20) -> list[dict[str, Any]]:
        """返回最近 limit 条会话的摘要（不含 reference_structure 详情）。"""
        sessions = self._get_sessions()[:limit]
        return [
            {
                "session_id": s.get("session_id"),
                "theme": s.get("theme"),
                "kb_name": s.get("kb_name"),
                "status": s.get("status"),
                "task_md_path": s.get("task_md_path"),
                "repo_path": s.get("repo_path"),
                "token_stats": s.get("token_stats"),
                "created_at": s.get("created_at"),
                "updated_at": s.get("updated_at"),
            }
            for s in sessions
        ]

    def delete_session(self, session_id: str) -> bool:
        sessions = self._get_sessions()
        new_sessions = [s for s in sessions if s.get("session_id") != session_id]
        if len(new_sessions) < len(sessions):
            self._save_sessions(new_sessions)
            return True
        return False


# Singleton
_project_session_manager: ProjectSessionManager | None = None


def get_project_session_manager() -> ProjectSessionManager:
    global _project_session_manager
    if _project_session_manager is None:
        _project_session_manager = ProjectSessionManager()
    return _project_session_manager


__all__ = ["ProjectSessionManager", "get_project_session_manager"]
