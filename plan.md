# 案例项目创建功能 — 详细实现计划

## 背景与目标

在 DeepTutor 中新增"案例项目创建"功能（Project Creator），与 Question Generator、Smart Solver 等并列。

**三大子功能：**
1. **任务书生成**：用户上传参考任务书 + 输入新主题，AI 生成结构相同、内容全新的任务书
2. **代码仓库生成**：基于生成的任务书，调用 Claude Agent SDK 自动创建 git 仓库并生成项目骨架代码
3. **多源信息融合**：生成过程中可检索 RAG 知识库 + 实时网络搜索

**参考任务书结构**（基于 `book/嵌入式开发暑期实习任务书.docx`）：

生成的新任务书须包含以下 9 个章节：
1. 封面信息（课程名、专业、实习时间）
2. 课程背景与目标（4 条目标）
3. 模块概述（3-4 个模块）
4. 各模块设计内容（带编号子任务，含技术要求）
5. 作业要求（格式、页数、报告提纲）
6. 提交成果（代码、报告、视频、数据）
7. 成绩考核（评分维度 + 分级标准）
8. 时间安排（各阶段日期）
9. 参考资源（附录链接 + 教材书单）

---

## 整体架构

```
用户上传参考任务书(.docx/.pdf) + 输入新主题
              ↓
     ┌── Step 1: 任务书生成 ──┐
     │  task_parser.py        │  解析参考文档结构
     │  task_generator.py     │  RAG检索 + Web搜索 + LLM生成
     │  WebSocket流式输出      │  实时推送给前端
     └────────────────────────┘
              ↓
     用户在前端审阅/编辑 → 下载 md/docx
              ↓
     ┌── Step 2: 代码仓库生成 ──┐
     │  code_generator.py       │  Claude Agent SDK
     │  创建 git 仓库             │  data/user/projects/{id}/repo/
     │  生成项目骨架代码          │  WebSocket推送Agent日志
     └──────────────────────────┘
              ↓
     前端展示文件树 + 提供下载
```

---

## Phase 1：后端 — 任务书生成

### 1.1 新增文件结构

```
src/
├── api/routers/project.py              ← 新建：路由定义
├── agents/project/
│   ├── __init__.py                     ← 新建
│   ├── coordinator.py                  ← 新建：主协调器
│   ├── session_manager.py              ← 新建：会话存储
│   ├── agents/
│   │   ├── __init__.py                 ← 新建
│   │   ├── task_parser.py              ← 新建：解析参考任务书
│   │   ├── task_generator.py           ← 新建：生成新任务书
│   │   └── code_generator.py           ← 新建：代码仓库生成
│   └── prompts/
│       ├── zh/
│       │   ├── task_generation.yaml    ← 新建：中文提示词
│       │   └── code_generation.yaml    ← 新建
│       └── en/
│           ├── task_generation.yaml    ← 新建：英文提示词
│           └── code_generation.yaml    ← 新建
```

### 1.2 路由文件 `src/api/routers/project.py`

参考 `src/api/routers/question.py` 的 WebSocket 模式。

**路由定义：**
```python
router = APIRouter()

# REST 端点
@router.post("/project/upload-reference")
async def upload_reference(files: list[UploadFile] = File(...)) -> dict:
    """上传参考任务书，解析并返回文档结构"""

@router.get("/project/sessions")
async def list_sessions(limit: int = 20) -> dict:
    """获取历史项目列表"""

@router.get("/project/sessions/{session_id}")
async def get_session(session_id: str) -> dict:
    """获取单个项目详情"""

@router.delete("/project/sessions/{session_id}")
async def delete_session(session_id: str) -> dict:
    """删除项目"""

@router.get("/project/{session_id}/download-task")
async def download_task(session_id: str, format: str = "md") -> FileResponse:
    """下载任务书（md 或 docx 格式）"""

# WebSocket 端点
@router.websocket("/project/generate-task")
async def websocket_generate_task(websocket: WebSocket):
    """流式生成新任务书"""

@router.websocket("/project/generate-code")
async def websocket_generate_code(websocket: WebSocket):
    """流式生成代码仓库（调用 Claude Agent SDK）"""
```

**WebSocket 消息协议（沿用现有约定）：**
```python
# 客户端 → 服务端（generate-task）
{
    "theme": "ROS 机器人导航实习",
    "reference_structure": { ... },   # upload-reference 返回的结构
    "kb_name": "raspberry_pi_cookbook",
    "web_search": true,
    "session_id": null                # null=新建，有值=继续
}

# 服务端 → 客户端（流式消息）
{"type": "status",    "content": "正在检索知识库..."}
{"type": "log",       "content": "RAG 查询: 机器人导航基础..."}
{"type": "chunk",     "content": "## 一、课程背景\n"}  # 任务书内容逐步输出
{"type": "section",   "section": "background", "content": "..."}  # 按章节通知
{"type": "token_stats","stats": {"calls": 3, "tokens": 2048}}
{"type": "complete",  "session_id": "proj_xxx", "output_dir": "..."}
{"type": "error",     "content": "错误信息"}
```

**generate-task 端点完整实现逻辑：**
```python
@router.websocket("/project/generate-task")
async def websocket_generate_task(websocket: WebSocket):
    await websocket.accept()
    log_queue = asyncio.Queue()

    try:
        # 1. 接收配置
        data = await websocket.receive_json()
        theme = data.get("theme", "")
        reference_structure = data.get("reference_structure", {})
        kb_name = data.get("kb_name")
        web_search = data.get("web_search", False)
        session_id = data.get("session_id")

        # 2. 获取 LLM 配置（复用现有 config）
        llm_config = get_llm_config()

        # 3. 创建或恢复会话
        session_mgr = ProjectSessionManager()
        if not session_id:
            session_id = session_mgr.create_session(theme=theme, kb_name=kb_name)

        # 4. 初始化 Coordinator
        coordinator = ProjectCoordinator(
            api_key=llm_config.api_key,
            base_url=llm_config.base_url,
            kb_name=kb_name,
            web_search_enabled=web_search,
            language=get_ui_language(),
            output_dir=str(get_project_output_dir(session_id))
        )

        # 5. 设置 WebSocket 回调（参考 question.py 模式）
        async def ws_callback(msg: dict):
            await log_queue.put(msg)
        coordinator.set_ws_callback(ws_callback)

        # 6. 启动日志推送任务
        async def log_pusher():
            while True:
                entry = await log_queue.get()
                if entry is None:
                    break
                try:
                    await websocket.send_json(entry)
                except Exception:
                    break

        pusher_task = asyncio.create_task(log_pusher())

        # 7. 执行生成
        result = await coordinator.generate_task_document(
            theme=theme,
            reference_structure=reference_structure
        )

        # 8. 发送完成
        await log_queue.put(None)
        await pusher_task
        await websocket.send_json({
            "type": "complete",
            "session_id": session_id,
            "task_md_path": result["md_path"],
            "task_content": result["content"]
        })

    except WebSocketDisconnect:
        pass
    except Exception as e:
        await websocket.send_json({"type": "error", "content": str(e)})
    finally:
        await websocket.close()
```

### 1.3 任务书解析器 `src/agents/project/agents/task_parser.py`

```python
class TaskParser:
    """解析参考任务书，提取文档结构"""

    SECTION_PATTERNS = {
        "cover": ["课程名称", "专业", "实习时间", "封面"],
        "objectives": ["课程目标", "设计目标", "学习目标"],
        "modules": ["模块", "阶段", "任务概述"],
        "details": ["设计内容", "任务内容", "具体要求"],
        "requirements": ["作业要求", "报告要求", "提交格式"],
        "deliverables": ["提交成果", "交付物"],
        "grading": ["成绩考核", "评分标准", "考核方式"],
        "schedule": ["时间安排", "进度计划"],
        "references": ["参考资料", "教材", "附录"]
    }

    def parse_docx(self, file_path: str) -> dict:
        """解析 .docx 文件，返回结构化内容"""
        from docx import Document
        doc = Document(file_path)
        # 返回格式：
        # {
        #   "sections": {
        #     "cover": {"title": "...", "content": "..."},
        #     "objectives": {...},
        #     ...
        #   },
        #   "module_count": 3,
        #   "has_grading_table": True,
        #   "raw_text": "完整文本"
        # }

    def parse_pdf(self, file_path: str) -> dict:
        """解析 .pdf 文件（使用 PyMuPDF）"""

    def extract_structure_summary(self, parsed: dict) -> str:
        """生成结构摘要，用于提示词"""
```

### 1.4 任务书生成器 `src/agents/project/agents/task_generator.py`

```python
class TaskGenerator:
    """
    生成新任务书，流程：
    1. RAG 检索相关知识
    2. Web Search 获取最新资料（可选）
    3. 逐章节流式生成
    4. 导出 Markdown + docx
    """

    async def generate(
        self,
        theme: str,
        reference_structure: dict,
        kb_name: str | None,
        web_search: bool,
        ws_callback: Callable
    ) -> dict:
        # Step 1: RAG 检索
        await ws_callback({"type": "status", "content": "正在检索知识库..."})
        rag_context = await self._rag_retrieve(theme, kb_name)

        # Step 2: Web 搜索
        if web_search:
            await ws_callback({"type": "status", "content": "正在网络搜索..."})
            web_context = await self._web_search(theme)
        else:
            web_context = ""

        # Step 3: 逐章节生成（流式）
        sections = {}
        for section_key, section_title in SECTION_ORDER:
            await ws_callback({"type": "status", "content": f"正在生成：{section_title}"})
            prompt = self._build_section_prompt(
                section_key, theme, reference_structure,
                rag_context, web_context, sections
            )
            section_content = ""
            async for chunk in llm_stream(prompt, ...):
                section_content += chunk
                await ws_callback({"type": "chunk", "content": chunk})
            sections[section_key] = section_content
            await ws_callback({"type": "section", "section": section_key, "content": section_content})

        # Step 4: 组合并导出
        full_md = self._assemble_markdown(sections)
        md_path = self._save_markdown(full_md)
        docx_path = self._export_docx(full_md)

        return {"content": full_md, "md_path": str(md_path), "docx_path": str(docx_path)}

    async def _rag_retrieve(self, theme: str, kb_name: str) -> str:
        """调用现有 RAG 服务检索相关内容"""
        # 复用 src/services/rag/ 中现有的检索逻辑

    async def _web_search(self, theme: str) -> str:
        """调用现有 Search 服务"""
        # 复用 src/services/search/ 中现有逻辑
```

**提示词文件 `prompts/zh/task_generation.yaml`：**
```yaml
system: |
  你是一个专业的课程任务书设计专家。请基于参考任务书的结构，
  为新主题生成结构完全相同、内容全新的任务书章节。
  要求：内容专业、具体、可执行；技术细节准确；难度适中。

section_prompts:
  cover: |
    生成任务书封面信息，主题：{theme}
    包含：课程名称、适用专业、实习时间段
    参考格式：{reference_cover}

  objectives: |
    为主题"{theme}"生成4条课程目标，参考以下知识：
    {rag_context}
    参考结构：{reference_objectives}
    要求：涵盖理论实践、自主学习、表达能力、创新动手四个维度

  modules: |
    为主题"{theme}"设计3-4个实践模块概述
    已有知识背景：{rag_context}
    网络资料：{web_context}
    参考结构：{reference_modules}

  # ... 其余章节
```

### 1.5 会话管理器 `src/agents/project/session_manager.py`

参考 `src/agents/solve/session_manager.py` 实现，存储到 `data/user/project_sessions.json`。

```python
# 会话数据结构
{
    "session_id": "proj_1726543210000_a1b2c3",
    "theme": "ROS 机器人导航实习",
    "kb_name": "raspberry_pi_cookbook",
    "status": "task_generated",  # init | task_generating | task_generated | code_generating | complete
    "task_md_path": "data/user/projects/proj_xxx/generated_task.md",
    "task_docx_path": "data/user/projects/proj_xxx/generated_task.docx",
    "repo_path": "data/user/projects/proj_xxx/repo/",
    "reference_structure": { ... },
    "token_stats": { ... },
    "created_at": 1726543210.123,
    "updated_at": 1726543220.456
}
```

### 1.6 注册路由 `src/api/main.py`

在现有路由注册块中添加（约第190-200行）：
```python
from src.api.routers import project
app.include_router(project.router, prefix="/api/v1", tags=["project"])
```

### 1.7 数据目录结构

```
data/user/projects/
├── project_sessions.json              ← 会话索引
└── proj_1726543210000_a1b2c3/
    ├── reference_task.docx            ← 用户上传的参考文档
    ├── reference_structure.json       ← 解析出的结构
    ├── generated_task.md              ← 生成的任务书（Markdown）
    ├── generated_task.docx            ← 生成的任务书（Word）
    └── repo/                          ← Claude Agent SDK 生成的代码
        ├── .git/
        ├── README.md
        ├── requirements.txt
        └── src/
```

---

## Phase 2：前端 — 任务书生成 UI

### 2.1 修改 `web/components/Sidebar.tsx`

**第 1 步：** 在文件顶部 import 中添加图标（约第 8-28 行）：
```typescript
import { FolderGit2 } from "lucide-react";
```

**第 2 步：** 在 `ALL_NAV_ITEMS` 中添加（约第 42-53 行）：
```typescript
"/project": { icon: FolderGit2, nameKey: "Project Creator" },
```

### 2.2 修改 `web/context/GlobalContext.tsx`

**第 1 步：** 添加类型定义（在现有类型定义区域）：
```typescript
interface ProjectState {
  step: "config" | "task_generating" | "task_review" | "code_generating" | "complete";
  theme: string;
  selectedKb: string;
  webSearchEnabled: boolean;
  uploadedFile: File | null;
  referenceStructure: Record<string, any> | null;
  // 任务书生成
  taskContent: string;          // 流式积累的 Markdown 内容
  taskSections: Record<string, string>;  // 已完成的章节
  currentSection: string | null;
  taskMdPath: string | null;
  taskDocxPath: string | null;
  // 代码生成
  agentLogs: AgentLogEntry[];   // Claude Agent SDK 的操作日志
  generatedFiles: FileTreeNode[];  // 生成的文件树
  repoPath: string | null;
  // 通用
  sessionId: string | null;
  logs: LogEntry[];
  tokenStats: TokenStats;
  error: string | null;
}

interface AgentLogEntry {
  timestamp: string;
  type: "tool_use" | "tool_result" | "message" | "error";
  tool?: string;          // "Write" | "Edit" | "Bash" | "Read" 等
  path?: string;          // 操作的文件路径
  content: string;
}

interface FileTreeNode {
  name: string;
  path: string;
  type: "file" | "directory";
  children?: FileTreeNode[];
}
```

**第 2 步：** 添加默认 State：
```typescript
const DEFAULT_PROJECT_STATE: ProjectState = {
  step: "config",
  theme: "",
  selectedKb: "",
  webSearchEnabled: false,
  uploadedFile: null,
  referenceStructure: null,
  taskContent: "",
  taskSections: {},
  currentSection: null,
  taskMdPath: null,
  taskDocxPath: null,
  agentLogs: [],
  generatedFiles: [],
  repoPath: null,
  sessionId: null,
  logs: [],
  tokenStats: { model: "Unknown", calls: 0, tokens: 0, input_tokens: 0, output_tokens: 0, cost: 0 },
  error: null
};
```

**第 3 步：** 在 `GlobalContextType` 中添加：
```typescript
// Project Creator
projectState: ProjectState;
setProjectState: React.Dispatch<React.SetStateAction<ProjectState>>;
uploadReference: (file: File) => Promise<void>;
startTaskGeneration: () => void;
startCodeGeneration: () => void;
resetProject: () => void;
```

**第 4 步：** 在 `DEFAULT_NAV_ORDER.learnResearch` 中添加 `"/project"`：
```typescript
learnResearch: ["/question", "/solver", "/guide", "/ideagen", "/research", "/co_writer", "/project"],
```

**第 5 步：** 实现 `startTaskGeneration` 函数（WebSocket 连接，参考 `startSolver` 模式）：
```typescript
const startTaskGeneration = () => {
  if (projectWs.current) projectWs.current.close();

  setProjectState(prev => ({ ...prev, step: "task_generating", taskContent: "", taskSections: {}, error: null }));

  const ws = new WebSocket(wsUrl("/api/v1/project/generate-task"));
  projectWs.current = ws;

  ws.onopen = () => {
    ws.send(JSON.stringify({
      theme: projectState.theme,
      reference_structure: projectState.referenceStructure,
      kb_name: projectState.selectedKb || null,
      web_search: projectState.webSearchEnabled,
      session_id: projectState.sessionId
    }));
  };

  ws.onmessage = (event) => {
    const data = JSON.parse(event.data);
    switch (data.type) {
      case "chunk":
        setProjectState(prev => ({ ...prev, taskContent: prev.taskContent + data.content }));
        break;
      case "section":
        setProjectState(prev => ({ ...prev, taskSections: { ...prev.taskSections, [data.section]: data.content }, currentSection: data.section }));
        break;
      case "status":
      case "log":
        setProjectState(prev => ({ ...prev, logs: [...prev.logs, { type: data.type, content: data.content, timestamp: Date.now() }] }));
        break;
      case "token_stats":
        setProjectState(prev => ({ ...prev, tokenStats: data.stats }));
        break;
      case "complete":
        setProjectState(prev => ({ ...prev, step: "task_review", sessionId: data.session_id, taskMdPath: data.task_md_path }));
        break;
      case "error":
        setProjectState(prev => ({ ...prev, step: "config", error: data.content }));
        break;
    }
  };
};
```

### 2.3 新建页面 `web/app/project/page.tsx`

**整体结构（4 步 Wizard）：**
```
┌─────────────────────────────────────────────────────┐
│  Project Creator                          [日志抽屉] │
├─────────────────────────────────────────────────────┤
│  Step 1: 配置     Step 2: 生成任务书                  │
│  Step 3: 审阅     Step 4: 生成代码                    │
├─────────────────────────────────────────────────────┤
│                                                     │
│  [Step 1 — 配置]                                    │
│  ┌──────────────────────┐  ┌──────────────────────┐ │
│  │ 上传参考任务书          │  │ 输入新主题             │ │
│  │ [拖拽/点击上传 .docx]  │  │ [文本输入框]           │ │
│  └──────────────────────┘  └──────────────────────┘ │
│  选择知识库: [下拉]   开启网络搜索: [Toggle]           │
│                              [开始生成任务书 →]       │
│                                                     │
│  [Step 2 — 生成任务书]                              │
│  左侧：进度状态 + 章节完成情况                         │
│  右侧：Markdown 实时渲染（流式追加）                   │
│                                                     │
│  [Step 3 — 审阅任务书]                              │
│  Markdown 完整预览（可复制）                          │
│  [下载 .md] [下载 .docx] [→ 生成代码仓库]            │
│                                                     │
│  [Step 4 — 代码生成]                                │
│  左侧：Agent 操作日志（工具调用流）                    │
│  右侧：文件树（实时更新）                              │
│  完成后：[下载 zip] [查看文件]                        │
└─────────────────────────────────────────────────────┘
```

**页面代码骨架：**
```typescript
"use client";
import { useGlobal } from "@/context/GlobalContext";
import { useTranslation } from "react-i18next";
import { apiUrl, wsUrl } from "@/lib/api";
import ReactMarkdown from "react-markdown";

export default function ProjectPage() {
  const { projectState, setProjectState, uploadReference, startTaskGeneration, startCodeGeneration, resetProject } = useGlobal();
  const { t } = useTranslation();
  const [kbs, setKbs] = useState<string[]>([]);

  // 获取知识库列表（复用现有模式）
  useEffect(() => {
    fetch(apiUrl("/api/v1/knowledge/list"))
      .then(r => r.json())
      .then(data => setKbs(data.knowledge_bases?.map((kb: any) => kb.name) || []));
  }, []);

  const step = projectState.step;

  return (
    <div className="flex h-full">
      {/* 步骤向导头部 */}
      <StepIndicator currentStep={step} steps={STEPS} />

      {/* 主内容区 */}
      <main>
        {step === "config" && <ConfigPanel ... />}
        {step === "task_generating" && <TaskGeneratingPanel ... />}
        {step === "task_review" && <TaskReviewPanel ... />}
        {step === "code_generating" && <CodeGeneratingPanel ... />}
        {step === "complete" && <CompletePanel ... />}
      </main>

      {/* 日志抽屉（复用现有 LogDrawer 组件）*/}
      <LogDrawer logs={projectState.logs} />
    </div>
  );
}
```

**Chapter 进度组件（Step 2 左侧）：**
```typescript
const SECTION_NAMES = {
  cover: "封面信息",
  objectives: "背景与目标",
  modules: "模块概述",
  details: "设计内容",
  requirements: "作业要求",
  deliverables: "提交成果",
  grading: "成绩考核",
  schedule: "时间安排",
  references: "参考资源"
};

function ChapterProgress({ sections, currentSection }) {
  return (
    <ul>
      {Object.entries(SECTION_NAMES).map(([key, name]) => (
        <li key={key} className={cn(
          "flex items-center gap-2",
          sections[key] ? "text-green-500" : key === currentSection ? "text-blue-500 animate-pulse" : "text-gray-400"
        )}>
          {sections[key] ? <CheckCircle /> : key === currentSection ? <Loader2 className="animate-spin" /> : <Circle />}
          {name}
        </li>
      ))}
    </ul>
  );
}
```

---

## Phase 3：后端 — 代码仓库生成

> ✅ **已实现**。认证方式与原计划不同：不使用 `ANTHROPIC_API_KEY`，改为复用本机
> Claude Code 的 OAuth Token（`~/.claude/.credentials.json`，Pro 订阅）。
> 不需要 `claude-agent-sdk` Python 包，统一走 CLI subprocess。

### 3.1 新增文件

| 文件 | 作用 |
|------|------|
| `src/agents/project/agents/requirement_extractor.py` | 用现有 LLM factory 从任务书提取 `RequirementSpec`（不需要 Anthropic key） |
| `src/agents/project/agents/code_generator.py` | 三阶段生成：规划(B) → 编码(C) → 验证修复(D)，subprocess 调用 claude CLI |
| `src/agents/project/agents/code_verifier.py` | pip install + 主模块导入 + pytest 冒烟测试 |

**前置条件（无需 API key）：**
```bash
# 1. claude CLI 已安装
npm install -g @anthropic-ai/claude-code

# 2. 已通过 OAuth 登录（Claude Pro 订阅）
claude login

# 凭据存储在 ~/.claude/.credentials.json，subscriptionType: "pro"
# DeepTutor 调用 claude subprocess 时不注入任何 API key，CLI 自动读取凭据
```

**RequirementSpec 数据结构（需求提取器输出）：**
```python
@dataclass
class TaskModule:
    id: str
    title: str
    objectives: list[str]
    technical_requirements: list[str]
    deliverables: list[str]

@dataclass
class RequirementSpec:
    theme: str
    tech_stack: list[str]
    environment: str
    install_requirements: list[str]
    modules: list[TaskModule]
    coverage_map: dict[str, list[str]]   # 生成后回填
```

**CodeGenerator 实际接口：**
```python
class CodeGenerator:
    """
    通过 subprocess 调用本机 claude CLI（OAuth，无 API key）。
    模型跟随 Claude Code 当前配置，不在 DeepTutor 侧指定。
    """
    CLAUDE_BIN: str  # shutil.which("claude") 或 ~/.local/bin/claude

    def __init__(self) -> None:
        # 检查 claude CLI 文件是否存在；不存在则抛 EnvironmentError
        ...

    async def generate(
        self,
        spec: RequirementSpec,    # ← 注意：不是 task_document: str
        output_dir: str,
        ws_callback: Callable,
    ) -> dict:
        # 返回：{"repo_path", "file_tree", "coverage_map", "verify_passed", "verify_report"}
        ...

    async def _run_via_cli(self, prompt, cwd, allowed_tools, ws_callback):
        process = await asyncio.create_subprocess_exec(
            self.CLAUDE_BIN, "-p", prompt,
            "--allowedTools", ",".join(allowed_tools),
            "--output-format", "stream-json",
            cwd=cwd,
            env=os.environ,    # 原样传递，不注入 ANTHROPIC_API_KEY
            ...
        )
```

### 3.2 generate-code WebSocket 端点（实际实现）

**消息协议（更新）：**
```python
# 客户端 → 服务端
{"session_id": "proj_xxx", "task_content": "## 课程背景\n..."}

# 服务端 → 客户端（顺序）
{"type": "phase",        "phase": "analysis", "content": "正在分析任务书需求..."}
{"type": "phase",        "phase": "planning", "content": "正在制定项目架构..."}
{"type": "phase",        "phase": "coding",   "content": "Claude Agent 开始编写代码..."}
{"type": "phase",        "phase": "verify",   "content": "正在验证代码可运行性..."}
{"type": "agent_log",    "log_type": "tool_use"|"tool_result"|"message"|"error",
                         "tool": "Write"|"Bash"|..., "path": "src/main.py", "content": "..."}
{"type": "file_created", "path": "src/main.py"}
{"type": "verify_result","passed": true|false, "report": "..."}
{"type": "coverage",     "map": {"module_1": ["src/main.py"]}}
{"type": "complete",     "session_id": "...", "repo_path": "...",
                         "file_tree": [...], "coverage_map": {...}, "verify_passed": true}
{"type": "error",        "content": "找不到 claude CLI..."}
```

**端点逻辑（与原计划的关键差异）：**
```python
@router.websocket("/project/generate-code")
async def websocket_generate_code(websocket: WebSocket):
    ...
    # 检查 claude CLI（不检查 ANTHROPIC_API_KEY）
    try:
        generator = CodeGenerator()    # 构造时验证 CLI 是否存在
    except EnvironmentError as e:
        await websocket.send_json({"type": "error", "content": str(e)})
        return

    # 阶段 A：需求提取（用现有 LLM factory，不需要 Anthropic key）
    extractor = RequirementExtractor()
    spec = await extractor.extract(task_content, ws_callback)

    # 阶段 B/C/D：代码生成 + 验证
    result = await generator.generate(spec=spec, output_dir=output_dir, ws_callback=ws_callback)

    await websocket.send_json({
        "type": "complete",
        "session_id": session_id,
        "repo_path": result["repo_path"],
        "file_tree": result["file_tree"],
        "coverage_map": result["coverage_map"],    # ← 原计划缺少
        "verify_passed": result["verify_passed"],   # ← 原计划缺少
    })
```

### 3.3 download-repo 端点（原计划缺少）

```python
GET /project/{session_id}/download-repo
# 将 data/user/projects/{id}/repo/ 打包为 zip 返回（排除 .git，限制 500 MB）
```

---

## Phase 4：前端 — 代码生成 UI

> ✅ **已实现**，与 Phase 3 后端同步完成。整合在 `web/app/project/page.tsx` Step 4
> 和 `web/context/GlobalContext.tsx` 中，不是独立的 Phase。

### 4.1 ProjectState 新增字段（原计划缺少）

```typescript
interface ProjectState {
  // ... 原有字段 ...
  // Phase 3 新增：
  agentLogs: AgentLogEntry[];
  generatedFiles: FileTreeNode[];
  repoPath: string | null;
  verifyPassed: boolean | null;      // ← 原计划缺少
  coverageMap: Record<string, string[]> | null;  // ← 原计划缺少
}
```

### 4.2 GlobalContext 中的 startCodeGeneration（实际实现）

```typescript
const startCodeGeneration = useCallback(() => {
  if (projectWs.current) projectWs.current.close();
  setProjectState(prev => ({
    ...prev,
    step: "code_generating",
    agentLogs: [], generatedFiles: [],
    repoPath: null, verifyPassed: null, coverageMap: null, error: null,
  }));

  // 注意：不使用 wsUrl()，直接替换 env var 以避免模块初始化时的异常
  const base = process.env.NEXT_PUBLIC_API_BASE || "";
  const wsBase = base.replace(/^http:/, "ws:").replace(/^https:/, "wss:");
  const ws = new WebSocket(`${wsBase}/api/v1/project/generate-code`);
  projectWs.current = ws;

  ws.onopen = () => {
    ws.send(JSON.stringify({
      session_id: projectStateRef.current.sessionId,    // 用 ref 避免闭包问题
      task_content: projectStateRef.current.taskContent,
    }));
  };

  ws.onmessage = (event) => {
    const data = JSON.parse(event.data);
    switch (data.type) {
      case "phase":         // ← 原计划缺少
        setProjectState(prev => ({ ...prev, logs: [...prev.logs, { type: "status", content: data.content, timestamp: Date.now() }] }));
        break;
      case "agent_log":
        setProjectState(prev => ({
          ...prev,
          agentLogs: [...prev.agentLogs.slice(-499), { timestamp: new Date().toLocaleTimeString(), type: data.log_type, tool: data.tool, path: data.path, content: data.content }],
        }));
        break;
      case "verify_result": // ← 原计划缺少
        setProjectState(prev => ({ ...prev, logs: [...prev.logs, { type: data.passed ? "status" : "error", content: data.passed ? `验证通过：${data.report}` : `验证失败：${data.report}`, timestamp: Date.now() }] }));
        break;
      case "coverage":      // ← 原计划缺少
        setProjectState(prev => ({ ...prev, coverageMap: data.map }));
        break;
      case "complete":
        setProjectState(prev => ({
          ...prev,
          step: "complete",
          repoPath: data.repo_path,
          generatedFiles: data.file_tree,
          coverageMap: data.coverage_map,    // ← 原计划缺少
          verifyPassed: data.verify_passed,   // ← 原计划缺少
        }));
        break;
      case "error":
        setProjectState(prev => ({ ...prev, step: "task_review", error: data.content }));
        break;
    }
  };
}, []);
```

### 4.3 Step 4 页面布局（实际实现）

```
左 1/3：Agent 操作日志（深色终端风格，滚动，最多 500 条）
右 2/3：
  ├── 验证状态栏（绿色/红色）
  ├── 文件树（目录/文件图标，递归渲染）
  ├── 需求覆盖率（模块 ID → 文件列表）
  └── 操作按钮：[下载代码 zip] [新建项目]
```

下载按钮调用 `GET /api/v1/project/{sessionId}/download-repo`，返回 zip 包。

---

## Phase 5：集成测试

### 5.1 后端单元测试（实际测试文件）

```
tests/test_task_parser.py       —  7 个测试，覆盖 docx/pdf 解析
tests/test_session_manager.py   —  8 个测试，覆盖会话 CRUD
tests/test_project_router.py    —  8 个测试，覆盖 REST 端点
tests/test_task_generator.py    —  3 个测试，覆盖流式生成逻辑
tests/test_code_generator.py    — 17 个测试，覆盖 RequirementExtractor / CodeVerifier / CodeGenerator
```

关键测试用例（已实现）：
```python
# tests/test_code_generator.py
async def test_extract_valid_json():                    # RequirementExtractor 正常解析
async def test_extract_invalid_json_returns_minimal_spec()  # LLM 返回无效 JSON 时降级
async def test_verify_syntax_error_fails():             # CodeVerifier 检测语法错误
def test_missing_claude_bin_raises():                   # CLI 不存在时抛 EnvironmentError
def test_build_file_tree_excludes_git():                # 文件树排除 .git 目录
```

### 5.2 端到端测试步骤

1. 上传 `book/嵌入式开发暑期实习任务书.docx`，主题设为 `ROS 机器人导航实习`
2. 选择知识库 `raspberry_pi_cookbook`，开启 Web Search
3. 点击生成，观察 9 个章节逐步出现，Markdown 实时渲染
4. 下载 `.md` 和 `.docx`，检查格式与结构
5. 点击"生成代码仓库"，观察 Agent 日志（git init、文件写入等）
6. 验证 `data/user/projects/{id}/repo/` 下生成 README.md、src/ 目录等
7. 在前端文件树中确认所有文件显示正确

---

## 实现优先级与顺序

| 顺序 | 任务 | 状态 | 依赖 |
|------|------|------|------|
| 1 | task_parser.py | ✅ | python-docx |
| 2 | project.py (upload-reference) | ✅ | task_parser |
| 3 | task_generator.py (基础版，无流式) | ✅ | 现有 LLM |
| 4 | project.py (generate-task WebSocket) | ✅ | task_generator |
| 5 | session_manager.py | ✅ | — |
| 6 | main.py 注册路由 | ✅ | project.py |
| 7 | Sidebar.tsx 添加菜单 | ✅ | — |
| 8 | GlobalContext.tsx 添加 state | ✅ | — |
| 9 | project/page.tsx (Step 1-3) | ✅ | 后端 Phase 1 |
| 10 | task_generator.py 改为流式输出 | ✅ | 基础版完成 |
| 11 | 添加 RAG + Web Search | ✅ | 流式版完成 |
| 12 | 确认 claude CLI 已安装并 OAuth 登录 | ✅ | claude login |
| 13 | requirement_extractor.py + code_verifier.py | ✅ | 现有 LLM factory |
| 14 | code_generator.py（CLI subprocess，无 API key） | ✅ | claude CLI + OAuth |
| 15 | project.py (generate-code WebSocket + download-repo) | ✅ | code_generator |
| 16 | project/page.tsx (Step 4) + GlobalContext startCodeGeneration | ✅ | 后端 Phase 3 |
| 17 | 端到端集成测试 | ⬜ | 全部完成 |

---

## 前置依赖清单

```bash
# Python（deeptutor 环境）
# python-docx 已安装（deeptutor 环境），无需额外安装
# claude-agent-sdk 不需要（已放弃该方案）

# 环境变量（.env）
# ANTHROPIC_API_KEY 不需要（代码生成走 OAuth，不走 API key）
# 任务书生成（Phase 1）继续使用 DASHSCOPE_API_KEY / 字节 API key

# Claude CLI（必须安装并登录，代码生成的唯一入口）
npm install -g @anthropic-ai/claude-code
claude login    # 浏览器完成 OAuth 授权，凭据存入 ~/.claude/.credentials.json
```

---

## 关键文件路径速查

| 文件 | 路径 | 操作 | 状态 |
|------|------|------|------|
| 路由 | `src/api/routers/project.py` | 新建 | ✅ |
| 协调器 | `src/agents/project/coordinator.py` | 新建 | ✅ |
| 文档解析 | `src/agents/project/agents/task_parser.py` | 新建 | ✅ |
| 任务书生成 | `src/agents/project/agents/task_generator.py` | 新建 | ✅ |
| 需求提取 | `src/agents/project/agents/requirement_extractor.py` | 新建 | ✅ |
| 代码生成 | `src/agents/project/agents/code_generator.py` | 新建 | ✅ |
| 代码验证 | `src/agents/project/agents/code_verifier.py` | 新建 | ✅ |
| 会话管理 | `src/agents/project/session_manager.py` | 新建 | ✅ |
| 路由注册 | `src/api/main.py` | 修改 | ✅ |
| 侧边栏 | `web/components/Sidebar.tsx` | 修改 | ✅ |
| 全局状态 | `web/context/GlobalContext.tsx` | 修改 | ✅ |
| 前端页面 | `web/app/project/page.tsx` | 新建 | ✅ |
| 提示词(中) | `src/agents/project/prompts/zh/*.yaml` | 新建 | ✅ |
| 参考路由 | `src/api/routers/question.py` | 只读参考 | — |
| 参考 Agent | `src/agents/question/coordinator.py` | 只读参考 | — |
| 参考会话 | `src/agents/solve/session_manager.py` | 只读参考 | — |
| 参考页面 | `web/app/question/page.tsx` | 只读参考 | — |
