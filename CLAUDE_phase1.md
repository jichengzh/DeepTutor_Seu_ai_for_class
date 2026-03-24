# CLAUDE.md — Phase 1: 案例项目创建 · 后端任务书生成

## 规则

- **不写兼容性代码**，除非用户明确要求。
- 需求不明确时向用户提问，不猜测。
- 每完成一个文件，立即运行对应测试，确认通过再继续。
- 发现 bug 时：先写能重现的测试 → 修复 → 确认测试通过 → 反思根因。

---

## Phase 1 任务计划

按顺序实现，每步完成后立即测试：

| 步骤 | 文件 | 依赖 |
|------|------|------|
| 1 | `src/agents/project/agents/task_parser.py` | python-docx, pymupdf |
| 2 | `src/api/routers/project.py` — `upload-reference` 端点 | task_parser |
| 3 | `src/agents/project/agents/task_generator.py` (基础版，非流式) | 现有 LLM factory |
| 4 | `src/api/routers/project.py` — `generate-task` WebSocket | task_generator |
| 5 | `src/agents/project/session_manager.py` | — |
| 6 | `src/api/main.py` — 注册路由 | project.py |

其他文件（`__init__.py`, `coordinator.py`, prompts yaml, `code_generator.py`）随步骤创建。

---

## 新增文件列表

```
src/
├── api/routers/project.py
├── agents/project/
│   ├── __init__.py
│   ├── coordinator.py
│   ├── session_manager.py
│   └── agents/
│       ├── __init__.py
│       ├── task_parser.py
│       ├── task_generator.py
│       └── code_generator.py        ← Phase 3 实现，Phase 1 只建空文件
│   └── prompts/
│       ├── zh/task_generation.yaml
│       ├── zh/code_generation.yaml
│       ├── en/task_generation.yaml
│       └── en/code_generation.yaml
```

修改文件：
- `src/api/main.py` — 添加路由注册（约 L190-200）

---

## WebSocket 消息协议

```
客户端 → 服务端：
{"theme": "...", "reference_structure": {...}, "kb_name": "...", "web_search": true, "session_id": null}

服务端 → 客户端（顺序）：
{"type": "status",     "content": "正在检索知识库..."}
{"type": "log",        "content": "RAG 查询: ..."}
{"type": "chunk",      "content": "## 一、课程背景\n"}
{"type": "section",    "section": "cover", "content": "..."}
{"type": "token_stats","stats": {"calls": 3, "tokens": 2048}}
{"type": "complete",   "session_id": "proj_xxx", "task_md_path": "...", "task_content": "..."}
{"type": "error",      "content": "错误信息"}
```

---

## 边缘情况

### task_parser.py
- 上传空 .docx（0 段落）→ 返回空 sections dict，不报错
- .docx 无标题段落（全为正文）→ 将所有内容归入 `raw_text`，sections 为空
- .pdf 是扫描件（无文字层）→ 捕获异常，返回 `{"error": "scanned_pdf", "raw_text": ""}`
- 文件扩展名与实际格式不符（伪装的 .docx）→ 捕获 `PackageNotFoundError`，返回错误
- 超大文件（>50MB）→ 解析可能慢，不设超时，但结构摘要截断到 2000 字符

### upload-reference 端点
- 上传非 .docx/.pdf 文件 → 返回 HTTP 400，message 说明支持格式
- 文件名含中文/特殊字符 → 用 `uuid` 生成存储文件名，不用原始文件名
- 并发上传两个文件 → 各自独立处理，互不影响

### task_generator.py
- `kb_name` 为 None 且 `web_search=False` → 仅凭 LLM 知识生成，发 status 提示"无外部知识源"
- RAG 检索返回空 → `rag_context = ""`，继续生成，不中断
- LLM 超时（单章节）→ 抛 `LLMTimeoutError`，由 factory 重试机制处理（最多 5 次）
- LLM 返回内容含 `</think>` 思考标签 → `clean_thinking_tags()` 已处理
- 某章节 LLM 返回空字符串 → 保留空 section，最终 markdown 有空章节标题，不中断整体流程
- 生成中途 WebSocket 断开 → `ws_callback` 写入 queue 时队列已满（不会，asyncio.Queue 默认无上限），pusher 捕获异常退出，主协程继续生成到完成后静默结束

### session_manager.py
- 并发写入同一 sessions.json → 使用文件锁（`fcntl.flock` 或 `asyncio.Lock`）
- sessions.json 被手动损坏（非法 JSON）→ 捕获 `json.JSONDecodeError`，重置为空列表，记录警告日志
- 超过 100 条会话 → 删除最旧的，保留最新 100 条（同 solve/session_manager.py 模式）
- 查询不存在的 session_id → 返回 `None`，路由层返回 HTTP 404

---

## 测试用例

### tests/test_task_parser.py

```python
import pytest
from pathlib import Path
from src.agents.project.agents.task_parser import TaskParser

SAMPLE_DOCX = Path("book/嵌入式开发暑期实习任务书.docx")

def test_parse_real_docx_has_all_sections():
    """真实任务书应解析出全部 9 个 section key"""
    parser = TaskParser()
    result = parser.parse_docx(str(SAMPLE_DOCX))
    assert "sections" in result
    expected = {"cover", "objectives", "modules", "details", "requirements", "deliverables", "grading", "schedule", "references"}
    found = set(result["sections"].keys())
    # 至少命中 6 个章节（允许文档不完整）
    assert len(found & expected) >= 6

def test_parse_docx_returns_raw_text():
    parser = TaskParser()
    result = parser.parse_docx(str(SAMPLE_DOCX))
    assert len(result.get("raw_text", "")) > 100

def test_parse_nonexistent_file_raises():
    parser = TaskParser()
    with pytest.raises(FileNotFoundError):
        parser.parse_docx("/nonexistent/path.docx")

def test_parse_wrong_extension_raises():
    """传入非 docx 文件应抛出异常而非静默失败"""
    import tempfile, os
    with tempfile.NamedTemporaryFile(suffix=".docx", delete=False) as f:
        f.write(b"not a real docx")
        tmp = f.name
    try:
        parser = TaskParser()
        with pytest.raises(Exception):  # PackageNotFoundError or similar
            parser.parse_docx(tmp)
    finally:
        os.unlink(tmp)

def test_extract_structure_summary_truncates():
    """结构摘要不超过 2000 字符"""
    parser = TaskParser()
    result = parser.parse_docx(str(SAMPLE_DOCX))
    summary = parser.extract_structure_summary(result)
    assert len(summary) <= 2000
```

### tests/test_project_router.py

```python
import pytest
from fastapi.testclient import TestClient
from src.api.main import app

client = TestClient(app)

def test_upload_reference_valid_docx():
    with open("book/嵌入式开发暑期实习任务书.docx", "rb") as f:
        resp = client.post("/api/v1/project/upload-reference", files={"files": ("task.docx", f, "application/vnd.openxmlformats-officedocument.wordprocessingml.document")})
    assert resp.status_code == 200
    data = resp.json()
    assert "structure" in data
    assert "session_upload_id" in data or "reference_id" in data

def test_upload_reference_invalid_type():
    from io import BytesIO
    resp = client.post("/api/v1/project/upload-reference", files={"files": ("test.txt", BytesIO(b"hello"), "text/plain")})
    assert resp.status_code == 400

def test_list_sessions_empty():
    resp = client.get("/api/v1/project/sessions")
    assert resp.status_code == 200
    assert "sessions" in resp.json()

def test_get_nonexistent_session():
    resp = client.get("/api/v1/project/sessions/proj_nonexistent_id")
    assert resp.status_code == 404
```

### tests/test_session_manager.py

```python
import pytest, json, tempfile, os
from unittest.mock import patch
from src.agents.project.session_manager import ProjectSessionManager

def test_create_and_retrieve_session(tmp_path):
    with patch("src.agents.project.session_manager.SESSION_FILE", str(tmp_path / "sessions.json")):
        mgr = ProjectSessionManager()
        sid = mgr.create_session(theme="ROS 导航", kb_name="kb1")
        assert sid.startswith("proj_")
        session = mgr.get_session(sid)
        assert session["theme"] == "ROS 导航"
        assert session["status"] == "init"

def test_update_session(tmp_path):
    with patch("src.agents.project.session_manager.SESSION_FILE", str(tmp_path / "sessions.json")):
        mgr = ProjectSessionManager()
        sid = mgr.create_session(theme="test")
        mgr.update_session(sid, status="task_generated", task_md_path="/some/path.md")
        session = mgr.get_session(sid)
        assert session["status"] == "task_generated"

def test_max_100_sessions(tmp_path):
    with patch("src.agents.project.session_manager.SESSION_FILE", str(tmp_path / "sessions.json")):
        mgr = ProjectSessionManager()
        for i in range(105):
            mgr.create_session(theme=f"theme_{i}")
        sessions = mgr.list_sessions()
        assert len(sessions) <= 100

def test_corrupted_json_resets(tmp_path):
    sessions_file = tmp_path / "sessions.json"
    sessions_file.write_text("NOT VALID JSON")
    with patch("src.agents.project.session_manager.SESSION_FILE", str(sessions_file)):
        mgr = ProjectSessionManager()
        # 应能正常创建新会话，不崩溃
        sid = mgr.create_session(theme="recovery test")
        assert sid is not None
```

### tests/test_task_generator.py

```python
import pytest
from unittest.mock import AsyncMock, patch, MagicMock

@pytest.mark.asyncio
async def test_generate_without_rag_or_search():
    """无 RAG 无 Web 搜索时也能生成完整任务书"""
    from src.agents.project.agents.task_generator import TaskGenerator
    logs = []

    async def fake_callback(msg):
        logs.append(msg)

    gen = TaskGenerator(api_key="test", base_url="http://localhost:9999")

    # Mock LLM stream
    async def fake_stream(*args, **kwargs):
        yield "# 测试任务书内容\n"

    with patch("src.agents.project.agents.task_generator.llm_factory.stream", fake_stream):
        result = await gen.generate(
            theme="测试主题",
            reference_structure={},
            kb_name=None,
            web_search=False,
            ws_callback=fake_callback,
        )

    assert "content" in result
    assert "md_path" in result
    # status 消息应包含"无外部知识源"提示
    status_msgs = [l["content"] for l in logs if l.get("type") == "status"]
    assert any("无外部知识源" in m or "知识库" in m for m in status_msgs)

@pytest.mark.asyncio
async def test_generate_all_9_sections_present():
    """生成结果的 markdown 应包含 9 个章节标题"""
    # ... mock LLM 返回各章节内容，验证 full_md 包含所有标题
```

---

## 主动测试策略

完成每个步骤后立即运行：

```bash
# Step 1 完成后
conda run -n deeptutor pytest tests/test_task_parser.py -v

# Step 2 完成后
conda run -n deeptutor pytest tests/test_project_router.py::test_upload_reference_valid_docx -v

# Step 4 完成后（WebSocket 测试）
conda run -n deeptutor pytest tests/test_project_router.py -v

# Step 5 完成后
conda run -n deeptutor pytest tests/test_session_manager.py -v

# Step 6 完成后（整体冒烟测试）
conda run -n deeptutor python -c "
from src.api.main import app
routes = [r.path for r in app.routes]
assert '/api/v1/project/upload-reference' in routes
assert '/api/v1/project/generate-task' in routes
print('路由注册正常:', [r for r in routes if 'project' in r])
"
```

---

## Bug 处理协议

1. **复现**：写最小化的 pytest 测试，使其失败（`assert False` 或实际错误）。
2. **修复**：只改最小范围代码，不动无关逻辑。
3. **验证**：运行测试，确认由红转绿。
4. **反思**：在本文件 [反思记录] 节添加一条，说明根因和预防措施。

---

## 反思记录

*每次纠错后在此追加，格式：`日期 · 问题简述 · 根因 · 预防`*

2026-03-22 · pytest async 测试不运行 · `conda run -n deeptutor pytest` 有时解析到系统 Python（/usr/bin/python3）而非 conda env，导致缺包；asyncio_mode 未在 pyproject.toml 声明 · 今后：所有测试用 `conda run -n deeptutor python -m pytest`；pyproject.toml 加 `asyncio_mode = "auto"` 后记得验证使用了正确 Python 路径

2026-03-22 · test 中使用 `asyncio.coroutines.coroutine(lambda: None)()` 作为 callback · 该用法已废弃，传入 lambda 时必须用 `async def` 函数 · 今后 mock async callback 统一写 `async def noop_callback(msg): pass`
