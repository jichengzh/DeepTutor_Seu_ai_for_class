# CLAUDE_phase2.md — Phase 2: 前端任务书生成 UI

## 规则

- **不写兼容性代码**，除非用户明确要求。
- 需求不明确时向用户提问，不猜测。
- 每完成一个文件，立即运行对应测试，确认通过再继续。
- 发现 bug 时：先写能重现的测试 → 修复 → 确认通过 → 反思根因。

---

## Phase 2 任务计划

按顺序实现，每步完成后立即测试：

| 步骤 | 文件 | 操作 |
|------|------|------|
| 1 | `web/components/Sidebar.tsx` | 添加 `FolderGit2` 图标 + `/project` 导航项 |
| 2 | `web/context/GlobalContext.tsx` | 添加类型定义、默认状态、`GlobalContextType` 新增字段 |
| 3 | `web/context/GlobalContext.tsx` | 实现 `uploadReference`、`startTaskGeneration`、`resetProject` 函数 |
| 4 | `web/app/project/page.tsx` | 新建 4 步 Wizard 页面（Step 1-3，Step 4 占位） |

---

## 修改文件速查

| 文件 | 改动位置 | 改动类型 |
|------|----------|---------|
| `web/components/Sidebar.tsx` | L8-28 import 区块 | 添加 `FolderGit2` |
| `web/components/Sidebar.tsx` | L42-53 `ALL_NAV_ITEMS` | 添加 `"/project"` 条目 |
| `web/context/GlobalContext.tsx` | 类型定义区（`interface` 块） | 新增 `ProjectState`、`AgentLogEntry`、`FileTreeNode` |
| `web/context/GlobalContext.tsx` | `DEFAULT_NAV_ORDER.learnResearch`（L613-620） | 追加 `"/project"` |
| `web/context/GlobalContext.tsx` | `interface GlobalContextType`（L255-326） | 新增 Project 相关字段 |
| `web/context/GlobalContext.tsx` | Provider 函数体 | 新增状态、函数实现、`projectWs` ref |
| `web/context/GlobalContext.tsx` | Provider `return` 的 value 对象 | 暴露新字段 |
| `web/app/project/page.tsx` | — | 新建文件 |

---

## 关键数据结构

### ProjectState（添加到 GlobalContext.tsx 类型区）

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
  taskSections: Record<string, string>;
  currentSection: string | null;
  taskMdPath: string | null;
  taskDocxPath: string | null;
  // 通用
  sessionId: string | null;
  logs: LogEntry[];
  tokenStats: TokenStats;
  error: string | null;
}

interface AgentLogEntry {
  timestamp: string;
  type: "tool_use" | "tool_result" | "message" | "error";
  tool?: string;
  path?: string;
  content: string;
}

interface FileTreeNode {
  name: string;
  path: string;
  type: "file" | "directory";
  children?: FileTreeNode[];
}
```

### DEFAULT_PROJECT_STATE

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
  sessionId: null,
  logs: [],
  tokenStats: { model: "Unknown", calls: 0, tokens: 0, input_tokens: 0, output_tokens: 0, cost: 0 },
  error: null,
};
```

### GlobalContextType 新增字段

```typescript
// Project Creator
projectState: ProjectState;
setProjectState: React.Dispatch<React.SetStateAction<ProjectState>>;
uploadReference: (file: File) => Promise<void>;
startTaskGeneration: () => void;
resetProject: () => void;
```

---

## WebSocket 消息处理（startTaskGeneration）

参考 `startSolver` 的实现模式（`useRef<WebSocket | null>`）：

```typescript
const projectWs = useRef<WebSocket | null>(null);

const startTaskGeneration = useCallback(() => {
  if (projectWs.current) projectWs.current.close();

  setProjectState(prev => ({
    ...prev,
    step: "task_generating",
    taskContent: "",
    taskSections: {},
    currentSection: null,
    error: null,
    logs: [],
  }));

  const ws = new WebSocket(wsUrl("/api/v1/project/generate-task"));
  projectWs.current = ws;

  ws.onopen = () => {
    ws.send(JSON.stringify({
      theme: projectStateRef.current.theme,          // 用 ref 避免闭包问题
      reference_structure: projectStateRef.current.referenceStructure,
      kb_name: projectStateRef.current.selectedKb || null,
      web_search: projectStateRef.current.webSearchEnabled,
      session_id: projectStateRef.current.sessionId,
    }));
  };

  ws.onmessage = (event) => {
    const data = JSON.parse(event.data);
    switch (data.type) {
      case "chunk":
        setProjectState(prev => ({ ...prev, taskContent: prev.taskContent + data.content }));
        break;
      case "section":
        setProjectState(prev => ({
          ...prev,
          taskSections: { ...prev.taskSections, [data.section]: data.content },
          currentSection: data.section,
        }));
        break;
      case "status":
      case "log":
        setProjectState(prev => ({
          ...prev,
          logs: [...prev.logs, { type: data.type, content: data.content, timestamp: Date.now() }],
        }));
        break;
      case "token_stats":
        setProjectState(prev => ({ ...prev, tokenStats: data.stats }));
        break;
      case "complete":
        setProjectState(prev => ({
          ...prev,
          step: "task_review",
          sessionId: data.session_id,
          taskMdPath: data.task_md_path,
        }));
        break;
      case "error":
        setProjectState(prev => ({ ...prev, step: "config", error: data.content }));
        break;
    }
  };

  ws.onerror = () => {
    setProjectState(prev => ({ ...prev, step: "config", error: "WebSocket connection error" }));
  };
}, []);
```

> **注意**：`ws.onopen` 中读取 projectState 会遇到 React 闭包问题（捕获的是旧值）。
> 解决方案：用 `useRef` 同步最新 state 值，参见闭包边缘情况一节。

---

## 页面结构（project/page.tsx）

```
web/app/project/
└── page.tsx      ← 新建（单文件，子组件定义在同一文件末尾）
```

4 步 Wizard 布局：

```
┌──────────────── StepIndicator ─────────────────┐
│  ① 配置  →  ② 生成  →  ③ 审阅  →  ④ 代码     │
└────────────────────────────────────────────────┘
┌──────────────── 主内容区 ───────────────────────┐
│ Step 1 (config):                                │
│   左：文件上传区（.docx/.pdf）                   │
│   右：主题输入 + KB下拉 + WebSearch toggle       │
│   底：[开始生成 →] 按钮                          │
│                                                 │
│ Step 2 (task_generating):                       │
│   左（1/3）：ChapterProgress 进度列表            │
│   右（2/3）：Markdown 实时渲染（流式追加）         │
│                                                 │
│ Step 3 (task_review):                           │
│   完整 Markdown 预览                            │
│   [↓ 下载 .md] [↓ 下载 .docx] [→ 生成代码] 按钮 │
│                                                 │
│ Step 4 (code_generating/complete):              │
│   Phase 3 占位 — 显示"即将推出"                 │
└────────────────────────────────────────────────┘
```

---

## 边缘情况

### Sidebar.tsx
- `FolderGit2` 需要从 `lucide-react` 导入，确认版本支持（lucide-react ≥ 0.263.1）
- 如果 lucide-react 版本不包含 `FolderGit2`，改用 `FolderPlus` 或 `FolderOpen` 作为备选

### GlobalContext.tsx — 闭包陷阱
- `ws.onopen` 回调中读取 `projectState` 会拿到创建时的旧值（React state 闭包）
- 解决：创建 `projectStateRef = useRef(projectState)`，通过 `useEffect` 同步更新，`ws.onopen` 读 `projectStateRef.current`
- 不要用 `projectState` 直接在 `useCallback` 依赖之外读取

### GlobalContext.tsx — WebSocket 清理
- 组件卸载时 `ws` 若未关闭会导致内存泄漏或 setState-after-unmount 警告
- 在 `useEffect` 返回的 cleanup 函数中关闭 `projectWs.current`

### project/page.tsx — 文件上传
- 用户上传 0 字节文件 → 调用 `uploadReference` 前校验 `file.size > 0`，否则显示错误提示
- 上传进行中禁用"开始生成"按钮（用 `isUploading` 本地状态控制）
- 上传失败（网络错误）→ 捕获 fetch 异常，设置错误提示，不改变 step
- 上传不支持格式（非 .docx/.pdf）→ 前端在 `<input accept>` 限制 + 二次校验 MIME type

### project/page.tsx — Markdown 流式渲染
- 流式追加时内容可能在标签中间截断（如 `**加` + `粗**`）→ ReactMarkdown 能正确处理不完整 Markdown，不需要额外处理
- 内容过长（>5000行）时 DOM 更新频繁 → 用 `useMemo` 包裹 ReactMarkdown，只在 `taskContent` 变化时重渲染

### project/page.tsx — 下载链接
- `taskMdPath` 是服务器端绝对路径，不能直接用于前端下载
- 下载应调用 `apiUrl("/api/v1/project/{sessionId}/download-task?format=md")` REST 接口，而非直接引用文件路径

---

## 测试用例

### E2E 测试（Playwright）：`web/tests/project.spec.ts`

```typescript
import { test, expect } from "@playwright/test";

test.describe("Project Creator", () => {
  test("sidebar shows Project Creator link", async ({ page }) => {
    await page.goto("/");
    await expect(page.getByText("Project Creator")).toBeVisible();
  });

  test("navigates to /project", async ({ page }) => {
    await page.goto("/project");
    await expect(page).toHaveURL("/project");
    await expect(page.getByText(/Project Creator/i)).toBeVisible();
  });

  test("step 1 renders config form", async ({ page }) => {
    await page.goto("/project");
    await expect(page.getByText(/上传参考任务书|Upload Reference/i)).toBeVisible();
    await expect(page.locator("input[placeholder*='主题']").or(
      page.locator("input[placeholder*='theme']")
    )).toBeVisible();
  });

  test("start button disabled when theme empty", async ({ page }) => {
    await page.goto("/project");
    const btn = page.getByRole("button", { name: /生成任务书|Generate/i });
    await expect(btn).toBeDisabled();
  });

  test("rejects non-docx upload", async ({ page }) => {
    await page.goto("/project");
    const fileInput = page.locator("input[type=file]");
    await fileInput.setInputFiles({
      name: "test.txt",
      mimeType: "text/plain",
      buffer: Buffer.from("hello"),
    });
    // 应显示格式错误提示
    await expect(page.getByText(/不支持|unsupported|docx|pdf/i)).toBeVisible();
  });
});
```

### 单元测试（TypeScript / Vitest，如后续添加）

目前项目只有 Playwright，暂无 Vitest。以下为手动验证点：

| 场景 | 验证方式 |
|------|---------|
| `FolderGit2` 图标正确显示 | 浏览器访问 `/project`，侧边栏图标可见 |
| WebSocket 连接建立 | 浏览器 Network tab 可见 `ws://localhost:8001/api/v1/project/generate-task` |
| chunk 消息追加内容 | 右侧 Markdown 区域实时出现文字 |
| section 消息更新进度 | 左侧章节列表对应项变绿 |
| complete 消息切换到 Step 3 | 页面自动跳转到审阅视图 |
| 下载 .md 有效 | 点击下载按钮，文件内容正确 |
| resetProject 后回到 Step 1 | 点击重置，所有字段清空 |

---

## 主动测试策略

```bash
# Step 1 完成后 — 检查 TypeScript 编译
cd web && npx tsc --noEmit 2>&1 | head -20

# Step 2&3 完成后 — 检查 Context 编译无误
cd web && npx tsc --noEmit 2>&1 | head -20

# Step 4 完成后 — 启动前端，运行 Playwright 冒烟测试
cd web && npm run dev &
sleep 5
npx playwright test tests/project.spec.ts --reporter=line

# 完整 E2E（需要后端运行）
conda run -n deeptutor python -m uvicorn src.api.main:app --port 8001 &
sleep 3
cd web && npx playwright test tests/project.spec.ts
```

---

## Bug 处理协议

1. **复现**：写最小化的 Playwright test 或 `console.log` 序列，使其失败。
2. **修复**：只改最小范围代码，不动无关逻辑。
3. **验证**：运行测试，确认由红转绿。
4. **反思**：在本文件 [反思记录] 节追加一条。

---

## 反思记录

*每次纠错后在此追加，格式：`日期 · 问题简述 · 根因 · 预防`*

2026-03-22 · startTaskGeneration 中无法使用 wsUrl() — 因为 wsUrl 调用 apiUrl 时在 useCallback 内部，而 API_BASE_URL 环境变量在模块初始化时读取，测试时会抛异常 · 改为直接对 NEXT_PUBLIC_API_BASE 做 http→ws 替换，与 wsUrl() 等效 · 今后 context 内的 WS 函数直接操作 process.env.NEXT_PUBLIC_API_BASE，不通过 wsUrl() wrapper

2026-03-22 · `npx tsc --noEmit` 使用了系统 Node 18，Next.js build 报版本不足 · 运行 build/tsc 前需先 `nvm use 20` · 今后在 web 目录操作时统一加 `export NVM_DIR=... && source nvm.sh && nvm use 20`
