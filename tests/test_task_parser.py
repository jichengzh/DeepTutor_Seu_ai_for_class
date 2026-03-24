"""Tests for TaskParser."""

import os
import tempfile
from pathlib import Path

import pytest

from src.agents.project.agents.task_parser import TaskParser

SAMPLE_DOCX = Path("book/嵌入式开发暑期实习任务书.docx")


@pytest.mark.skipif(not SAMPLE_DOCX.exists(), reason="Sample docx not found")
def test_parse_real_docx_has_sections():
    parser = TaskParser()
    result = parser.parse_docx(str(SAMPLE_DOCX))
    assert "sections" in result
    expected = {"cover", "objectives", "modules", "details", "requirements",
                "deliverables", "grading", "schedule", "references"}
    found = set(result["sections"].keys())
    assert len(found & expected) >= 4, f"Only found sections: {found}"


@pytest.mark.skipif(not SAMPLE_DOCX.exists(), reason="Sample docx not found")
def test_parse_docx_returns_raw_text():
    parser = TaskParser()
    result = parser.parse_docx(str(SAMPLE_DOCX))
    assert len(result.get("raw_text", "")) > 100


def test_parse_nonexistent_file_raises():
    parser = TaskParser()
    with pytest.raises(FileNotFoundError):
        parser.parse_docx("/nonexistent/path.docx")


def test_parse_wrong_content_raises():
    """传入内容不是合法 docx 的文件应抛出异常。"""
    with tempfile.NamedTemporaryFile(suffix=".docx", delete=False) as f:
        f.write(b"not a real docx file")
        tmp = f.name
    try:
        parser = TaskParser()
        with pytest.raises(Exception):
            parser.parse_docx(tmp)
    finally:
        os.unlink(tmp)


@pytest.mark.skipif(not SAMPLE_DOCX.exists(), reason="Sample docx not found")
def test_extract_structure_summary_truncates():
    parser = TaskParser()
    result = parser.parse_docx(str(SAMPLE_DOCX))
    summary = parser.extract_structure_summary(result)
    assert len(summary) <= 2000


def test_extract_structure_summary_empty_input():
    parser = TaskParser()
    result = {"sections": {}, "module_count": 0, "has_grading_table": False, "raw_text": ""}
    summary = parser.extract_structure_summary(result)
    assert isinstance(summary, str)


def test_extract_structure_summary_scanned_pdf():
    parser = TaskParser()
    result = {"error": "scanned_pdf", "raw_text": "", "sections": {}}
    summary = parser.extract_structure_summary(result)
    assert "scanned_pdf" in summary
