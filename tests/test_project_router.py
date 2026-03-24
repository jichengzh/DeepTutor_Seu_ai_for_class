"""Tests for Project Creator API router."""

from io import BytesIO
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from src.api.main import app

client = TestClient(app)

SAMPLE_DOCX = Path("book/嵌入式开发暑期实习任务书.docx")


@pytest.mark.skipif(not SAMPLE_DOCX.exists(), reason="Sample docx not found")
def test_upload_reference_valid_docx():
    with open(SAMPLE_DOCX, "rb") as f:
        resp = client.post(
            "/api/v1/project/upload-reference",
            files={"files": ("task.docx", f,
                             "application/vnd.openxmlformats-officedocument.wordprocessingml.document")},
        )
    assert resp.status_code == 200
    data = resp.json()
    assert "structure" in data
    assert "reference_id" in data
    assert data["section_count"] >= 0


def test_upload_reference_invalid_type():
    resp = client.post(
        "/api/v1/project/upload-reference",
        files={"files": ("test.txt", BytesIO(b"hello world"), "text/plain")},
    )
    assert resp.status_code == 400


def test_upload_reference_no_files():
    resp = client.post("/api/v1/project/upload-reference")
    assert resp.status_code in (400, 422)


def test_list_sessions():
    resp = client.get("/api/v1/project/sessions")
    assert resp.status_code == 200
    data = resp.json()
    assert "sessions" in data
    assert isinstance(data["sessions"], list)


def test_get_nonexistent_session():
    resp = client.get("/api/v1/project/sessions/proj_nonexistent_id_12345")
    assert resp.status_code == 404


def test_delete_nonexistent_session():
    resp = client.delete("/api/v1/project/sessions/proj_nonexistent_id_12345")
    assert resp.status_code == 404


def test_download_task_nonexistent_session():
    resp = client.get("/api/v1/project/proj_nonexistent_id/download-task")
    assert resp.status_code == 404


def test_project_routes_registered():
    """Smoke test: verify all project routes are registered."""
    routes = [r.path for r in app.routes]
    assert "/api/v1/project/upload-reference" in routes
    assert "/api/v1/project/sessions" in routes
    assert "/api/v1/project/generate-task" in routes
