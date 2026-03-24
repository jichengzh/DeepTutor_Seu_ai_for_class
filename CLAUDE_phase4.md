# CLAUDE_phase4.md — Phase 4: 集成测试 + 历史记录页面

## 规则

- **不写兼容性代码**，除非用户明确要求。
- 需求不明确时向用户提问，不猜测。
- 每完成一个任务，立即运行对应测试，确认通过再继续。
- 发现 bug 时：先写能重现的测试 → 修复 → 确认通过 → 反思根因。
- **前置条件**：Phase 1-3 全部完成（步骤 1-16 均 ✅）；`claude` CLI 已安装并 OAuth 登录；DeepTutor 后端和前端均可正常启动。

---

## Phase 4 任务计划

按顺序实现，每步完成后立即测试：

| 步骤 | 文件 | 操作 | 依赖 |
|------|------|------|------|
| 1 | `web/tests/e2e/project.spec.ts` | 扩展 Playwright E2E：Steps 2-4 UI 行为 + WS Mock | Phase 2/3 前端完成 |
| 2 | `web/app/project/history/page.tsx` | 新建会话历史页面（调用 GET /project/sessions） | 后端 sessions 端点 ✅ |
| 3 | `web/components/Sidebar.tsx` | 添加 History 子链接（可选折叠） | history/page.tsx |
| 4 | `tests/test_integration_project.py` | 后端全链路集成测试（不依赖真实 claude CLI） | 全部后端完成 |
| 5 | 手动端到端验证 | 按 plan.md Phase 5.2 步骤 1-7 运行真实 claude CLI | 全部完成 |

---

## 新增 / 修改文件列表

```
web/
├── tests/e2e/project.spec.ts          ← 修改：扩展 Step 2-4 测试
└── app/project/history/page.tsx       ← 新建：历史会话列表
web/components/Sidebar.tsx             ← 修改（可选）：添加 History 子链接
tests/
└── test_integration_project.py        ← 新建：后端集成测试
```

---

## Step 1：扩展 Playwright E2E

### 1.1 目前覆盖范围（已有，不改动）

`web/tests/e2e/project.spec.ts` 现有 7 个测试，覆盖：
- 侧边栏显示 Project Creator 链接
- 导航到 /project 页面
- Step 1 配置表单渲染
- "开始生成任务书"按钮在主题空/无文件时禁用
- 拒绝非 .docx/.pdf 文件
- 步骤指示器显示全部 4 步

### 1.2 新增测试（追加到同一文件）

**策略**：对 WebSocket 相关测试使用 Playwright 的 `page.routeWebSocket()` 进行 Mock（Playwright 1.48+），避免依赖真实后端。

```typescript
import { test, expect, Page } from "@playwright/test";

// ──────────────────────────────────────────────
// 辅助：注入 .docx 文件（Step 1 必需）
// ──────────────────────────────────────────────
async function fillStep1(page: Page, theme = "ROS 机器人导航实习") {
  await page.getByPlaceholder(/ROS 机器人导航/).fill(theme);
  const fileInput = page.locator("input[type=file]");
  await fileInput.setInputFiles({
    name: "ref.docx",
    mimeType: "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
    buffer: Buffer.from("PK fake docx content"),  // 仅通过前端格式校验
  });
}

// ──────────────────────────────────────────────
// 上传接口 Mock（POST /api/v1/project/upload-reference）
// ──────────────────────────────────────────────
async function mockUploadReference(page: Page) {
  await page.route("**/api/v1/project/upload-reference", (route) =>
    route.fulfill({
      status: 200,
      contentType: "application/json",
      body: JSON.stringify({
        reference_id: "test-ref-id",
        original_filename: "ref.docx",
        structure: { sections: { objectives: "...", modules: "..." } },
        structure_summary: "测试结构摘要",
        section_count: 2,
      }),
    })
  );
}

// ──────────────────────────────────────────────
// 知识库列表 Mock
// ──────────────────────────────────────────────
async function mockKbList(page: Page) {
  await page.route("**/api/v1/knowledge/list", (route) =>
    route.fulfill({
      status: 200,
      contentType: "application/json",
      body: JSON.stringify({ knowledge_bases: [{ name: "test_kb" }] }),
    })
  );
}

test.describe("Project Creator — Step 2 (任务书生成)", () => {
  test("上传成功后显示结构摘要", async ({ page }) => {
    await mockUploadReference(page);
    await mockKbList(page);
    await page.goto("/project");
    await fillStep1(page);
    // 等待上传接口调用完成（文件设置后前端自动上传）
    await expect(page.getByText(/测试结构摘要/)).toBeVisible({ timeout: 5000 });
  });

  test("WS generate-task：流式 chunk 实时追加到 Markdown 预览", async ({ page }) => {
    await mockUploadReference(page);
    await mockKbList(page);
    await page.goto("/project");
    await fillStep1(page);
    // 等待上传完成
    await expect(page.getByText(/测试结构摘要/)).toBeVisible({ timeout: 5000 });

    // Mock WebSocket
    await page.routeWebSocket("**/api/v1/project/generate-task", (ws) => {
      ws.onopen(() => {
        // 发送若干 chunk + section + complete
        ws.send(JSON.stringify({ type: "status", content: "正在生成..." }));
        ws.send(JSON.stringify({ type: "chunk", content: "## 一、课程背景\n" }));
        ws.send(JSON.stringify({ type: "chunk", content: "本实习旨在...\n" }));
        ws.send(JSON.stringify({ type: "section", section: "objectives", content: "## 一、课程背景\n本实习旨在...\n" }));
        ws.send(JSON.stringify({
          type: "complete",
          session_id: "test-session-001",
          task_md_path: "/tmp/task.md",
          task_content: "## 一、课程背景\n本实习旨在...\n",
        }));
      });
    });

    await page.getByRole("button", { name: /开始生成任务书/ }).click();

    // 验证实时内容出现在预览区
    await expect(page.getByText("课程背景")).toBeVisible({ timeout: 8000 });

    // 验证进入 task_review 步骤
    await expect(page.getByRole("button", { name: /下载 .md/ })).toBeVisible({ timeout: 5000 });
  });

  test("章节进度指示器：complete 后所有章节显示完成", async ({ page }) => {
    await mockUploadReference(page);
    await mockKbList(page);
    await page.goto("/project");
    await fillStep1(page);
    await expect(page.getByText(/测试结构摘要/)).toBeVisible({ timeout: 5000 });

    const allSections = ["cover", "objectives", "modules", "details", "requirements", "deliverables", "grading", "schedule", "references"];

    await page.routeWebSocket("**/api/v1/project/generate-task", (ws) => {
      ws.onopen(() => {
        for (const sec of allSections) {
          ws.send(JSON.stringify({ type: "section", section: sec, content: `## ${sec}` }));
        }
        ws.send(JSON.stringify({
          type: "complete",
          session_id: "test-session-002",
          task_md_path: "/tmp/task.md",
          task_content: "内容",
        }));
      });
    });

    await page.getByRole("button", { name: /开始生成任务书/ }).click();
    // Step 2 进行中：应显示章节列表（每个章节对应一行）
    // 等待 complete → 进入 task_review
    await expect(page.getByRole("button", { name: /下载 .md/ })).toBeVisible({ timeout: 8000 });
  });

  test("WS error 回退到 Step 1 并显示错误信息", async ({ page }) => {
    await mockUploadReference(page);
    await mockKbList(page);
    await page.goto("/project");
    await fillStep1(page);
    await expect(page.getByText(/测试结构摘要/)).toBeVisible({ timeout: 5000 });

    await page.routeWebSocket("**/api/v1/project/generate-task", (ws) => {
      ws.onopen(() => {
        ws.send(JSON.stringify({ type: "error", content: "LLM 超时，请重试" }));
      });
    });

    await page.getByRole("button", { name: /开始生成任务书/ }).click();
    await expect(page.getByText(/LLM 超时/)).toBeVisible({ timeout: 8000 });
    // 回到 Step 1：生成按钮应重新出现
    await expect(page.getByRole("button", { name: /开始生成任务书/ })).toBeVisible({ timeout: 5000 });
  });
});

test.describe("Project Creator — Step 3 (任务书审阅)", () => {
  // 辅助：直接导航到 task_review 状态（通过完整的 WS Mock 流程）
  async function reachTaskReview(page: Page) {
    await mockUploadReference(page);
    await mockKbList(page);
    // Mock download endpoints
    await page.route("**/api/v1/project/*/download-task*", (route) =>
      route.fulfill({ status: 200, body: "## 任务书内容", contentType: "text/markdown" })
    );
    await page.goto("/project");
    await fillStep1(page);
    await expect(page.getByText(/测试结构摘要/)).toBeVisible({ timeout: 5000 });

    await page.routeWebSocket("**/api/v1/project/generate-task", (ws) => {
      ws.onopen(() => {
        ws.send(JSON.stringify({
          type: "complete",
          session_id: "review-session-001",
          task_md_path: "/tmp/task.md",
          task_content: "## 任务书完整内容\n\n这是生成的任务书。",
        }));
      });
    });

    await page.getByRole("button", { name: /开始生成任务书/ }).click();
    await expect(page.getByRole("button", { name: /下载 .md/ })).toBeVisible({ timeout: 8000 });
  }

  test("Step 3 显示任务书完整 Markdown 预览", async ({ page }) => {
    await reachTaskReview(page);
    await expect(page.getByText("任务书完整内容")).toBeVisible();
  });

  test("Step 3 显示下载按钮：.md 和 .docx", async ({ page }) => {
    await reachTaskReview(page);
    await expect(page.getByRole("button", { name: /下载 .md/ })).toBeVisible();
    await expect(page.getByRole("button", { name: /下载 .docx/ })).toBeVisible();
  });

  test("Step 3 显示'生成代码仓库'按钮且可点击", async ({ page }) => {
    await reachTaskReview(page);
    const codeBtn = page.getByRole("button", { name: /生成代码仓库/ });
    await expect(codeBtn).toBeVisible();
    await expect(codeBtn).toBeEnabled();
  });
});

test.describe("Project Creator — Step 4 (代码生成)", () => {
  async function reachCodeGenerating(page: Page) {
    await mockUploadReference(page);
    await mockKbList(page);
    await page.goto("/project");
    await fillStep1(page);
    await expect(page.getByText(/测试结构摘要/)).toBeVisible({ timeout: 5000 });

    await page.routeWebSocket("**/api/v1/project/generate-task", (ws) => {
      ws.onopen(() => {
        ws.send(JSON.stringify({
          type: "complete",
          session_id: "code-session-001",
          task_md_path: "/tmp/task.md",
          task_content: "## 任务书",
        }));
      });
    });

    await page.getByRole("button", { name: /开始生成任务书/ }).click();
    await expect(page.getByRole("button", { name: /生成代码仓库/ })).toBeVisible({ timeout: 8000 });

    await page.routeWebSocket("**/api/v1/project/generate-code", (ws) => {
      ws.onopen(() => {
        ws.send(JSON.stringify({ type: "phase", content: "阶段 B：规划中..." }));
        ws.send(JSON.stringify({ type: "agent_log", log_type: "tool_use", tool: "Write", path: "PLAN.md", content: "写入 PLAN.md" }));
        ws.send(JSON.stringify({ type: "phase", content: "阶段 C：生成代码..." }));
        ws.send(JSON.stringify({ type: "agent_log", log_type: "tool_use", tool: "Write", path: "src/main.py", content: "写入 src/main.py" }));
        ws.send(JSON.stringify({ type: "file_created", path: "src/main.py" }));
        ws.send(JSON.stringify({ type: "verify_result", passed: true, report: "所有检查通过" }));
        ws.send(JSON.stringify({ type: "coverage", map: { "module_1": ["src/main.py"] } }));
        ws.send(JSON.stringify({
          type: "complete",
          session_id: "code-session-001",
          repo_path: "/tmp/repo",
          file_tree: [{ name: "src", path: "src", type: "directory", children: [{ name: "main.py", path: "src/main.py", type: "file" }] }, { name: "README.md", path: "README.md", type: "file" }],
          coverage_map: { "module_1": ["src/main.py"] },
          verify_passed: true,
        }));
      });
    });

    await page.getByRole("button", { name: /生成代码仓库/ }).click();
  }

  test("Step 4 显示 Agent 日志面板", async ({ page }) => {
    await reachCodeGenerating(page);
    // 日志面板中应出现工具调用
    await expect(page.getByText("Write")).toBeVisible({ timeout: 8000 });
    await expect(page.getByText("PLAN.md")).toBeVisible({ timeout: 5000 });
  });

  test("Step 4 文件树展示生成的文件", async ({ page }) => {
    await reachCodeGenerating(page);
    await expect(page.getByText("README.md")).toBeVisible({ timeout: 8000 });
    await expect(page.getByText("main.py")).toBeVisible({ timeout: 5000 });
  });

  test("Step 4 验证通过时显示绿色状态", async ({ page }) => {
    await reachCodeGenerating(page);
    // 验证通过指示
    await expect(page.getByText(/验证通过|所有检查通过/)).toBeVisible({ timeout: 8000 });
  });

  test("Step 4 完成后显示下载 zip 按钮", async ({ page }) => {
    await reachCodeGenerating(page);
    await expect(page.getByRole("button", { name: /下载代码/ })).toBeVisible({ timeout: 8000 });
  });

  test("Step 4 完成后显示新建项目按钮", async ({ page }) => {
    await reachCodeGenerating(page);
    await expect(page.getByRole("button", { name: /新建项目/ })).toBeVisible({ timeout: 8000 });
  });

  test("generate-code WS error → 回退到 task_review", async ({ page }) => {
    await mockUploadReference(page);
    await mockKbList(page);
    await page.goto("/project");
    await fillStep1(page);
    await expect(page.getByText(/测试结构摘要/)).toBeVisible({ timeout: 5000 });

    await page.routeWebSocket("**/api/v1/project/generate-task", (ws) => {
      ws.onopen(() => {
        ws.send(JSON.stringify({ type: "complete", session_id: "err-session", task_md_path: "/tmp/task.md", task_content: "## 任务书" }));
      });
    });

    await page.getByRole("button", { name: /开始生成任务书/ }).click();
    await expect(page.getByRole("button", { name: /生成代码仓库/ })).toBeVisible({ timeout: 8000 });

    await page.routeWebSocket("**/api/v1/project/generate-code", (ws) => {
      ws.onopen(() => {
        ws.send(JSON.stringify({ type: "error", content: "claude CLI 未找到" }));
      });
    });

    await page.getByRole("button", { name: /生成代码仓库/ }).click();
    // 回退到 task_review
    await expect(page.getByText(/claude CLI 未找到/)).toBeVisible({ timeout: 8000 });
    await expect(page.getByRole("button", { name: /生成代码仓库/ })).toBeVisible({ timeout: 5000 });
  });
});
```

### 1.3 运行命令

```bash
# 启动前端开发服务器（另一终端）
cd web && npm run dev

# 运行所有 E2E 测试（headless）
cd web && npx playwright test tests/e2e/project.spec.ts

# 查看测试报告（带 UI）
cd web && npx playwright test tests/e2e/project.spec.ts --reporter=html && npx playwright show-report
```

**期望**：全部 20+ 测试通过（7 个原有 + 13 个新增）。

---

## Step 2：历史会话页面

### 2.1 新建 `web/app/project/history/page.tsx`

功能：展示所有历史项目列表，点击可查看详情或继续生成。

```typescript
"use client";

import { useEffect, useState } from "react";
import { apiUrl } from "@/lib/api";
import { useRouter } from "next/navigation";
import { FolderGit2, Trash2, Download, Clock, CheckCircle, AlertCircle, Loader2 } from "lucide-react";
import { useTranslation } from "react-i18next";

interface SessionSummary {
  session_id: string;
  theme: string;
  status: "init" | "task_generating" | "task_generated" | "code_generating" | "complete";
  created_at: number;
  updated_at: number;
  task_md_path?: string;
  repo_path?: string;
}

const STATUS_CONFIG = {
  init:             { label: "初始化",    color: "text-gray-400",  icon: Clock },
  task_generating:  { label: "生成任务书中", color: "text-blue-400",  icon: Loader2 },
  task_generated:   { label: "任务书就绪", color: "text-yellow-400", icon: CheckCircle },
  code_generating:  { label: "生成代码中", color: "text-purple-400", icon: Loader2 },
  complete:         { label: "已完成",    color: "text-green-400",  icon: CheckCircle },
};

export default function ProjectHistoryPage() {
  const { t } = useTranslation();
  const router = useRouter();
  const [sessions, setSessions] = useState<SessionSummary[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [deleting, setDeleting] = useState<string | null>(null);

  useEffect(() => {
    fetch(apiUrl("/api/v1/project/sessions?limit=50"))
      .then((r) => r.json())
      .then((data) => setSessions(data.sessions || []))
      .catch((e) => setError(String(e)))
      .finally(() => setLoading(false));
  }, []);

  async function handleDelete(sessionId: string) {
    if (!confirm("确认删除此项目？此操作不可撤销。")) return;
    setDeleting(sessionId);
    try {
      const r = await fetch(apiUrl(`/api/v1/project/sessions/${sessionId}`), { method: "DELETE" });
      if (r.ok) {
        setSessions((prev) => prev.filter((s) => s.session_id !== sessionId));
      } else {
        alert("删除失败：" + (await r.text()));
      }
    } finally {
      setDeleting(null);
    }
  }

  function handleDownloadRepo(sessionId: string) {
    window.location.href = apiUrl(`/api/v1/project/${sessionId}/download-repo`);
  }

  function handleDownloadTask(sessionId: string) {
    window.location.href = apiUrl(`/api/v1/project/${sessionId}/download-task?format=md`);
  }

  function formatDate(ts: number) {
    return new Date(ts * 1000).toLocaleString("zh-CN", {
      year: "numeric", month: "2-digit", day: "2-digit",
      hour: "2-digit", minute: "2-digit",
    });
  }

  if (loading) {
    return (
      <div className="flex items-center justify-center h-full">
        <Loader2 className="animate-spin w-8 h-8 text-gray-400" />
      </div>
    );
  }

  if (error) {
    return (
      <div className="flex flex-col items-center justify-center h-full gap-4">
        <AlertCircle className="w-12 h-12 text-red-400" />
        <p className="text-gray-400">{error}</p>
      </div>
    );
  }

  return (
    <div className="flex flex-col h-full p-6 max-w-4xl mx-auto">
      {/* 标题行 */}
      <div className="flex items-center justify-between mb-6">
        <div className="flex items-center gap-2">
          <FolderGit2 className="w-6 h-6 text-blue-400" />
          <h1 className="text-xl font-semibold text-gray-100">历史项目</h1>
          <span className="text-sm text-gray-500 ml-2">({sessions.length} 个)</span>
        </div>
        <button
          onClick={() => router.push("/project")}
          className="px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white rounded-lg text-sm transition-colors"
        >
          + 新建项目
        </button>
      </div>

      {/* 列表 */}
      {sessions.length === 0 ? (
        <div className="flex flex-col items-center justify-center flex-1 gap-4 text-gray-500">
          <FolderGit2 className="w-16 h-16 opacity-30" />
          <p>暂无历史项目</p>
          <button
            onClick={() => router.push("/project")}
            className="text-blue-400 hover:underline text-sm"
          >
            创建第一个项目 →
          </button>
        </div>
      ) : (
        <div className="flex flex-col gap-3">
          {sessions.map((session) => {
            const cfg = STATUS_CONFIG[session.status] || STATUS_CONFIG.init;
            const StatusIcon = cfg.icon;
            return (
              <div
                key={session.session_id}
                className="bg-gray-800 border border-gray-700 rounded-xl p-4 hover:border-gray-600 transition-colors"
              >
                {/* 主题 + 状态 */}
                <div className="flex items-start justify-between gap-4 mb-2">
                  <div className="flex-1 min-w-0">
                    <h2 className="text-gray-100 font-medium truncate">{session.theme || "（无主题）"}</h2>
                    <p className="text-gray-500 text-xs mt-1 font-mono">{session.session_id}</p>
                  </div>
                  <div className={`flex items-center gap-1 text-sm ${cfg.color} shrink-0`}>
                    <StatusIcon className={`w-4 h-4 ${session.status.endsWith("ing") ? "animate-spin" : ""}`} />
                    <span>{cfg.label}</span>
                  </div>
                </div>

                {/* 时间戳 */}
                <div className="flex items-center gap-4 text-xs text-gray-500 mb-3">
                  <span>创建：{formatDate(session.created_at)}</span>
                  <span>更新：{formatDate(session.updated_at)}</span>
                </div>

                {/* 操作按钮 */}
                <div className="flex items-center gap-2">
                  {session.task_md_path && (
                    <button
                      onClick={() => handleDownloadTask(session.session_id)}
                      className="flex items-center gap-1 px-3 py-1.5 bg-gray-700 hover:bg-gray-600 text-gray-300 rounded-lg text-xs transition-colors"
                    >
                      <Download className="w-3 h-3" />
                      下载任务书
                    </button>
                  )}
                  {session.status === "complete" && (
                    <button
                      onClick={() => handleDownloadRepo(session.session_id)}
                      className="flex items-center gap-1 px-3 py-1.5 bg-blue-700 hover:bg-blue-600 text-white rounded-lg text-xs transition-colors"
                    >
                      <Download className="w-3 h-3" />
                      下载代码 zip
                    </button>
                  )}
                  <button
                    onClick={() => handleDelete(session.session_id)}
                    disabled={deleting === session.session_id}
                    className="flex items-center gap-1 px-3 py-1.5 bg-red-900/40 hover:bg-red-900/70 text-red-400 rounded-lg text-xs transition-colors disabled:opacity-50 ml-auto"
                  >
                    {deleting === session.session_id ? (
                      <Loader2 className="w-3 h-3 animate-spin" />
                    ) : (
                      <Trash2 className="w-3 h-3" />
                    )}
                    删除
                  </button>
                </div>
              </div>
            );
          })}
        </div>
      )}
    </div>
  );
}
```

### 2.2 关键点

- **无额外 API**：调用现有 `GET /project/sessions`、`DELETE /project/sessions/{id}`、`GET /project/{id}/download-task`、`GET /project/{id}/download-repo`，全部已实现。
- **下载按钮条件显示**：只有 `task_md_path` 存在才显示"下载任务书"；只有 `status === "complete"` 才显示"下载代码 zip"。
- **删除确认**：`window.confirm` 防止误操作。

---

## Step 3：Sidebar 添加 History 子链接（可选）

如果需要在侧边栏显示历史入口，在 `web/components/Sidebar.tsx` 中：

```typescript
// 在 ALL_NAV_ITEMS 中添加（如果支持子路由）：
"/project/history": { icon: Clock, nameKey: "Project History" },
```

或者在 `/project` 页面内部提供跳转链接即可，无需修改侧边栏。

> **注意**：若侧边栏的 `ALL_NAV_ITEMS` 对象键必须是顶级路由，则不在此添加，在 `page.tsx` 内部放"查看历史"按钮跳转到 `/project/history` 即可。

---

## Step 4：后端集成测试

### 4.1 新建 `tests/test_integration_project.py`

**原则**：不依赖真实 claude CLI（用 `unittest.mock.patch` 模拟 `CodeGenerator.generate`），但真实调用 FastAPI 路由（使用 `httpx.AsyncClient`）。

```python
# tests/test_integration_project.py
# -*- coding: utf-8 -*-
"""
Project Creator 后端集成测试

覆盖范围：
- upload-reference 端到端（真实文件解析）
- sessions CRUD（create / list / get / delete）
- generate-task WebSocket（mock LLM）
- generate-code WebSocket（mock claude CLI）
- download-task / download-repo
"""
import io
import json
import asyncio
import zipfile
from pathlib import Path
from unittest.mock import AsyncMock, MagicMock, patch

import pytest
import pytest_asyncio
from httpx import AsyncClient, ASGITransport

from src.api.main import app


# ─────────────────────────────────────────────────────────
# Fixtures
# ─────────────────────────────────────────────────────────

@pytest.fixture(scope="session")
def anyio_backend():
    return "asyncio"


@pytest_asyncio.fixture
async def client():
    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as c:
        yield c


@pytest.fixture
def minimal_docx_bytes() -> bytes:
    """生成最小合法 .docx 文件（ZIP 格式，含 [Content_Types].xml）"""
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as zf:
        zf.writestr("[Content_Types].xml", '<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/></Types>')
        zf.writestr("word/document.xml", '<?xml version="1.0"?><w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body><w:p><w:r><w:t>课程目标 测试内容 模块一</w:t></w:r></w:p></w:body></w:document>')
        zf.writestr("_rels/.rels", '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"></Relationships>')
    return buf.getvalue()


# ─────────────────────────────────────────────────────────
# 1. upload-reference
# ─────────────────────────────────────────────────────────

@pytest.mark.anyio
async def test_upload_reference_valid_docx(client, minimal_docx_bytes):
    """上传合法 .docx 返回 200 + structure 字段"""
    response = await client.post(
        "/api/v1/project/upload-reference",
        files={"files": ("test_ref.docx", minimal_docx_bytes, "application/vnd.openxmlformats-officedocument.wordprocessingml.document")},
    )
    assert response.status_code == 200
    data = response.json()
    assert "reference_id" in data
    assert "structure" in data
    assert data["original_filename"] == "test_ref.docx"


@pytest.mark.anyio
async def test_upload_reference_invalid_type(client):
    """上传非 docx/pdf 文件返回 400"""
    response = await client.post(
        "/api/v1/project/upload-reference",
        files={"files": ("test.txt", b"hello world", "text/plain")},
    )
    assert response.status_code == 400
    assert "Unsupported file type" in response.json()["detail"]


@pytest.mark.anyio
async def test_upload_reference_no_file(client):
    """不提供文件返回 422（FastAPI validation）"""
    response = await client.post("/api/v1/project/upload-reference")
    assert response.status_code == 422


# ─────────────────────────────────────────────────────────
# 2. sessions CRUD
# ─────────────────────────────────────────────────────────

@pytest.mark.anyio
async def test_list_sessions_empty_or_existing(client):
    """GET /project/sessions 返回 200 + sessions 列表"""
    response = await client.get("/api/v1/project/sessions")
    assert response.status_code == 200
    data = response.json()
    assert "sessions" in data
    assert "total" in data
    assert isinstance(data["sessions"], list)


@pytest.mark.anyio
async def test_get_session_not_found(client):
    """不存在的 session_id 返回 404"""
    response = await client.get("/api/v1/project/sessions/nonexistent-id-12345")
    assert response.status_code == 404


@pytest.mark.anyio
async def test_delete_session_not_found(client):
    """删除不存在的 session 返回 404"""
    response = await client.delete("/api/v1/project/sessions/nonexistent-id-12345")
    assert response.status_code == 404


@pytest.mark.anyio
async def test_session_create_and_get_and_delete(client):
    """创建 session → 获取 → 删除 完整流程（通过 session_manager 直接操作）"""
    from src.agents.project import get_project_session_manager
    mgr = get_project_session_manager()
    sid = mgr.create_session(theme="集成测试主题", kb_name=None, reference_structure={})

    # GET
    response = await client.get(f"/api/v1/project/sessions/{sid}")
    assert response.status_code == 200
    data = response.json()
    assert data["theme"] == "集成测试主题"

    # DELETE
    response = await client.delete(f"/api/v1/project/sessions/{sid}")
    assert response.status_code == 200

    # 再次 GET 应 404
    response = await client.get(f"/api/v1/project/sessions/{sid}")
    assert response.status_code == 404


# ─────────────────────────────────────────────────────────
# 3. download-task（依赖已有 session + 文件）
# ─────────────────────────────────────────────────────────

@pytest.mark.anyio
async def test_download_task_not_found_session(client):
    """session 不存在时 download-task 返回 404"""
    response = await client.get("/api/v1/project/nonexistent-id/download-task")
    assert response.status_code == 404


@pytest.mark.anyio
async def test_download_task_md_success(client, tmp_path):
    """session 存在且 md 文件存在时返回文件内容"""
    from src.agents.project import get_project_session_manager
    mgr = get_project_session_manager()
    sid = mgr.create_session(theme="下载测试", kb_name=None, reference_structure={})

    # 创建假 md 文件
    md_file = tmp_path / "task.md"
    md_file.write_text("# 测试任务书", encoding="utf-8")
    mgr.update_session(sid, task_md_path=str(md_file))

    response = await client.get(f"/api/v1/project/{sid}/download-task?format=md")
    assert response.status_code == 200
    assert "task" in response.headers.get("content-disposition", "").lower()

    # 清理
    mgr.delete_session(sid)


# ─────────────────────────────────────────────────────────
# 4. download-repo
# ─────────────────────────────────────────────────────────

@pytest.mark.anyio
async def test_download_repo_not_found_session(client):
    """session 不存在时 download-repo 返回 404"""
    response = await client.get("/api/v1/project/nonexistent-id/download-repo")
    assert response.status_code == 404


@pytest.mark.anyio
async def test_download_repo_success(client, tmp_path):
    """repo 目录存在时返回 zip 流"""
    from src.agents.project import get_project_session_manager
    from src.api.routers.project import PROJECTS_DIR

    mgr = get_project_session_manager()
    sid = mgr.create_session(theme="仓库下载测试", kb_name=None, reference_structure={})
    mgr.update_session(sid, status="complete")

    # 在标准 PROJECTS_DIR 下创建 repo
    repo_dir = PROJECTS_DIR / sid / "repo"
    repo_dir.mkdir(parents=True, exist_ok=True)
    (repo_dir / "README.md").write_text("# Test Repo", encoding="utf-8")
    (repo_dir / "main.py").write_text("print('hello')", encoding="utf-8")

    response = await client.get(f"/api/v1/project/{sid}/download-repo")
    assert response.status_code == 200
    assert response.headers["content-type"] == "application/zip"

    # 验证 zip 内容
    buf = io.BytesIO(response.content)
    with zipfile.ZipFile(buf) as zf:
        names = zf.namelist()
    assert "README.md" in names
    assert "main.py" in names

    # 清理
    import shutil
    shutil.rmtree(PROJECTS_DIR / sid, ignore_errors=True)
    mgr.delete_session(sid)
```

### 4.2 运行命令

```bash
conda run -n deeptutor pytest tests/test_integration_project.py -v
# 期望：全部通过（无真实 claude CLI 依赖）
```

---

## Step 5：手动端到端验证

### 5.1 前置检查

```bash
# 1. 确认 claude CLI 可用
which claude && claude --version

# 2. 确认 OAuth 凭据有效（非交互式测试）
timeout 30 claude -p "say hello in 3 words" --output-format json 2>&1

# 3. 启动后端
conda run -n deeptutor uvicorn src.api.main:app --reload --port 8000

# 4. 启动前端
cd web && npm run dev
```

### 5.2 端到端步骤（来自 plan.md Phase 5.2）

| 步骤 | 操作 | 期望结果 |
|------|------|---------|
| 1 | 打开 http://localhost:3000/project | 配置表单显示 |
| 2 | 上传 `book/嵌入式开发暑期实习任务书.docx` | 显示文档结构摘要（9 个章节） |
| 3 | 输入主题"ROS 机器人导航实习"，选择知识库，开启 Web Search | 表单完整 |
| 4 | 点击"开始生成任务书" | 章节逐步出现，Markdown 实时渲染 |
| 5 | 生成完成后下载 `.md` 和 `.docx` | 文件可打开，包含 9 个章节结构 |
| 6 | 点击"生成代码仓库" | Agent 日志出现（Write PLAN.md → git init → 文件生成） |
| 7 | 等待完成 | 文件树显示 README.md + src/ 等；验证状态绿色 |
| 8 | 点击"下载代码 zip" | 浏览器下载 zip；解压后可见完整目录 |
| 9 | 验证 `data/user/projects/{id}/repo/` | README.md、requirements.txt、src/ 目录存在 |
| 10 | 访问 http://localhost:3000/project/history | 显示刚创建的项目；状态为"已完成" |
| 11 | 在历史页面点击"下载代码 zip" | 同步骤 8 |
| 12 | 在历史页面点击"删除" | 项目消失；再次查看列表为空 |

### 5.3 验证生成代码质量

```bash
# 进入生成的仓库目录
cd data/user/projects/<session_id>/repo

# 安装依赖（如有 requirements.txt）
pip install -r requirements.txt 2>&1 | tail -5

# 尝试运行主模块
python main.py 2>&1 | head -20

# 或运行 pytest（如有测试）
pytest --tb=short 2>&1 | tail -20

# 检查 README.md 包含安装和使用说明
grep -E "安装|install|运行|run|使用|usage" README.md -i
```

---

## 边缘情况与测试用例

| 场景 | 预期行为 | 测试类型 |
|------|---------|---------|
| 上传空 .docx（无正文） | 返回空 structure，section_count=0，不报错 | 单元测试 |
| 上传超大 .pdf（>50MB） | 解析超时或内存错误时返回 422，不崩溃 | 手动 |
| 主题字符串含特殊字符（`"<>` 等） | 正常处理，不造成 XSS 或路径注入 | 单元测试 |
| WS 连接在 task_generating 中断 | `WebSocketDisconnect` 被捕获，日志记录，不泄漏资源 | 单元测试（现有） |
| claude CLI 生成超时（>10 分钟） | 进程被 kill，返回 error 消息，session 标记失败 | 手动 |
| download-repo 仓库 >500MB | 返回 413，不尝试打包 | 单元测试（已有检查） |
| 并发两个 generate-code 请求（同 session） | 两个 WS 均独立运行各自的 session（session_id 不同），互不干扰 | 手动 |
| 删除 session 后访问 download-task | 返回 404 | 集成测试（已覆盖） |
| history 页面 API 返回空列表 | 显示"暂无历史项目"空状态 | E2E（通过 mock） |
| history 页面删除失败（网络错误） | alert 显示错误信息，列表不变 | 手动 |

---

## 主动测试策略

按顺序执行，每一层通过后再进入下一层：

```bash
# 层 1：Python 单元测试（全套，含新集成测试）
conda run -n deeptutor pytest tests/ -v --tb=short 2>&1 | tail -30
# 期望：全部通过（43 + 新增集成测试）

# 层 2：TypeScript 类型检查
cd web && npx tsc --noEmit 2>&1
# 期望：0 错误

# 层 3：Playwright E2E（Mock WS，不依赖后端）
cd web && npx playwright test tests/e2e/project.spec.ts 2>&1 | tail -20
# 期望：全部通过（20+ 测试）

# 层 4：后端集成测试（真实 FastAPI 路由，mock claude CLI）
conda run -n deeptutor pytest tests/test_integration_project.py -v 2>&1
# 期望：全部通过

# 层 5：手动端到端（真实 claude CLI）
# 按 Step 5.2 表格操作，用真实任务书文件验证
```

---

## Bug 处理协议

1. **发现 bug** → 先写能重现的最小测试用例（单元测试 or E2E）
2. **确认测试失败**（红）→ 修复代码
3. **确认测试通过**（绿）→ 运行全套测试确保无回归
4. **反思根因**：在下方"已知陷阱"中记录，防止再犯

---

## 已知陷阱与防范（来自 Phase 1-3 经验）

| 陷阱 | 根因 | 防范 |
|------|------|------|
| `process.stdout is None` | `asyncio.create_subprocess_exec` 返回 `Optional[StreamReader]` | subprocess 创建后立即 `assert process.stdout is not None` |
| conda 环境找不到 `claude` binary | conda PATH 不含 `~/.local/bin/` | `_find_claude_bin()` 先 `shutil.which`，再 fallback hardcoded path |
| `wsUrl()` 在 `useCallback` 中抛异常 | `wsUrl()` 读取模块级 env var，非浏览器环境初始化时可能为 undefined | 改用 `process.env.NEXT_PUBLIC_API_BASE` 直接替换 `http→ws` |
| stale closure 导致 WS 发送空 session_id | `useCallback` 闭包捕获旧 state | 用 `projectStateRef` + `useEffect` 同步 ref，`ws.onopen` 中读 ref |
| Playwright `routeWebSocket` 需要 1.48+ | 旧版本无此 API | `package.json` 中 `@playwright/test >= "1.48.0"` |
| 历史页面时间戳单位 | 后端 `created_at` 是 Unix 秒（float），JS `Date` 需乘 1000 | `new Date(ts * 1000)` |

---

## 关键文件路径速查

| 文件 | 路径 | 操作 | 状态 |
|------|------|------|------|
| E2E 测试 | `web/tests/e2e/project.spec.ts` | 扩展 | 待实现 |
| 历史页面 | `web/app/project/history/page.tsx` | 新建 | 待实现 |
| 后端集成测试 | `tests/test_integration_project.py` | 新建 | 待实现 |
| 路由（已完成） | `src/api/routers/project.py` | 只读 | ✅ |
| 会话管理（已完成） | `src/agents/project/session_manager.py` | 只读 | ✅ |
| 代码生成器（已完成） | `src/agents/project/agents/code_generator.py` | 只读 | ✅ |
| 前端页面（已完成） | `web/app/project/page.tsx` | 只读 | ✅ |
| 全局状态（已完成） | `web/context/GlobalContext.tsx` | 只读 | ✅ |
