"use client";

import { useState, useEffect, useCallback, useRef } from "react";
import {
  FolderGit2,
  Upload,
  Loader2,
  CheckCircle,
  Circle,
  RefreshCw,
  Download,
  FileText,
  ChevronRight,
  AlertCircle,
  X,
  Folder,
  File as FileIcon,
  Terminal,
  Wrench,
  MessageSquare,
} from "lucide-react";
import { useGlobal } from "@/context/GlobalContext";
import { useTranslation } from "react-i18next";
import { apiUrl } from "@/lib/api";
import ReactMarkdown from "react-markdown";
import remarkGfm from "remark-gfm";

// ─── Section names ───────────────────────────────────────────────────────────

const SECTION_NAMES: Record<string, string> = {
  cover: "封面信息",
  objectives: "背景与目标",
  modules: "模块概述",
  details: "设计内容",
  requirements: "作业要求",
  deliverables: "提交成果",
  grading: "成绩考核",
  schedule: "时间安排",
  references: "参考资源",
};

const SECTION_ORDER = Object.keys(SECTION_NAMES);

// ─── Step indicator ───────────────────────────────────────────────────────────

const STEPS = [
  { key: "config", label: "配置" },
  { key: "task_generating", label: "生成中" },
  { key: "task_review", label: "审阅" },
  { key: "code_generating", label: "代码" },
];

function StepIndicator({ current }: { current: string }) {
  const stepIndex = STEPS.findIndex((s) => s.key === current);
  const displayIndex = current === "complete" ? 3 : stepIndex;

  return (
    <div className="flex items-center gap-2 mb-6">
      {STEPS.map((step, i) => (
        <div key={step.key} className="flex items-center gap-2">
          <div
            className={`flex items-center justify-center w-7 h-7 rounded-full text-xs font-semibold border-2 transition-colors ${
              i < displayIndex
                ? "bg-green-500 border-green-500 text-white"
                : i === displayIndex
                  ? "bg-blue-500 border-blue-500 text-white"
                  : "border-gray-300 text-gray-400"
            }`}
          >
            {i < displayIndex ? <CheckCircle className="w-4 h-4" /> : i + 1}
          </div>
          <span
            className={`text-sm font-medium ${
              i === displayIndex ? "text-blue-600" : i < displayIndex ? "text-green-600" : "text-gray-400"
            }`}
          >
            {step.label}
          </span>
          {i < STEPS.length - 1 && (
            <ChevronRight className="w-4 h-4 text-gray-300 mx-1" />
          )}
        </div>
      ))}
    </div>
  );
}

// ─── Chapter progress (Step 2 left panel) ────────────────────────────────────

function ChapterProgress({
  sections,
  currentSection,
}: {
  sections: Record<string, string>;
  currentSection: string | null;
}) {
  return (
    <ul className="space-y-2">
      {SECTION_ORDER.map((key) => {
        const done = !!sections[key];
        const active = key === currentSection && !done;
        return (
          <li key={key} className="flex items-center gap-2">
            {done ? (
              <CheckCircle className="w-4 h-4 text-green-500 shrink-0" />
            ) : active ? (
              <Loader2 className="w-4 h-4 text-blue-500 animate-spin shrink-0" />
            ) : (
              <Circle className="w-4 h-4 text-gray-300 shrink-0" />
            )}
            <span
              className={`text-sm ${
                done ? "text-green-600" : active ? "text-blue-600 font-medium" : "text-gray-400"
              }`}
            >
              {SECTION_NAMES[key]}
            </span>
          </li>
        );
      })}
    </ul>
  );
}

// ─── Log drawer (slide-in from right) ────────────────────────────────────────

function LogDrawer({
  logs,
  open,
  onClose,
}: {
  logs: Array<{ type: string; content: string; timestamp?: number }>;
  open: boolean;
  onClose: () => void;
}) {
  const endRef = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (open) endRef.current?.scrollIntoView({ behavior: "smooth" });
  }, [logs, open]);

  return (
    <div
      className={`fixed right-0 top-0 h-full w-80 bg-white dark:bg-gray-900 shadow-2xl border-l z-50 flex flex-col transition-transform duration-300 ${
        open ? "translate-x-0" : "translate-x-full"
      }`}
    >
      <div className="flex items-center justify-between p-4 border-b">
        <span className="font-semibold text-sm">运行日志</span>
        <button onClick={onClose} className="p-1 hover:bg-gray-100 rounded">
          <X className="w-4 h-4" />
        </button>
      </div>
      <div className="flex-1 overflow-y-auto p-3 text-xs font-mono space-y-1">
        {logs.map((log, i) => (
          <div
            key={i}
            className={`py-0.5 ${
              log.type === "error" ? "text-red-500" : "text-gray-600 dark:text-gray-400"
            }`}
          >
            {log.content}
          </div>
        ))}
        <div ref={endRef} />
      </div>
    </div>
  );
}

// ─── File tree ────────────────────────────────────────────────────────────────

interface FileTreeNode {
  name: string;
  path: string;
  type: "file" | "directory";
  children?: FileTreeNode[];
}

function FileTree({ nodes, depth = 0 }: { nodes: FileTreeNode[]; depth?: number }) {
  if (!nodes || nodes.length === 0) return null;
  return (
    <ul className={depth > 0 ? "pl-4" : ""}>
      {nodes.map((node) => (
        <li key={node.path}>
          <div className="flex items-center gap-1 py-0.5 text-xs text-gray-700 dark:text-gray-300 hover:bg-gray-50 dark:hover:bg-gray-800 rounded px-1">
            {node.type === "directory"
              ? <Folder className="w-3.5 h-3.5 text-blue-400 shrink-0" />
              : <FileIcon className="w-3.5 h-3.5 text-gray-400 shrink-0" />}
            <span>{node.name}</span>
          </div>
          {node.children && <FileTree nodes={node.children} depth={depth + 1} />}
        </li>
      ))}
    </ul>
  );
}

// ─── Main page ────────────────────────────────────────────────────────────────

export default function ProjectPage() {
  const {
    projectState,
    setProjectState,
    uploadReference,
    startTaskGeneration,
    startCodeGeneration,
    resetProject,
  } = useGlobal();
  const { t } = useTranslation();

  const [kbs, setKbs] = useState<string[]>([]);
  const [isUploading, setIsUploading] = useState(false);
  const [uploadError, setUploadError] = useState<string | null>(null);
  const [showLogs, setShowLogs] = useState(false);
  const fileInputRef = useRef<HTMLInputElement>(null);

  // Fetch knowledge base list
  useEffect(() => {
    fetch(apiUrl("/api/v1/knowledge/list"))
      .then((r) => r.json())
      .then((data) => setKbs(data.knowledge_bases?.map((kb: any) => kb.name) || []))
      .catch(() => {});
  }, []);

  const {
    step, theme, selectedKb, webSearchEnabled, referenceStructure,
    taskContent, taskSections, currentSection, sessionId, logs, error,
    agentLogs, generatedFiles, verifyPassed, coverageMap,
  } = projectState;

  // ── File upload ──
  const handleFileChange = useCallback(
    async (e: React.ChangeEvent<HTMLInputElement>) => {
      const file = e.target.files?.[0];
      if (!file) return;

      const ext = file.name.split(".").pop()?.toLowerCase();
      if (!["docx", "pdf"].includes(ext || "")) {
        setUploadError("仅支持 .docx 和 .pdf 格式");
        return;
      }
      if (file.size === 0) {
        setUploadError("文件不能为空");
        return;
      }

      setUploadError(null);
      setIsUploading(true);
      try {
        await uploadReference(file);
      } catch (err: any) {
        setUploadError(err.message || "上传失败");
      } finally {
        setIsUploading(false);
        if (fileInputRef.current) fileInputRef.current.value = "";
      }
    },
    [uploadReference],
  );

  const canGenerate =
    theme.trim().length > 0 && referenceStructure !== null && !isUploading;

  // ── Download helper ──
  const downloadTask = (format: "md" | "docx") => {
    if (!sessionId) return;
    const url = apiUrl(`/api/v1/project/${sessionId}/download-task?format=${format}`);
    const a = document.createElement("a");
    a.href = url;
    a.download = format === "md" ? "generated_task.md" : "generated_task.docx";
    a.click();
  };

  // ─────────────────────────────────────────────────────────────────────────
  return (
    <div className="flex flex-col h-full p-6 max-w-5xl mx-auto">
      {/* Header */}
      <div className="flex items-center justify-between mb-4">
        <div className="flex items-center gap-3">
          <FolderGit2 className="w-6 h-6 text-blue-500" />
          <h1 className="text-xl font-semibold">Project Creator</h1>
        </div>
        <div className="flex gap-2">
          {logs.length > 0 && (
            <button
              onClick={() => setShowLogs(true)}
              className="text-xs px-3 py-1.5 rounded border hover:bg-gray-50 text-gray-600"
            >
              日志 ({logs.length})
            </button>
          )}
          {step !== "config" && (
            <button
              onClick={resetProject}
              className="flex items-center gap-1 text-xs px-3 py-1.5 rounded border hover:bg-gray-50 text-gray-600"
            >
              <RefreshCw className="w-3 h-3" /> 重置
            </button>
          )}
        </div>
      </div>

      {/* Step indicator */}
      <StepIndicator current={step} />

      {/* Error banner */}
      {error && (
        <div className="flex items-center gap-2 mb-4 p-3 bg-red-50 border border-red-200 rounded-lg text-sm text-red-700">
          <AlertCircle className="w-4 h-4 shrink-0" />
          {error}
          <button
            className="ml-auto"
            onClick={() => setProjectState((p) => ({ ...p, error: null }))}
          >
            <X className="w-4 h-4" />
          </button>
        </div>
      )}

      {/* ── Step 1: Config ── */}
      {step === "config" && (
        <div className="flex flex-col gap-6">
          <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
            {/* Upload card */}
            <div className="border-2 border-dashed rounded-xl p-6 flex flex-col items-center gap-3 hover:border-blue-400 transition-colors cursor-pointer"
              onClick={() => fileInputRef.current?.click()}
            >
              <Upload className="w-8 h-8 text-gray-400" />
              <div className="text-center">
                <p className="font-medium text-sm">上传参考任务书</p>
                <p className="text-xs text-gray-400 mt-1">支持 .docx / .pdf</p>
              </div>
              {isUploading && <Loader2 className="w-5 h-5 animate-spin text-blue-500" />}
              {referenceStructure && !isUploading && (
                <div className="flex items-center gap-1 text-green-600 text-xs">
                  <CheckCircle className="w-4 h-4" />
                  已解析 {Object.keys(referenceStructure.sections || {}).length} 个章节
                </div>
              )}
              {uploadError && <p className="text-xs text-red-500">{uploadError}</p>}
              <input
                ref={fileInputRef}
                type="file"
                accept=".docx,.pdf"
                className="hidden"
                onChange={handleFileChange}
              />
            </div>

            {/* Theme input */}
            <div className="flex flex-col gap-3">
              <label className="text-sm font-medium">新任务书主题</label>
              <textarea
                className="border rounded-lg p-3 text-sm resize-none h-24 focus:ring-2 focus:ring-blue-300 outline-none"
                placeholder="例如：ROS 机器人导航暑期实习"
                value={theme}
                onChange={(e) =>
                  setProjectState((p) => ({ ...p, theme: e.target.value }))
                }
              />
              {/* KB selector */}
              <div className="flex items-center gap-2">
                <label className="text-sm text-gray-600 shrink-0">知识库：</label>
                <select
                  className="flex-1 border rounded-lg px-3 py-1.5 text-sm focus:ring-2 focus:ring-blue-300 outline-none"
                  value={selectedKb}
                  onChange={(e) =>
                    setProjectState((p) => ({ ...p, selectedKb: e.target.value }))
                  }
                >
                  <option value="">不使用知识库</option>
                  {kbs.map((kb) => (
                    <option key={kb} value={kb}>{kb}</option>
                  ))}
                </select>
              </div>
              {/* Web search toggle */}
              <label className="flex items-center gap-2 cursor-pointer select-none">
                <div
                  className={`relative w-10 h-5 rounded-full transition-colors ${
                    webSearchEnabled ? "bg-blue-500" : "bg-gray-300"
                  }`}
                  onClick={() =>
                    setProjectState((p) => ({ ...p, webSearchEnabled: !p.webSearchEnabled }))
                  }
                >
                  <div
                    className={`absolute top-0.5 w-4 h-4 bg-white rounded-full shadow transition-transform ${
                      webSearchEnabled ? "translate-x-5" : "translate-x-0.5"
                    }`}
                  />
                </div>
                <span className="text-sm text-gray-600">开启网络搜索</span>
              </label>
            </div>
          </div>

          <button
            disabled={!canGenerate}
            onClick={startTaskGeneration}
            className="self-end flex items-center gap-2 px-6 py-2.5 bg-blue-500 text-white rounded-lg font-medium text-sm hover:bg-blue-600 disabled:opacity-40 disabled:cursor-not-allowed transition-colors"
          >
            开始生成任务书 <ChevronRight className="w-4 h-4" />
          </button>
        </div>
      )}

      {/* ── Step 2: Generating ── */}
      {step === "task_generating" && (
        <div className="flex gap-6 flex-1 min-h-0">
          {/* Left: chapter progress */}
          <div className="w-48 shrink-0">
            <p className="text-sm font-medium mb-3 text-gray-700">章节进度</p>
            <ChapterProgress sections={taskSections} currentSection={currentSection} />
          </div>

          {/* Right: streaming markdown */}
          <div className="flex-1 overflow-y-auto border rounded-xl p-4 prose prose-sm max-w-none dark:prose-invert">
            {taskContent ? (
              <ReactMarkdown remarkPlugins={[remarkGfm]}>{taskContent}</ReactMarkdown>
            ) : (
              <div className="flex items-center gap-2 text-gray-400 text-sm">
                <Loader2 className="w-4 h-4 animate-spin" /> 正在连接...
              </div>
            )}
          </div>
        </div>
      )}

      {/* ── Step 3: Review ── */}
      {step === "task_review" && (
        <div className="flex flex-col gap-4 flex-1 min-h-0">
          <div className="flex items-center justify-between">
            <p className="text-sm font-medium text-gray-700">任务书预览</p>
            <div className="flex gap-2">
              <button
                onClick={() => downloadTask("md")}
                className="flex items-center gap-1.5 text-xs px-3 py-1.5 rounded border hover:bg-gray-50"
              >
                <Download className="w-3.5 h-3.5" /> 下载 .md
              </button>
              <button
                onClick={() => downloadTask("docx")}
                className="flex items-center gap-1.5 text-xs px-3 py-1.5 rounded border hover:bg-gray-50"
              >
                <FileText className="w-3.5 h-3.5" /> 下载 .docx
              </button>
              <button
                onClick={startCodeGeneration}
                className="flex items-center gap-1.5 text-xs px-3 py-1.5 rounded bg-blue-500 text-white hover:bg-blue-600 transition-colors"
              >
                <FolderGit2 className="w-3.5 h-3.5" /> 生成代码仓库
              </button>
            </div>
          </div>

          <div className="flex-1 overflow-y-auto border rounded-xl p-5 prose prose-sm max-w-none dark:prose-invert">
            <ReactMarkdown remarkPlugins={[remarkGfm]}>{taskContent}</ReactMarkdown>
          </div>
        </div>
      )}

      {/* ── Step 4: Code generating / complete ── */}
      {(step === "code_generating" || step === "complete") && (
        <div className="flex gap-4 flex-1 min-h-0">

          {/* Left 1/3: Agent log */}
          <div className="w-72 shrink-0 flex flex-col gap-2">
            <p className="text-sm font-medium text-gray-700">Agent 操作日志</p>
            <div className="flex-1 overflow-y-auto border rounded-xl p-2 bg-gray-950 text-xs font-mono space-y-0.5">
              {agentLogs.length === 0 && step === "code_generating" && (
                <div className="flex items-center gap-2 text-gray-500 p-2">
                  <Loader2 className="w-3 h-3 animate-spin" /> 等待 Claude Agent...
                </div>
              )}
              {agentLogs.map((log, i) => {
                const isError = log.type === "error";
                const isResult = log.type === "tool_result";
                const Icon =
                  log.tool === "Bash" ? Terminal :
                  log.tool === "Write" || log.tool === "Edit" ? Wrench :
                  log.type === "message" ? MessageSquare :
                  Terminal;
                return (
                  <div key={i} className={`flex items-start gap-1.5 py-0.5 ${isError ? "text-red-400" : isResult ? "text-green-400" : "text-gray-300"}`}>
                    <span className="text-gray-600 shrink-0 mt-0.5">{log.timestamp}</span>
                    <Icon className="w-3 h-3 shrink-0 mt-0.5" />
                    <span className="break-all leading-relaxed">{log.content}</span>
                  </div>
                );
              })}
            </div>
          </div>

          {/* Right 2/3: File tree + coverage + actions */}
          <div className="flex-1 flex flex-col gap-3 min-h-0">

            {/* Verify status bar */}
            {step === "complete" && (
              <div className={`flex items-center gap-2 px-3 py-2 rounded-lg text-sm ${
                verifyPassed ? "bg-green-50 text-green-700 border border-green-200" : "bg-red-50 text-red-700 border border-red-200"
              }`}>
                {verifyPassed
                  ? <><CheckCircle className="w-4 h-4" /> 代码验证通过 — 可正常运行</>
                  : <><AlertCircle className="w-4 h-4" /> 验证未完全通过，请查看日志</>}
              </div>
            )}

            {/* File tree */}
            <div className="flex-1 overflow-y-auto border rounded-xl p-3">
              {step === "code_generating" && generatedFiles.length === 0 ? (
                <div className="flex items-center gap-2 text-gray-400 text-sm">
                  <Loader2 className="w-4 h-4 animate-spin" /> 正在生成文件...
                </div>
              ) : (
                <>
                  <p className="text-xs font-medium text-gray-500 mb-2">生成文件树</p>
                  <FileTree nodes={generatedFiles} />
                </>
              )}
            </div>

            {/* Coverage map */}
            {coverageMap && Object.keys(coverageMap).length > 0 && (
              <div className="border rounded-xl p-3">
                <p className="text-xs font-medium text-gray-500 mb-2">需求覆盖率</p>
                <div className="space-y-1">
                  {Object.entries(coverageMap).map(([moduleId, files]) => (
                    <div key={moduleId} className="flex items-start gap-2 text-xs">
                      <CheckCircle className="w-3.5 h-3.5 text-green-500 shrink-0 mt-0.5" />
                      <div>
                        <span className="font-medium text-gray-700">{moduleId}</span>
                        <span className="text-gray-400 ml-1">{files.join(", ")}</span>
                      </div>
                    </div>
                  ))}
                </div>
              </div>
            )}

            {/* Actions */}
            {step === "complete" && sessionId && (
              <div className="flex gap-2">
                <button
                  onClick={() => {
                    const a = document.createElement("a");
                    a.href = apiUrl(`/api/v1/project/${sessionId}/download-repo`);
                    a.download = "";
                    a.click();
                  }}
                  className="flex items-center gap-1.5 text-xs px-4 py-2 rounded-lg bg-blue-500 text-white hover:bg-blue-600 transition-colors"
                >
                  <Download className="w-3.5 h-3.5" /> 下载代码 zip
                </button>
                <button
                  onClick={resetProject}
                  className="flex items-center gap-1.5 text-xs px-4 py-2 rounded-lg border hover:bg-gray-50 text-gray-600"
                >
                  <RefreshCw className="w-3.5 h-3.5" /> 新建项目
                </button>
              </div>
            )}
          </div>
        </div>
      )}

      {/* Log drawer */}
      <LogDrawer
        logs={logs}
        open={showLogs}
        onClose={() => setShowLogs(false)}
      />
      {showLogs && (
        <div
          className="fixed inset-0 bg-black/20 z-40"
          onClick={() => setShowLogs(false)}
        />
      )}
    </div>
  );
}
