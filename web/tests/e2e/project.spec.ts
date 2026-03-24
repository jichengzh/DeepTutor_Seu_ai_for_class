import { test, expect, type Page } from "@playwright/test";

// ─── WebSocket Mock Infrastructure ───────────────────────────────────────────
//
// page.routeWebSocket() does NOT reliably intercept cross-origin connections
// (page at :3000 → backend at :8001). Instead, inject a JS-level WebSocket
// mock via addInitScript — this intercepts at the constructor level, bypassing
// the network entirely.
//
// Usage:
//   injectWsMock(page, "/project/generate-task", [msg1, msg2, ...])
//   → call BEFORE page.goto(); the mock responds with msgs after first send()
//
//   addWsMockAfterLoad(page, "/project/generate-code", [msg1, ...])
//   → call AFTER page.goto() to register additional URL patterns

async function injectWsMock(page: Page, urlFragment: string, messages: object[]) {
  await page.addInitScript(
    ({ frag, msgs }) => {
      const g = window as any;
      if (!g.__wsMockInstalled) {
        g.__wsMockInstalled = true;
        g.__wsMockMap = new Map();
        const Orig = window.WebSocket;

        function FakeWS(this: any, url: string, proto?: any) {
          this.url = url;
          this.readyState = 0;
          this.bufferedAmount = 0;
          this.onopen = null;
          this.onclose = null;
          this.onerror = null;
          this.onmessage = null;
          this._L = {};
          this._sent = false;
          this._r = null;
          this._msgs = null;

          const map: Map<string, object[]> = g.__wsMockMap;
          let found = false;
          for (const [f, m] of map.entries()) {
            if (url.includes(f)) {
              this._msgs = [...(m as any[])];
              found = true;
              break;
            }
          }

          if (!found) {
            const self = this;
            this._r = new Orig(url, proto);
            this._r.onopen = (e: any) => {
              self.readyState = 1;
              if (self.onopen) self.onopen(e);
              self._fire("open", e);
            };
            this._r.onclose = (e: any) => {
              self.readyState = 3;
              if (self.onclose) self.onclose(e);
              self._fire("close", e);
            };
            this._r.onerror = (e: any) => {
              if (self.onerror) self.onerror(e);
              self._fire("error", e);
            };
            this._r.onmessage = (e: any) => {
              if (self.onmessage) self.onmessage(e);
              self._fire("message", e);
            };
          } else {
            const self = this;
            setTimeout(() => {
              self.readyState = 1;
              const e = new Event("open");
              if (self.onopen) self.onopen(e);
              self._fire("open", e);
            }, 10);
          }
        }

        FakeWS.CONNECTING = 0;
        FakeWS.OPEN = 1;
        FakeWS.CLOSING = 2;
        FakeWS.CLOSED = 3;
        FakeWS.prototype.CONNECTING = 0;
        FakeWS.prototype.OPEN = 1;
        FakeWS.prototype.CLOSING = 2;
        FakeWS.prototype.CLOSED = 3;

        FakeWS.prototype.send = function (data: any) {
          if (this._r) {
            this._r.send(data);
            return;
          }
          if (!this._sent) {
            this._sent = true;
            const self = this;
            const msgs: any[] = this._msgs;
            setTimeout(() => {
              for (const m of msgs) {
                const e = new MessageEvent("message", {
                  data: JSON.stringify(m),
                });
                if (self.onmessage) self.onmessage(e);
                self._fire("message", e);
              }
            }, 50);
          }
        };

        FakeWS.prototype.close = function (code?: number, reason?: string) {
          if (this._r) this._r.close(code, reason);
          this.readyState = 3;
          const e = new CloseEvent("close", { code: code ?? 1000, reason });
          if (this.onclose) this.onclose(e);
          this._fire("close", e);
        };

        FakeWS.prototype.addEventListener = function (t: string, l: any) {
          if (!this._L[t]) this._L[t] = [];
          this._L[t].push(l);
        };

        FakeWS.prototype.removeEventListener = function (t: string, l: any) {
          if (this._L[t])
            this._L[t] = this._L[t].filter((x: any) => x !== l);
        };

        FakeWS.prototype._fire = function (t: string, e: Event) {
          for (const l of this._L[t] || []) {
            if (typeof l === "function") l(e);
            else l.handleEvent(e);
          }
        };

        g.WebSocket = FakeWS;
      }

      g.__wsMockMap.set(frag, msgs);
    },
    { frag: urlFragment, msgs: messages },
  );
}

/** Register an additional WS mock AFTER the page is already loaded. */
async function addWsMockAfterLoad(
  page: Page,
  urlFragment: string,
  messages: object[],
) {
  await page.evaluate(
    ({ frag, msgs }) => {
      (window as any).__wsMockMap?.set(frag, msgs);
    },
    { frag: urlFragment, msgs: messages },
  );
}

// ─── Existing tests (Step 1) ─────────────────────────────────────────────────

test.describe("Project Creator", () => {
  test("sidebar shows Project Creator link", async ({ page }) => {
    await page.goto("/");
    await expect(page.getByText("Project Creator")).toBeVisible({
      timeout: 10000,
    });
  });

  test("navigates to /project page", async ({ page }) => {
    await page.goto("/project");
    await expect(page).toHaveURL("/project");
    await expect(
      page.getByRole("heading", { name: /Project Creator/i }),
    ).toBeVisible();
  });

  test("step 1 renders config form", async ({ page }) => {
    await page.goto("/project");
    await expect(page.getByText(/上传参考任务书/)).toBeVisible();
    await expect(page.getByPlaceholder(/ROS 机器人导航/)).toBeVisible();
  });

  test("start button disabled when theme is empty", async ({ page }) => {
    await page.goto("/project");
    const btn = page.getByRole("button", { name: /开始生成任务书/ });
    await expect(btn).toBeDisabled();
  });

  test("start button disabled without uploaded file", async ({ page }) => {
    await page.goto("/project");
    await page.getByPlaceholder(/ROS 机器人导航/).fill("测试主题");
    // No file uploaded → button still disabled
    const btn = page.getByRole("button", { name: /开始生成任务书/ });
    await expect(btn).toBeDisabled();
  });

  test("rejects non-docx/pdf file type", async ({ page }) => {
    await page.goto("/project");
    const fileInput = page.locator("input[type=file]");
    await fileInput.setInputFiles({
      name: "test.txt",
      mimeType: "text/plain",
      buffer: Buffer.from("hello world"),
    });
    await expect(page.getByText(/仅支持 .docx 和 .pdf 格式/)).toBeVisible();
  });

  test("step indicator renders all 4 steps", async ({ page }) => {
    await page.goto("/project");
    await expect(page.getByText("配置")).toBeVisible();
    await expect(page.getByText("生成中")).toBeVisible();
    await expect(page.getByText("审阅")).toBeVisible();
    await expect(page.getByText("代码")).toBeVisible();
  });
});

// ─── Helpers ─────────────────────────────────────────────────────────────────

async function mockUploadReference(page: Page) {
  await page.route("**/api/v1/project/upload-reference", (route) =>
    route.fulfill({
      status: 200,
      contentType: "application/json",
      body: JSON.stringify({
        reference_id: "test-ref-id.docx",
        original_filename: "ref.docx",
        structure: { sections: { objectives: "目标内容", modules: "模块内容" } },
        structure_summary: "测试结构摘要",
        section_count: 2,
      }),
    }),
  );
}

async function mockKbList(page: Page) {
  await page.route("**/api/v1/knowledge/list", (route) =>
    route.fulfill({
      status: 200,
      contentType: "application/json",
      body: JSON.stringify({ knowledge_bases: [{ name: "test_kb" }] }),
    }),
  );
}

const FAKE_DOCX = Buffer.from("PK\x03\x04 fake docx");

/** Upload a fake .docx file and fill in the theme. Waits for upload success indicator. */
async function fillStep1(page: Page, theme = "ROS 机器人导航实习") {
  await page.getByPlaceholder(/ROS 机器人导航/).fill(theme);
  const fileInput = page.locator("input[type=file]");
  await fileInput.setInputFiles({
    name: "ref.docx",
    mimeType: "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
    buffer: FAKE_DOCX,
  });
  // Wait for the upload success indicator ("已解析 2 个章节")
  await expect(page.getByText(/已解析.*章节/)).toBeVisible({ timeout: 6000 });
}

/** Reach task_review step by running full Step 1 + WS generate-task mock. */
async function reachTaskReview(page: Page, sessionId = "review-session-001") {
  await mockUploadReference(page);
  await mockKbList(page);
  // Register WS mock BEFORE page.goto() so the constructor patch is in place
  await injectWsMock(page, "/project/generate-task", [
    // Send chunk first so taskContent is populated (complete handler doesn't update taskContent)
    { type: "chunk", content: "## 任务书完整内容\n\n这是生成的任务书正文。" },
    {
      type: "complete",
      session_id: sessionId,
      task_md_path: "/tmp/task.md",
      task_content: "## 任务书完整内容\n\n这是生成的任务书正文。",
    },
  ]);
  await page.goto("/project");
  await fillStep1(page);
  await page.getByRole("button", { name: /开始生成任务书/ }).click();
  await expect(
    page.getByRole("button", { name: /下载 .md/ }),
  ).toBeVisible({ timeout: 8000 });
}

/** Reach complete step by running full Steps 1-3 + WS generate-code mock. */
async function reachCodeComplete(page: Page) {
  await reachTaskReview(page, "code-session-001");

  // Page is already loaded — register code generation mock via evaluate
  await addWsMockAfterLoad(page, "/project/generate-code", [
    { type: "phase", content: "阶段 B：规划中..." },
    {
      type: "agent_log",
      log_type: "tool_use",
      tool: "Write",
      path: "PLAN.md",
      content: "Write PLAN.md",
    },
    { type: "phase", content: "阶段 C：生成代码..." },
    {
      type: "agent_log",
      log_type: "tool_use",
      tool: "Write",
      path: "src/main.py",
      content: "Write src/main.py",
    },
    { type: "file_created", path: "src/main.py" },
    { type: "verify_result", passed: true, report: "所有检查通过" },
    { type: "coverage", map: { module_1: ["src/main.py"] } },
    {
      type: "complete",
      session_id: "code-session-001",
      repo_path: "/tmp/repo",
      file_tree: [
        {
          name: "src",
          path: "src",
          type: "directory",
          children: [
            { name: "main.py", path: "src/main.py", type: "file" },
          ],
        },
        { name: "README.md", path: "README.md", type: "file" },
      ],
      coverage_map: { module_1: ["src/main.py"] },
      verify_passed: true,
    },
  ]);

  await page.getByRole("button", { name: /生成代码仓库/ }).click();
  // Wait for complete state (download button appears)
  await expect(
    page.getByRole("button", { name: /下载代码 zip/ }),
  ).toBeVisible({ timeout: 15000 });
}

// ─── Step 2: Task Generation ──────────────────────────────────────────────────

test.describe("Project Creator — Step 2 (任务书生成)", () => {
  test("上传成功后显示已解析章节数", async ({ page }) => {
    await mockUploadReference(page);
    await mockKbList(page);
    await page.goto("/project");
    await page.getByPlaceholder(/ROS 机器人导航/).fill("测试主题");
    const fileInput = page.locator("input[type=file]");
    await fileInput.setInputFiles({
      name: "ref.docx",
      mimeType: "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
      buffer: FAKE_DOCX,
    });
    await expect(page.getByText(/已解析.*章节/)).toBeVisible({
      timeout: 6000,
    });
    // Button enabled after upload
    await expect(
      page.getByRole("button", { name: /开始生成任务书/ }),
    ).toBeEnabled({ timeout: 3000 });
  });

  test("WS generate-task：流式 chunk 实时追加到 Markdown 预览", async ({
    page,
  }) => {
    await mockUploadReference(page);
    await mockKbList(page);
    await injectWsMock(page, "/project/generate-task", [
      { type: "status", content: "正在生成..." },
      { type: "chunk", content: "## 一、课程背景\n" },
      { type: "chunk", content: "本实习旨在培养能力。\n" },
      {
        type: "section",
        section: "objectives",
        content: "## 一、课程背景\n本实习旨在培养能力。\n",
      },
      {
        type: "complete",
        session_id: "stream-session-001",
        task_md_path: "/tmp/task.md",
        task_content: "## 一、课程背景\n本实习旨在培养能力。\n",
      },
    ]);

    await page.goto("/project");
    await fillStep1(page);
    await page.getByRole("button", { name: /开始生成任务书/ }).click();

    // Content streamed into preview
    await expect(page.getByText("课程背景")).toBeVisible({ timeout: 8000 });

    // Transitions to task_review
    await expect(
      page.getByRole("button", { name: /下载 .md/ }),
    ).toBeVisible({ timeout: 5000 });
  });

  test("WS error 回退到 Step 1 并显示错误信息", async ({ page }) => {
    await mockUploadReference(page);
    await mockKbList(page);
    await injectWsMock(page, "/project/generate-task", [
      { type: "error", content: "LLM 超时，请重试" },
    ]);

    await page.goto("/project");
    await fillStep1(page);
    await page.getByRole("button", { name: /开始生成任务书/ }).click();
    await expect(page.getByText(/LLM 超时/)).toBeVisible({ timeout: 8000 });
    // Back to config step — generate button re-appears
    await expect(
      page.getByRole("button", { name: /开始生成任务书/ }),
    ).toBeVisible({ timeout: 5000 });
  });
});

// ─── Step 3: Task Review ──────────────────────────────────────────────────────

test.describe("Project Creator — Step 3 (任务书审阅)", () => {
  test("Step 3 显示任务书 Markdown 预览内容", async ({ page }) => {
    await reachTaskReview(page);
    await expect(page.getByText("任务书完整内容")).toBeVisible();
  });

  test("Step 3 显示下载 .md 和 .docx 按钮", async ({ page }) => {
    await reachTaskReview(page);
    await expect(
      page.getByRole("button", { name: /下载 .md/ }),
    ).toBeVisible();
    await expect(
      page.getByRole("button", { name: /下载 .docx/ }),
    ).toBeVisible();
  });

  test("Step 3 显示并可点击'生成代码仓库'按钮", async ({ page }) => {
    await reachTaskReview(page);
    const btn = page.getByRole("button", { name: /生成代码仓库/ });
    await expect(btn).toBeVisible();
    await expect(btn).toBeEnabled();
  });
});

// ─── Step 4: Code Generation ──────────────────────────────────────────────────

test.describe("Project Creator — Step 4 (代码生成)", () => {
  test("Step 4 显示 Agent 操作日志面板", async ({ page }) => {
    await reachCodeComplete(page);
    // "Write PLAN.md" is the log entry content; avoid strict-mode collision with "Co-Writer" sidebar link
    await expect(page.getByText("Write PLAN.md")).toBeVisible({ timeout: 5000 });
    await expect(page.getByText("PLAN.md")).toBeVisible({ timeout: 5000 });
  });

  test("Step 4 文件树展示生成的文件", async ({ page }) => {
    await reachCodeComplete(page);
    await expect(page.getByText("README.md")).toBeVisible({ timeout: 5000 });
    // Use exact match to avoid collision with "src/main.py" in coverage map
    await expect(page.getByText("main.py", { exact: true })).toBeVisible({ timeout: 5000 });
  });

  test("Step 4 验证通过时显示绿色状态栏", async ({ page }) => {
    await reachCodeComplete(page);
    await expect(page.getByText(/代码验证通过/)).toBeVisible({
      timeout: 5000,
    });
  });

  test("Step 4 完成后显示下载 zip 按钮", async ({ page }) => {
    await reachCodeComplete(page);
    await expect(
      page.getByRole("button", { name: /下载代码 zip/ }),
    ).toBeVisible({ timeout: 5000 });
  });

  test("Step 4 完成后显示新建项目按钮", async ({ page }) => {
    await reachCodeComplete(page);
    await expect(
      page.getByRole("button", { name: /新建项目/ }),
    ).toBeVisible({ timeout: 5000 });
  });

  test("generate-code WS error → 回退到 task_review", async ({ page }) => {
    await mockUploadReference(page);
    await mockKbList(page);
    await injectWsMock(page, "/project/generate-task", [
      {
        type: "complete",
        session_id: "err-session-001",
        task_md_path: "/tmp/task.md",
        task_content: "## 任务书",
      },
    ]);

    await page.goto("/project");
    await fillStep1(page);
    await page.getByRole("button", { name: /开始生成任务书/ }).click();
    await expect(
      page.getByRole("button", { name: /生成代码仓库/ }),
    ).toBeVisible({ timeout: 8000 });

    // Register code-generation error mock after page is loaded
    await addWsMockAfterLoad(page, "/project/generate-code", [
      { type: "error", content: "claude CLI 未找到" },
    ]);

    await page.getByRole("button", { name: /生成代码仓库/ }).click();
    // Error shown in banner
    await expect(page.getByText(/claude CLI 未找到/)).toBeVisible({
      timeout: 8000,
    });
    // Reverted to task_review
    await expect(
      page.getByRole("button", { name: /生成代码仓库/ }),
    ).toBeVisible({ timeout: 5000 });
  });
});

// ─── History page ─────────────────────────────────────────────────────────────

test.describe("Project History", () => {
  test("navigates to /project/history", async ({ page }) => {
    await page.route("**/api/v1/project/sessions*", (route) =>
      route.fulfill({
        status: 200,
        contentType: "application/json",
        body: JSON.stringify({ sessions: [], total: 0 }),
      }),
    );
    await page.goto("/project/history");
    await expect(page).toHaveURL("/project/history");
  });

  test("empty history shows placeholder text", async ({ page }) => {
    await page.route("**/api/v1/project/sessions*", (route) =>
      route.fulfill({
        status: 200,
        contentType: "application/json",
        body: JSON.stringify({ sessions: [], total: 0 }),
      }),
    );
    await page.goto("/project/history");
    await expect(page.getByText(/暂无历史项目/)).toBeVisible({
      timeout: 5000,
    });
  });

  test("history page lists sessions from API", async ({ page }) => {
    await page.route("**/api/v1/project/sessions*", (route) =>
      route.fulfill({
        status: 200,
        contentType: "application/json",
        body: JSON.stringify({
          sessions: [
            {
              session_id: "proj_test_001",
              theme: "ROS 机器人导航实习",
              status: "complete",
              created_at: 1700000000,
              updated_at: 1700000100,
              task_md_path: "/tmp/task.md",
              repo_path: "/tmp/repo",
            },
          ],
          total: 1,
        }),
      }),
    );
    await page.goto("/project/history");
    await expect(page.getByText("ROS 机器人导航实习")).toBeVisible({
      timeout: 5000,
    });
    await expect(page.getByText("已完成")).toBeVisible({ timeout: 5000 });
  });
});
