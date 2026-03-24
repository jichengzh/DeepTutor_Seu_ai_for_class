"""Tests for ProjectSessionManager."""

import json
from pathlib import Path
from unittest.mock import patch

import pytest

from src.agents.project.session_manager import ProjectSessionManager


def _mgr(tmp_path: Path) -> ProjectSessionManager:
    return ProjectSessionManager(base_dir=str(tmp_path))


def test_create_and_retrieve_session(tmp_path):
    mgr = _mgr(tmp_path)
    sid = mgr.create_session(theme="ROS 导航", kb_name="kb1")
    assert sid.startswith("proj_")
    session = mgr.get_session(sid)
    assert session is not None
    assert session["theme"] == "ROS 导航"
    assert session["status"] == "init"
    assert session["kb_name"] == "kb1"


def test_update_session(tmp_path):
    mgr = _mgr(tmp_path)
    sid = mgr.create_session(theme="test")
    mgr.update_session(sid, status="task_generated", task_md_path="/some/path.md")
    session = mgr.get_session(sid)
    assert session["status"] == "task_generated"
    assert session["task_md_path"] == "/some/path.md"


def test_update_nonexistent_session(tmp_path):
    mgr = _mgr(tmp_path)
    result = mgr.update_session("proj_does_not_exist", status="complete")
    assert result is None


def test_max_100_sessions(tmp_path):
    mgr = _mgr(tmp_path)
    for i in range(105):
        mgr.create_session(theme=f"theme_{i}")
    sessions = mgr.list_sessions(limit=200)
    assert len(sessions) <= 100


def test_delete_session(tmp_path):
    mgr = _mgr(tmp_path)
    sid = mgr.create_session(theme="to delete")
    deleted = mgr.delete_session(sid)
    assert deleted is True
    assert mgr.get_session(sid) is None


def test_delete_nonexistent_returns_false(tmp_path):
    mgr = _mgr(tmp_path)
    assert mgr.delete_session("proj_no_such") is False


def test_corrupted_json_resets(tmp_path):
    sessions_file = tmp_path / "project_sessions.json"
    sessions_file.write_text("NOT VALID JSON")
    mgr = _mgr(tmp_path)
    sid = mgr.create_session(theme="recovery test")
    assert sid is not None
    session = mgr.get_session(sid)
    assert session["theme"] == "recovery test"


def test_list_sessions_limit(tmp_path):
    mgr = _mgr(tmp_path)
    for i in range(10):
        mgr.create_session(theme=f"t{i}")
    result = mgr.list_sessions(limit=5)
    assert len(result) == 5
