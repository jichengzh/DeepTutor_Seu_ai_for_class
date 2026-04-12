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
  BookOpen,
} from "lucide-react";
import { useGlobal } from "@/context/GlobalContext";
import { useTranslation } from "react-i18next";
import { apiUrl } from "@/lib/api";
import ReactMarkdown from "react-markdown";
import remarkGfm from "remark-gfm";
import SectionNav from "./components/SectionNav";
import ReviewPanel from "./components/ReviewPanel";

// ─── Section names for task mode ────────────────────────────────────────────

const TASK_SECTION_NAMES: Record<string, string> = {
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

const TASK_SECTION_ORDER = Object.keys(TASK_SECTION_NAMES);

// ─── Section names for syllabus mode ────────────────────────────────────────

const SYLLABUS_SECTION_NAMES: Record<string, string> = {
  cover: "封面信息",
  objectives: "课程简介与教学目标",
  prerequisites: "前置知识要求",
  content_structure: "课程内容及学时分配",
  teaching_methods: "教学方法与手段",
  grading_scheme: "课程考核与成绩评定",
  teaching_materials: "教材与参考资料",
  schedule: "教学进度安排",
};

const SYLLABUS_SECTION_ORDER = Object.keys(SYLLABUS_SECTION_NAMES);

// ─── Step indicator ─────────────────────────────────────────────────────────

const TASK_STEPS = [
  { key: "config", label: "配置" },
  { key: "task_generating", label: "生成中" },
  { key: "task_review", label: "审阅" },
  { key: "code_generating", label: "代码" },
];

const SYLLABUS_STEPS = [
  { key: "config", label: "配置" },
  { key: "task_generating", label: "生成中" },
  { key: "task_review", label: "审阅" },
];

function StepIndicator({ current, mode }: { current: string; mode: string }) {
  const steps = mode === "syllabus" ? SYLLABUS_STEPS : TASK_STEPS;
  const stepIndex = steps.findIndex((s) => s.key === current);
  const displayIndex = current === "complete" ? steps.length - 1 : stepIndex;

  return (
    <div className="flex items-center gap-2 mb-6">
      {steps.map((step, i) => (
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
          {i < steps.length - 1 && (
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
  mode,
}: {
  sections: Record<string, string>;
  currentSection: string | null;
  mode: string;
}) {
  const sectionNames = mode === "syllabus" ? SYLLABUS_SECTION_NAMES : TASK_SECTION_NAMES;
  const sectionOrder = mode === "syllabus" ? SYLLABUS_SECTION_ORDER : TASK_SECTION_ORDER;

  return (
    <ul className="space-y-2">
      {sectionOrder.map((key) => {
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
              {sectionNames[key] ?? key}
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
  const [newSectionTitle, setNewSectionTitle] = useState("");
  const [reflectionResult, setReflectionResult] = useState<{ rules: string[]; total: number } | null>(null);
  const [extracting, setExtracting] = useState(false);
  const fileInputRef = useRef<HTMLInputElement>(null);

  // Fetch knowledge base list
  useEffect(() => {
    fetch(apiUrl("/api/v1/knowledge/list"))
      .then((r) => r.json())
      .then((data) => setKbs((Array.isArray(data) ? data : (data.knowledge_bases ?? [])).map((kb: any) => kb.name)))
      .catch(() => {});
  }, []);

  const {
    step, mode, theme, selectedKb, webSearchEnabled, difficulty, cliTool, codexApiKey,
    referenceStructure, taskContent, taskSections, currentSection, sessionId,
    logs, error, agentLogs, generatedFiles, verifyPassed, coverageMap,
  } = projectState;

  const isSyllabus = mode === "syllabus";
  const sectionNames = isSyllabus ? SYLLABUS_SECTION_NAMES : TASK_SECTION_NAMES;
  const sectionOrder = isSyllabus ? SYLLABUS_SECTION_ORDER : TASK_SECTION_ORDER;

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
  const downloadTask = (format: "md" | "docx" | "pdf") => {
    if (!sessionId) return;
    const url = apiUrl(`/api/v1/project/${sessionId}/download-task?format=${format}`);
    const a = document.createElement("a");
    a.href = url;
    a.download = "";
    a.click();
  };

  // ─────────────────────────────────────────────────────────────────────────
  return (
    <div className="flex flex-col h-full p-6 max-w-5xl mx-auto">
      {/* Header */}
      <div className="flex items-center justify-between mb-4">
        <div className="flex items-center gap-3">
          {isSyllabus
            ? <BookOpen className="w-6 h-6 text-green-500" />
            : <FolderGit2 className="w-6 h-6 text-blue-500" />
          }
          <h1 className="text-xl font-semibold">
            {isSyllabus ? "课程大纲生成" : "Project Creator"}
          </h1>
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
      <StepIndicator current={step} mode={mode} />

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
          {/* Mode selection */}
          <div className="flex gap-3">
            <button
              onClick={() => setProjectState((p) => ({ ...p, mode: "task" }))}
              className={`flex-1 flex items-center gap-3 p-4 rounded-xl border-2 transition-colors ${
                !isSyllabus
                  ? "border-blue-500 bg-blue-50 dark:bg-blue-900/20"
                  : "border-gray-200 hover:border-gray-300 dark:border-gray-600"
              }`}
            >
              <FolderGit2 className={`w-5 h-5 ${!isSyllabus ? "text-blue-500" : "text-gray-400"}`} />
              <div className="text-left">
                <p className={`text-sm font-medium ${!isSyllabus ? "text-blue-700" : "text-gray-600"}`}>
                  任务书生成
                </p>
                <p className="text-xs text-gray-400">生成任务书 + 代码仓库</p>
              </div>
            </button>
            <button
              onClick={() => setProjectState((p) => ({ ...p, mode: "syllabus" }))}
              className={`flex-1 flex items-center gap-3 p-4 rounded-xl border-2 transition-colors ${
                isSyllabus
                  ? "border-green-500 bg-green-50 dark:bg-green-900/20"
                  : "border-gray-200 hover:border-gray-300 dark:border-gray-600"
              }`}
            >
              <BookOpen className={`w-5 h-5 ${isSyllabus ? "text-green-500" : "text-gray-400"}`} />
              <div className="text-left">
                <p className={`text-sm font-medium ${isSyllabus ? "text-green-700" : "text-gray-600"}`}>
                  课程大纲生成
                </p>
                <p className="text-xs text-gray-400">生成课程教学大纲文档</p>
              </div>
            </button>
          </div>

          <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
            {/* Upload card */}
            <div className="border-2 border-dashed rounded-xl p-6 flex flex-col items-center gap-3 hover:border-blue-400 transition-colors cursor-pointer"
              onClick={() => fileInputRef.current?.click()}
            >
              <Upload className="w-8 h-8 text-gray-400" />
              <div className="text-center">
                <p className="font-medium text-sm">
                  上传参考{isSyllabus ? "课程大纲" : "任务书"}
                </p>
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

            {/* Theme input & options */}
            <div className="flex flex-col gap-3">
              <label className="text-sm font-medium">
                {isSyllabus ? "新课程主题" : "新任务书主题"}
              </label>
              <textarea
                className="border rounded-lg p-3 text-sm resize-none h-24 focus:ring-2 focus:ring-blue-300 outline-none"
                placeholder={isSyllabus ? "例如：机器学习导论" : "例如：ROS 机器人导航暑期实习"}
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

              {/* Task mode only: difficulty + CLI tool */}
              {!isSyllabus && (
                <>
                  {/* Difficulty selector */}
                  <div className="mt-3">
                    <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">
                      代码难度
                    </label>
                    <div className="flex gap-2">
                      {(["low", "medium", "high"] as const).map((level) => {
                        const labels = { low: "低难度", medium: "中等", high: "高难度" };
                        const hints = { low: "≤10文件 / ≤3000行", medium: "≤25文件 / ≤7000行", high: "≤40文件 / ≤10000行" };
                        return (
                          <button
                            key={level}
                            type="button"
                            onClick={() => setProjectState((p) => ({ ...p, difficulty: level }))}
                            className={`flex-1 rounded-lg border px-3 py-2 text-sm text-center transition-colors ${
                              difficulty === level
                                ? "border-blue-500 bg-blue-50 text-blue-700 dark:bg-blue-900/30 dark:text-blue-300"
                                : "border-gray-200 text-gray-600 hover:border-gray-300 dark:border-gray-600 dark:text-gray-400"
                            }`}
                          >
                            <div className="font-medium">{labels[level]}</div>
                            <div className="text-xs text-gray-400 mt-0.5">{hints[level]}</div>
                          </button>
                        );
                      })}
                    </div>
                  </div>

                  {/* CLI tool selector */}
                  <div className="mt-3">
                    <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">
                      代码生成工具
                    </label>
                    <div className="flex gap-2">
                      {(["claude", "codex"] as const).map((tool) => {
                        const labels = { claude: "Claude CLI", codex: "Codex CLI" };
                        const hints  = { claude: "OAuth 认证（Claude Pro）", codex: "OpenAI Codex" };
                        return (
                          <button
                            key={tool}
                            type="button"
                            onClick={() => setProjectState((p) => ({ ...p, cliTool: tool }))}
                            className={`flex-1 rounded-lg border px-3 py-2 text-sm text-center transition-colors ${
                              cliTool === tool
                                ? "border-blue-500 bg-blue-50 text-blue-700 dark:bg-blue-900/30 dark:text-blue-300"
                                : "border-gray-200 text-gray-600 hover:border-gray-300 dark:border-gray-600 dark:text-gray-400"
                            }`}
                          >
                            <div className="font-medium">{labels[tool]}</div>
                            <div className="text-xs text-gray-400 mt-0.5">{hints[tool]}</div>
                          </button>
                        );
                      })}
                    </div>
                  </div>

                  {/* Codex API Key */}
                  {cliTool === "codex" && (
                    <div className="mt-2">
                      <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">
                        OpenAI API Key（可选，留空则使用 login 认证）
                      </label>
                      <input
                        type="password"
                        placeholder="sk-..."
                        value={codexApiKey}
                        onChange={(e) => setProjectState((p) => ({ ...p, codexApiKey: e.target.value }))}
                        className="w-full rounded-lg border border-gray-200 px-3 py-2 text-sm dark:border-gray-600 dark:bg-gray-800 dark:text-gray-200"
                      />
                    </div>
                  )}
                </>
              )}
            </div>
          </div>

          {/* 参考文档章节预览与编辑（动态增删） */}
          {referenceStructure && !isUploading && (
            <div className="border rounded-xl p-4 bg-gray-50 dark:bg-gray-800/50">
              <div className="flex items-center justify-between mb-3">
                <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300">
                  章节识别结果
                </h3>
                <span className="text-xs text-gray-400">
                  {Object.keys(referenceStructure.sections || {}).length} 个章节 · 可编辑内容、删除或添加章节
                </span>
              </div>
              <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
                {/* 动态遍历：先标准 key（按 section order 顺序），再自定义 key */}
                {[
                  ...sectionOrder.filter((k) => referenceStructure.sections?.[k] !== undefined),
                  ...Object.keys(referenceStructure.sections || {}).filter((k) => !sectionOrder.includes(k)),
                ].map((key) => {
                  const label = sectionNames[key] ?? referenceStructure.sections?.[key]?.title ?? key;
                  const sec = referenceStructure.sections?.[key];
                  const content = sec?.content ?? "";
                  const isEmpty = !content.trim();
                  return (
                    <div key={key} className="flex flex-col gap-1">
                      <div className="flex items-center gap-1.5">
                        {isEmpty
                          ? <AlertCircle className="w-3.5 h-3.5 text-amber-400 shrink-0" />
                          : <CheckCircle className="w-3.5 h-3.5 text-green-500 shrink-0" />
                        }
                        <span className="text-xs font-medium text-gray-600 dark:text-gray-400 flex-1 truncate">
                          {label}
                          {isEmpty && <span className="text-amber-400 ml-1">（未识别）</span>}
                        </span>
                        <button
                          type="button"
                          title="删除此章节"
                          onClick={() =>
                            setProjectState((p) => {
                              const newSections = { ...p.referenceStructure!.sections };
                              delete newSections[key];
                              return { ...p, referenceStructure: { ...p.referenceStructure, sections: newSections } };
                            })
                          }
                          className="text-gray-300 hover:text-red-400 transition-colors ml-1"
                        >
                          <X className="w-3.5 h-3.5" />
                        </button>
                      </div>
                      <textarea
                        className="w-full rounded-lg border border-gray-200 px-2.5 py-1.5 text-xs resize-none h-20 focus:ring-1 focus:ring-blue-300 outline-none dark:border-gray-600 dark:bg-gray-800 dark:text-gray-200"
                        placeholder={isEmpty ? "未识别到此章节，可手动填写参考内容..." : ""}
                        value={content}
                        onChange={(e) =>
                          setProjectState((p) => ({
                            ...p,
                            referenceStructure: {
                              ...p.referenceStructure,
                              sections: {
                                ...p.referenceStructure!.sections,
                                [key]: { ...(p.referenceStructure!.sections?.[key] ?? {}), content: e.target.value },
                              },
                            },
                          }))
                        }
                      />
                    </div>
                  );
                })}

                {/* 添加自定义章节 */}
                <div className="md:col-span-2 flex gap-2 mt-1">
                  <input
                    type="text"
                    placeholder="新章节名称（如：实验环境搭建）"
                    value={newSectionTitle}
                    onChange={(e) => setNewSectionTitle(e.target.value)}
                    onKeyDown={(e) => {
                      if (e.key === "Enter" && newSectionTitle.trim()) e.currentTarget.form?.requestSubmit?.();
                    }}
                    className="flex-1 rounded-lg border border-dashed border-gray-300 px-2.5 py-1.5 text-xs focus:ring-1 focus:ring-blue-300 outline-none dark:border-gray-600 dark:bg-gray-800 dark:text-gray-200"
                  />
                  <button
                    type="button"
                    disabled={!newSectionTitle.trim()}
                    onClick={() => {
                      const key = `custom_${Date.now()}`;
                      setProjectState((p) => ({
                        ...p,
                        referenceStructure: {
                          ...p.referenceStructure,
                          sections: {
                            ...p.referenceStructure!.sections,
                            [key]: { title: newSectionTitle.trim(), content: "" },
                          },
                        },
                      }));
                      setNewSectionTitle("");
                    }}
                    className="px-3 py-1.5 text-xs rounded-lg border border-blue-300 text-blue-600 hover:bg-blue-50 disabled:opacity-40 disabled:cursor-not-allowed transition-colors dark:border-blue-700 dark:text-blue-400 dark:hover:bg-blue-900/20"
                  >
                    + 添加章节
                  </button>
                </div>
              </div>
            </div>
          )}

          <button
            disabled={!canGenerate}
            onClick={startTaskGeneration}
            className={`self-end flex items-center gap-2 px-6 py-2.5 text-white rounded-lg font-medium text-sm hover:opacity-90 disabled:opacity-40 disabled:cursor-not-allowed transition-colors ${
              isSyllabus ? "bg-green-500 hover:bg-green-600" : "bg-blue-500 hover:bg-blue-600"
            }`}
          >
            开始生成{isSyllabus ? "课程大纲" : "任务书"} <ChevronRight className="w-4 h-4" />
          </button>
        </div>
      )}

      {/* ── Step 2: Generating ── */}
      {step === "task_generating" && (
        <div className="flex gap-6 flex-1 min-h-0">
          {/* Left: chapter progress */}
          <div className="w-48 shrink-0">
            <p className="text-sm font-medium mb-3 text-gray-700">章节进度</p>
            <ChapterProgress sections={taskSections} currentSection={currentSection} mode={mode} />
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

      {/* ── Step 3: Review (三栏布局) ── */}
      {step === "task_review" && (() => {
        // 解析 taskContent 为章节 map（按 --- 和 ## 分割）
        const parsedSections: { key: string; title: string; content: string }[] = [];
        const allSectionKeys = Object.keys(taskSections);
        if (allSectionKeys.length > 0) {
          // 使用 taskSections（逐章节生成时保存的）
          const order = [...sectionOrder.filter((k) => taskSections[k]), ...allSectionKeys.filter((k) => !sectionOrder.includes(k) && taskSections[k])];
          for (const key of order) {
            parsedSections.push({
              key,
              title: sectionNames[key] ?? key,
              content: taskSections[key] || "",
            });
          }
        }
        // 如果没有 taskSections，fallback 到整篇
        if (parsedSections.length === 0 && taskContent) {
          parsedSections.push({ key: "full", title: "完整文档", content: taskContent });
        }

        const reviewSelected = projectState.reviewSelectedSection || parsedSections[0]?.key || null;
        const selectedSec = parsedSections.find((s) => s.key === reviewSelected);
        const chatHistories = projectState.reviewChatHistories || {};

        const handleChatUpdate = (sectionKey: string, messages: any[]) => {
          setProjectState((prev) => ({
            ...prev,
            reviewChatHistories: { ...prev.reviewChatHistories, [sectionKey]: messages },
          }));
        };

        const handleAcceptRevision = async (sectionKey: string, newContent: string) => {
          // 更新前端状态
          let fullMd = "";
          setProjectState((prev) => {
            const newSections = { ...prev.taskSections, [sectionKey]: newContent };
            const order = [...sectionOrder.filter((k) => newSections[k]), ...Object.keys(newSections).filter((k) => !sectionOrder.includes(k) && newSections[k])];
            const suffix = isSyllabus ? "课程大纲" : "实习任务书";
            fullMd = `# ${prev.theme} — ${suffix}\n\n`;
            for (const k of order) {
              if (newSections[k]?.trim()) {
                fullMd += newSections[k].trim() + "\n\n---\n\n";
              }
            }
            return { ...prev, taskSections: newSections, taskContent: fullMd };
          });

          // 同步写回后端文件（更新 md + docx）
          if (sessionId) {
            // fullMd 可能在 setState 回调中还未赋值完，用 setTimeout 确保拿到最新值
            setTimeout(async () => {
              try {
                const currentState = projectState;
                const newSections = { ...currentState.taskSections, [sectionKey]: newContent };
                const order = [...sectionOrder.filter((k) => newSections[k]), ...Object.keys(newSections).filter((k) => !sectionOrder.includes(k) && newSections[k])];
                const suffix = isSyllabus ? "课程大纲" : "实习任务书";
                let md = `# ${currentState.theme} — ${suffix}\n\n`;
                for (const k of order) {
                  if (newSections[k]?.trim()) {
                    md += newSections[k].trim() + "\n\n---\n\n";
                  }
                }
                await fetch(apiUrl(`/api/v1/project/${sessionId}/apply-revision`), {
                  method: "POST",
                  headers: { "Content-Type": "application/json" },
                  body: JSON.stringify({
                    section_key: sectionKey,
                    new_content: newContent,
                    full_markdown: md,
                  }),
                });
              } catch (e) {
                console.error("Failed to sync revision to backend:", e);
              }
            }, 100);
          }
        };

        const handleExtractReflections = async () => {
          setExtracting(true);
          setReflectionResult(null);
          try {
            const resp = await fetch(apiUrl("/api/v1/project/reflections/extract"), {
              method: "POST",
              headers: { "Content-Type": "application/json" },
              body: JSON.stringify({
                chat_histories: chatHistories,
                theme: projectState.theme,
              }),
            });
            if (resp.ok) {
              const data = await resp.json();
              setReflectionResult({
                rules: data.extracted_rules || [],
                total: data.total_entries || 0,
              });
            } else {
              const errText = await resp.text();
              console.error("Extract reflections failed:", resp.status, errText);
              setReflectionResult({ rules: [`提炼失败: ${resp.status} ${errText.slice(0, 100)}`], total: 0 });
            }
          } catch (e: any) {
            console.error("Extract reflections error:", e);
            setReflectionResult({ rules: [`请求失败: ${e.message || e}`], total: 0 });
          }
          setExtracting(false);
        };

        // Check if any section has chat history
        const hasAnyChatHistory = Object.values(chatHistories).some((h) => h && h.length > 0);

        return (
          <div className="flex flex-1 min-h-0 gap-0">
            {/* Left: Section navigation */}
            <div className="w-48 shrink-0 border-r border-gray-200 dark:border-gray-700">
              <SectionNav
                sections={parsedSections.map((s) => ({
                  key: s.key,
                  title: s.title,
                  hasRevision: (chatHistories[s.key] || []).some((m: any) => m.accepted),
                  hasChat: (chatHistories[s.key] || []).length > 0,
                }))}
                selectedKey={reviewSelected}
                onSelect={(key) =>
                  setProjectState((prev) => ({ ...prev, reviewSelectedSection: key }))
                }
              />
            </div>

            {/* Center: Document preview */}
            <div className="flex-1 flex flex-col min-w-0">
              {/* Action bar */}
              <div className="flex items-center justify-between px-4 py-2 border-b border-gray-200 dark:border-gray-700 bg-gray-50 dark:bg-gray-800/50">
                <p className="text-sm font-medium text-gray-700 dark:text-gray-300">
                  {isSyllabus ? "课程大纲预览" : "任务书预览"}
                </p>
                <div className="flex gap-2">
                  {hasAnyChatHistory && (
                    <button
                      onClick={handleExtractReflections}
                      disabled={extracting}
                      className="flex items-center gap-1.5 text-xs px-3 py-1.5 rounded border border-amber-300 text-amber-600 hover:bg-amber-50 disabled:opacity-50 transition-colors"
                    >
                      {extracting ? (
                        <><Loader2 className="w-3 h-3 animate-spin" /> 提炼中...</>
                      ) : (
                        "提炼反思"
                      )}
                    </button>
                  )}
                  <button onClick={() => downloadTask("md")}
                    className="flex items-center gap-1.5 text-xs px-3 py-1.5 rounded border hover:bg-gray-50">
                    <Download className="w-3.5 h-3.5" /> .md
                  </button>
                  <button onClick={() => downloadTask("docx")}
                    className="flex items-center gap-1.5 text-xs px-3 py-1.5 rounded border hover:bg-gray-50">
                    <FileText className="w-3.5 h-3.5" /> .docx
                  </button>
                  {isSyllabus && (
                    <button onClick={() => downloadTask("pdf")}
                      className="flex items-center gap-1.5 text-xs px-3 py-1.5 rounded border hover:bg-gray-50">
                      <FileText className="w-3.5 h-3.5" /> .pdf
                    </button>
                  )}
                  {!isSyllabus && (
                    <button onClick={startCodeGeneration}
                      className="flex items-center gap-1.5 text-xs px-3 py-1.5 rounded bg-blue-500 text-white hover:bg-blue-600 transition-colors">
                      <FolderGit2 className="w-3.5 h-3.5" /> 生成代码仓库
                    </button>
                  )}
                </div>
              </div>

              {/* Reflection result banner */}
              {reflectionResult && (
                <div className="mx-4 mt-3 p-4 bg-amber-50 dark:bg-amber-900/20 border border-amber-200 dark:border-amber-800 rounded-xl">
                  <div className="flex items-center justify-between mb-2">
                    <h4 className="text-sm font-semibold text-amber-700 dark:text-amber-300">
                      反思提炼结果（已保存 {reflectionResult.rules.length} 条，全局共 {reflectionResult.total} 条）
                    </h4>
                    <button
                      onClick={() => setReflectionResult(null)}
                      className="text-amber-400 hover:text-amber-600 transition-colors"
                    >
                      <X className="w-4 h-4" />
                    </button>
                  </div>
                  {reflectionResult.rules.length > 0 ? (
                    <ul className="space-y-1">
                      {reflectionResult.rules.map((rule, i) => (
                        <li key={i} className="flex items-start gap-2 text-xs text-amber-800 dark:text-amber-200">
                          <CheckCircle className="w-3.5 h-3.5 text-amber-500 shrink-0 mt-0.5" />
                          <span>{rule}</span>
                        </li>
                      ))}
                    </ul>
                  ) : (
                    <p className="text-xs text-amber-600 dark:text-amber-400">未提炼到新的反思要点</p>
                  )}
                </div>
              )}

              {/* Preview area */}
              <div className="flex-1 overflow-y-auto px-6 py-4 prose prose-sm max-w-none dark:prose-invert">
                {selectedSec ? (
                  <ReactMarkdown remarkPlugins={[remarkGfm]}>
                    {selectedSec.content}
                  </ReactMarkdown>
                ) : (
                  <ReactMarkdown remarkPlugins={[remarkGfm]}>
                    {taskContent}
                  </ReactMarkdown>
                )}
              </div>
            </div>

            {/* Right: Review chat panel */}
            {selectedSec && (
              <div className="w-80 shrink-0 border-l border-gray-200 dark:border-gray-700">
                <ReviewPanel
                  sectionKey={selectedSec.key}
                  sectionTitle={selectedSec.title}
                  sectionContent={selectedSec.content}
                  chatHistory={(chatHistories[selectedSec.key] || []) as any}
                  onChatUpdate={handleChatUpdate}
                  onAcceptRevision={handleAcceptRevision}
                />
              </div>
            )}
          </div>
        );
      })()}

      {/* ── Step 4: Code generating / complete (task mode only) ── */}
      {!isSyllabus && (step === "code_generating" || step === "complete") && (
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
