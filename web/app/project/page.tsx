"use client";

import React, { useState, useEffect, useRef } from "react";
import {
  Upload,
  FileText,
  FileDown,
  CheckCircle2,
  Loader2,
  Play,
  RefreshCw,
  X,
  Settings,
  Database,
  Globe,
  BookOpen,
  Terminal,
  Package,
  CheckCircle,
  Circle,
  FileCode,
  Download,
  ArrowRight,
  Zap,
} from "lucide-react";
import ReactMarkdown from "react-markdown";
import remarkGfm from "remark-gfm";
import remarkMath from "remark-math";
import rehypeKatex from "rehype-katex";
import "katex/dist/katex.min.css";
import { useGlobal } from "@/context/GlobalContext";
import { apiUrl, wsUrl } from "@/lib/api";
import { useTranslation } from "react-i18next";

// Step names for progress display
const STEPS = [
  "config",
  "task_generating",
  "task_review",
  "code_generating",
  "complete",
] as const;

// Step labels
const STEP_LABELS = {
  config: "配置",
  task_generating: "生成中",
  task_review: "审阅",
  code_generating: "代码生成",
  complete: "完成",
};

// Section names for task generation mode
const TASK_SECTION_NAMES = {
  cover: "封面信息",
  objectives: "课程背景与目标",
  modules: "模块概述",
  details: "各模块设计内容",
  requirements: "作业要求",
  deliverables: "提交成果",
  grading: "成绩考核",
  schedule: "时间安排",
  references: "参考资源",
};

// Section names for syllabus generation mode
const SYLLABUS_SECTION_NAMES = {
  cover: "封面信息",
  objectives: "课程简介与教学目标",
  prerequisites: "前置知识要求",
  content_structure: "课程内容及学时分配",
  teaching_methods: "教学方法与手段",
  grading_scheme: "课程考核与成绩评定",
  teaching_materials: "教材与参考资料",
  schedule: "教学进度安排",
};

// Upload zone component
function UploadZone({
  onFileSelect,
  accept = ".docx,.pdf",
  label = "拖拽或点击上传",
  file,
  onRemove,
}: {
  onFileSelect: (file: File) => void;
  accept?: string;
  label?: string;
  file?: File | null;
  onRemove?: () => void;
}) {
  const [isDragging, setIsDragging] = useState(false);

  const handleDrop = (e: React.DragEvent) => {
    e.preventDefault();
    setIsDragging(false);
    const droppedFile = e.dataTransfer.files[0];
    if (droppedFile) onFileSelect(droppedFile);
  };

  const handleDragOver = (e: React.DragEvent) => {
    e.preventDefault();
    setIsDragging(true);
  };

  const handleDragLeave = () => setIsDragging(false);

  return (
    <div
      className={`relative border-2 border-dashed rounded-xl p-8 transition-all ${
        isDragging
          ? "border-blue-500 bg-blue-50 dark:bg-blue-900/20"
          : "border-slate-300 dark:border-slate-600 hover:border-slate-400 dark:hover:border-slate-500"
      } ${file ? "border-green-500 bg-green-50 dark:bg-green-900/20" : ""}`}
      onDrop={handleDrop}
      onDragOver={handleDragOver}
      onDragLeave={handleDragLeave}
    >
      <input
        type="file"
        accept={accept}
        onChange={(e) => {
          const selectedFile = e.target.files?.[0];
          if (selectedFile) onFileSelect(selectedFile);
        }}
        className="absolute inset-0 w-full h-full opacity-0 cursor-pointer"
      />
      {file ? (
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-3">
            <div className="w-12 h-12 bg-green-100 dark:bg-green-900/50 rounded-lg flex items-center justify-center">
              <FileText className="w-6 h-6 text-green-600 dark:text-green-400" />
            </div>
            <div>
              <p className="text-sm font-medium text-slate-900 dark:text-slate-100 truncate max-w-xs">
                {file.name}
              </p>
              <p className="text-xs text-slate-500 dark:text-slate-400">
                {(file.size / 1024).toFixed(1)} KB
              </p>
            </div>
          </div>
          {onRemove && (
            <button
              onClick={(e) => {
                e.stopPropagation();
                onRemove();
              }}
              className="p-2 text-red-500 hover:bg-red-100 dark:hover:bg-red-900/50 rounded-lg transition-colors"
            >
              <X className="w-5 h-5" />
            </button>
          )}
        </div>
      ) : (
        <div className="flex flex-col items-center justify-center gap-3">
          <div
            className={`w-16 h-16 rounded-full flex items-center justify-center ${
              isDragging
                ? "bg-blue-100 dark:bg-blue-900/50"
                : "bg-slate-100 dark:bg-slate-800"
            }`}
          >
            <Upload
              className={`w-8 h-8 ${
                isDragging
                  ? "text-blue-600 dark:text-blue-400"
                  : "text-slate-400 dark:text-slate-500"
              }`}
            />
          </div>
          <div className="text-center">
            <p className="text-sm font-medium text-slate-700 dark:text-slate-300 mb-1">
              {label}
            </p>
            <p className="text-xs text-slate-500 dark:text-slate-400">
              支持 {accept} 格式
            </p>
          </div>
        </div>
      )}
    </div>
  );
}

// Step indicator component
function StepIndicator({
  currentStep,
  steps,
}: {
  currentStep: string;
  steps: readonly string[];
}) {
  return (
    <div className="flex items-center gap-2 bg-slate-50 dark:bg-slate-800 px-4 py-2 rounded-lg">
      {steps.map((step, idx) => (
        <React.Fragment key={step}>
          <div
            className={`flex items-center gap-2 text-xs font-medium ${
              step === currentStep
                ? "text-blue-600 dark:text-blue-400"
                : steps.indexOf(currentStep) > idx
                  ? "text-green-600 dark:text-green-400"
                  : "text-slate-400 dark:text-slate-500"
            }`}
          >
            {steps.indexOf(currentStep) > idx ? (
              <CheckCircle className="w-4 h-4" />
            ) : step === currentStep ? (
              <Loader2 className="w-4 h-4 animate-spin" />
            ) : (
              <Circle className="w-4 h-4" />
            )}
            {STEP_LABELS[step as keyof typeof STEP_LABELS]}
          </div>
          {idx < steps.length - 1 && (
            <div
              className={`w-8 h-px ${
                steps.indexOf(currentStep) > idx
                  ? "bg-green-400 dark:bg-green-600"
                  : "bg-slate-300 dark:bg-slate-600"
              }`}
            />
          )}
        </React.Fragment>
      ))}
    </div>
  );
}

// File tree component for code generation
function FileTreeNode({
  node,
  depth = 0,
}: {
  node: any;
  depth?: number;
}) {
  const [expanded, setExpanded] = useState(depth < 1);

  return (
    <div>
      <div
        className={`flex items-center gap-2 py-1 hover:bg-slate-100 dark:hover:bg-slate-800 rounded cursor-pointer pl-${depth * 4}`}
        onClick={() => node.type === "directory" && setExpanded(!expanded)}
      >
        {node.type === "directory" ? (
          <Package className="w-4 h-4 text-amber-500 dark:text-amber-400" />
        ) : (
          <FileCode className="w-4 h-4 text-slate-500 dark:text-slate-400" />
        )}
        <span className="text-xs text-slate-700 dark:text-slate-300 truncate">
          {node.name}
        </span>
      </div>
      {node.type === "directory" && expanded && node.children && (
        <div>
          {node.children.map((child: any, idx: number) => (
            <FileTreeNode key={idx} node={child} depth={depth + 1} />
          ))}
        </div>
      )}
    </div>
  );
}

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
  const [historySessions, setHistorySessions] = useState<any[]>([]);
  const [showHistory, setShowHistory] = useState(false);
  const logContainerRef = useRef<HTMLDivElement | null>(null);

  // Fetch knowledge bases on mount
  useEffect(() => {
    fetch(apiUrl("/api/v1/knowledge/list"))
      .then((r) => r.json())
      .then((data) => {
        const names = data.map((kb: any) => kb.name);
        setKbs(names);
        if (!projectState.selectedKb && names.length > 0) {
          setProjectState((prev) => ({ ...prev, selectedKb: names[0] }));
        }
      })
      .catch((err) => console.error("Failed to fetch KBs:", err));
  }, []);

  // Fetch history sessions
  useEffect(() => {
    fetch(apiUrl("/api/v1/project/sessions?limit=20"))
      .then((r) => r.json())
      .then((data) => {
        setHistorySessions(data.sessions || []);
      })
      .catch((err) => console.error("Failed to fetch sessions:", err));
  }, []);

  // Auto-scroll logs
  useEffect(() => {
    if (logContainerRef.current) {
      logContainerRef.current.scrollTop = logContainerRef.current.scrollHeight;
    }
  }, [projectState.logs]);

  const handleFileUpload = async (file: File) => {
    try {
      setProjectState((prev) => ({ ...prev, uploadedFile: file }));
      await uploadReference(file);
    } catch (err: any) {
      console.error("Upload failed:", err);
      setProjectState((prev) => ({
        ...prev,
        error: err.message || "上传失败",
      }));
    }
  };

  const handleLoadHistory = (session: any) => {
    setProjectState((prev) => ({
      ...prev,
      sessionId: session.session_id,
      theme: session.theme,
      step: "task_review",
      mode: session.mode || "task",
      taskMdPath: session.task_md_path,
      taskDocxPath: session.task_docx_path,
      repoPath: session.repo_path,
      status: session.status,
    }));
    setShowHistory(false);

    // Load task content if available
    if (session.task_md_path) {
      fetch(`${apiUrl}/api/v1/project/${session.session_id}/download-task?format=md`)
        .then((r) => r.text())
        .then((content) => {
          setProjectState((prev) => ({ ...prev, taskContent: content }));
        })
        .catch((err) => console.error("Failed to load content:", err));
    }
  };

  const step = projectState.step;
  const sectionNames =
    projectState.mode === "syllabus"
      ? SYLLABUS_SECTION_NAMES
      : TASK_SECTION_NAMES;

  return (
    <div className="h-screen flex flex-col bg-white dark:bg-slate-900">
      {/* Header */}
      <div className="px-6 py-4 border-b border-slate-200 dark:border-slate-700 bg-white dark:bg-slate-800 flex items-center justify-between shrink-0">
        <div className="flex items-center gap-4">
          <div className="flex items-center gap-2">
            <FileText className="w-6 h-6 text-blue-600 dark:text-blue-400" />
            <h1 className="text-lg font-bold text-slate-900 dark:text-slate-100">
              {projectState.mode === "syllabus"
                ? "课程大纲生成"
                : "任务书生成"}
            </h1>
          </div>
          <StepIndicator currentStep={step} steps={STEPS} />
        </div>
        <div className="flex items-center gap-2">
          <button
            onClick={() => setShowHistory(!showHistory)}
            className="p-2 text-slate-600 dark:text-slate-400 hover:bg-slate-100 dark:hover:bg-slate-700 rounded-lg transition-colors"
            title="历史会话"
          >
            <BookOpen className="w-5 h-5" />
          </button>
          <button
            onClick={resetProject}
            className="p-2 text-slate-600 dark:text-slate-400 hover:bg-slate-100 dark:hover:bg-slate-700 rounded-lg transition-colors"
            title="新建会话"
          >
            <RefreshCw className="w-5 h-5" />
          </button>
        </div>
      </div>

      {/* Main Content */}
      <div className="flex-1 flex overflow-hidden">
        {/* History Sidebar */}
        {showHistory && (
          <div className="w-80 border-r border-slate-200 dark:border-slate-700 bg-slate-50 dark:bg-slate-800/50 flex flex-col overflow-hidden">
            <div className="px-4 py-3 border-b border-slate-200 dark:border-slate-700 flex items-center justify-between">
              <h3 className="text-sm font-semibold text-slate-700 dark:text-slate-200">
                历史会话
              </h3>
              <button
                onClick={() => setShowHistory(false)}
                className="text-slate-500 hover:text-slate-700 dark:hover:text-slate-300"
              >
                <X className="w-4 h-4" />
              </button>
            </div>
            <div className="flex-1 overflow-y-auto p-3 space-y-2">
              {historySessions.map((session, idx) => (
                <div
                  key={idx}
                  onClick={() => handleLoadHistory(session)}
                  className="w-full cursor-pointer text-left p-3 bg-white dark:bg-slate-700 rounded-lg hover:bg-slate-50 dark:hover:bg-slate-600 transition-colors border border-slate-200 dark:border-slate-600"
                >
                  <p className="text-sm font-medium text-slate-900 dark:text-slate-100 truncate mb-1">
                    {session.theme}
                  </p>
                  <p className="text-xs text-slate-500 dark:text-slate-400">
                    {session.mode === "syllabus" ? "课程大纲" : "任务书"} •{" "}
                    {STEP_LABELS[session.status as keyof typeof STEP_LABELS] ||
                      session.status}
                  </p>
                </div>
              ))}
              {historySessions.length === 0 && (
                <p className="text-sm text-slate-500 dark:text-slate-400 text-center py-8">
                  暂无历史会话
                </p>
              )}
            </div>
          </div>
        )}

        {/* Main Panel */}
        <div className="flex-1 flex flex-col overflow-hidden">
          {/* Step 1: Configuration */}
          {step === "config" && (
            <div className="flex-1 overflow-y-auto p-8 max-w-4xl mx-auto space-y-6">
              {/* Mode Selection */}
              <div className="bg-slate-50 dark:bg-slate-800 p-6 rounded-xl">
                <h2 className="text-sm font-semibold text-slate-700 dark:text-slate-200 mb-4">
                  选择生成模式
                </h2>
                <div className="flex gap-4">
                  <label className="flex-1 flex items-center gap-3 p-4 bg-white dark:bg-slate-700 rounded-lg border-2 border-slate-200 dark:border-slate-600 cursor-pointer hover:border-blue-400 dark:hover:border-blue-500 transition-all">
                    <input
                      type="radio"
                      name="mode"
                      value="task"
                      checked={projectState.mode === "task"}
                      onChange={(e) =>
                        setProjectState((prev) => ({
                          ...prev,
                          mode: "task" as const,
                        }))
                      }
                      className="w-4 h-4 text-blue-600"
                    />
                    <div>
                      <p className="font-medium text-slate-900 dark:text-slate-100">
                        任务书生成
                      </p>
                      <p className="text-xs text-slate-500 dark:text-slate-400">
                        生成实习/课程任务书文档
                      </p>
                    </div>
                  </label>
                  <label className="flex-1 flex items-center gap-3 p-4 bg-white dark:bg-slate-700 rounded-lg border-2 border-slate-200 dark:border-slate-600 cursor-pointer hover:border-blue-400 dark:hover:border-blue-500 transition-all">
                    <input
                      type="radio"
                      name="mode"
                      value="syllabus"
                      checked={projectState.mode === "syllabus"}
                      onChange={(e) =>
                        setProjectState((prev) => ({
                          ...prev,
                          mode: "syllabus" as const,
                        }))
                      }
                      className="w-4 h-4 text-blue-600"
                    />
                    <div>
                      <p className="font-medium text-slate-900 dark:text-slate-100">
                        课程大纲生成
                      </p>
                      <p className="text-xs text-slate-500 dark:text-slate-400">
                        生成课程教学大纲 PDF
                      </p>
                    </div>
                  </label>
                </div>
              </div>

              {/* Reference Upload */}
              <div>
                <h2 className="text-sm font-semibold text-slate-700 dark:text-slate-200 mb-2">
                  上传参考{" "}
                  {projectState.mode === "syllabus"
                    ? "课程大纲"
                    : "任务书"}
                </h2>
                <UploadZone
                  onFileSelect={handleFileUpload}
                  file={projectState.uploadedFile}
                  onRemove={() =>
                    setProjectState((prev) => ({
                      ...prev,
                      uploadedFile: null,
                      referenceStructure: null,
                    }))
                  }
                  label={
                    projectState.mode === "syllabus"
                      ? "拖拽或点击上传课程大纲"
                      : "拖拽或点击上传任务书"
                  }
                />
              </div>

              {/* Theme Input */}
              <div>
                <h2 className="text-sm font-semibold text-slate-700 dark:text-slate-200 mb-2">
                  {projectState.mode === "syllabus"
                    ? "新课程主题"
                    : "新任务书主题"}
                </h2>
                <input
                  type="text"
                  value={projectState.theme}
                  onChange={(e) =>
                    setProjectState((prev) => ({
                      ...prev,
                      theme: e.target.value,
                    }))
                  }
                  placeholder={
                    projectState.mode === "syllabus"
                      ? "例如：机器学习导论"
                      : "例如：ROS 机器人导航实习"
                  }
                  className="w-full px-4 py-3 bg-white dark:bg-slate-700 border border-slate-200 dark:border-slate-600 rounded-lg focus:outline-none focus:ring-2 focus:ring-blue-500/20 focus:border-blue-500 transition-all text-slate-900 dark:text-slate-100 placeholder:text-slate-400 dark:placeholder:text-slate-500"
                />
              </div>

              {/* Knowledge Base Selection */}
              <div>
                <h2 className="text-sm font-semibold text-slate-700 dark:text-slate-200 mb-2">
                  知识库（可选）
                </h2>
                <div className="flex items-center gap-2">
                  <Database className="w-5 h-5 text-slate-400" />
                  <select
                    value={projectState.selectedKb}
                    onChange={(e) =>
                      setProjectState((prev) => ({
                        ...prev,
                        selectedKb: e.target.value,
                      }))
                    }
                    className="flex-1 px-4 py-2 bg-white dark:bg-slate-700 border border-slate-200 dark:border-slate-600 rounded-lg focus:outline-none focus:ring-2 focus:ring-blue-500/20 focus:border-blue-500 transition-all text-slate-900 dark:text-slate-100"
                  >
                    <option value="">不使用知识库</option>
                    {kbs.map((kb) => (
                      <option key={kb} value={kb}>
                        {kb}
                      </option>
                    ))}
                  </select>
                </div>
              </div>

              {/* Web Search Toggle */}
              <div>
                <h2 className="text-sm font-semibold text-slate-700 dark:text-slate-200 mb-2">
                  启用网络搜索
                </h2>
                <label className="flex items-center gap-3 p-4 bg-slate-50 dark:bg-slate-800 rounded-lg cursor-pointer">
                  <input
                    type="checkbox"
                    checked={projectState.webSearchEnabled}
                    onChange={(e) =>
                      setProjectState((prev) => ({
                        ...prev,
                        webSearchEnabled: e.target.checked,
                      }))
                    }
                    className="w-5 h-5 text-blue-600 rounded focus:outline-none focus:ring-2 focus:ring-blue-500/20"
                  />
                  <div className="flex items-center gap-2">
                    <Globe className="w-5 h-5 text-slate-400" />
                    <span className="text-sm text-slate-700 dark:text-slate-300">
                      使用网络搜索获取最新资料
                    </span>
                  </div>
                </label>
              </div>

              {/* Error Display */}
              {projectState.error && (
                <div className="p-4 bg-red-50 dark:bg-red-900/20 text-red-700 dark:text-red-400 rounded-lg border border-red-200 dark:border-red-800">
                  {projectState.error}
                </div>
              )}

              {/* Start Button */}
              <div className="flex items-center gap-3">
                <button
                  onClick={startTaskGeneration}
                  disabled={
                    !projectState.theme.trim() ||
                    !projectState.referenceStructure
                  }
                  className="flex-1 flex items-center justify-center gap-2 px-6 py-3 bg-blue-600 text-white rounded-lg hover:bg-blue-700 disabled:opacity-50 disabled:hover:bg-blue-600 transition-all font-medium"
                >
                  <Play className="w-5 h-5" />
                  开始生成
                </button>
              </div>
            </div>
          )}

          {/* Step 2: Task/Syllabus Generating */}
          {step === "task_generating" && (
            <div className="flex-1 flex overflow-hidden">
              {/* Left Panel: Progress */}
              <div className="w-80 border-r border-slate-200 dark:border-slate-700 bg-slate-50 dark:bg-slate-800/50 flex flex-col overflow-hidden">
                <div className="px-4 py-3 border-b border-slate-200 dark:border-slate-700 flex items-center gap-2">
                  <div className="w-2 h-2 rounded-full bg-blue-500 animate-pulse" />
                  <h3 className="text-sm font-semibold text-slate-700 dark:text-slate-200">
                    生成进度
                  </h3>
                </div>
                <div className="flex-1 overflow-y-auto p-3 space-y-2">
                  {Object.entries(sectionNames).map(([key, name]) => (
                    <div
                      key={key}
                      className={`flex items-center gap-2 p-2 rounded-lg ${
                        projectState.taskSections[key]
                          ? "bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-400"
                          : projectState.currentSection === key
                            ? "bg-blue-100 dark:bg-blue-900/30 text-blue-700 dark:text-blue-400"
                            : "bg-slate-100 dark:bg-slate-700 text-slate-500 dark:text-slate-400"
                      }`}
                    >
                      {projectState.taskSections[key] ? (
                        <CheckCircle2 className="w-4 h-4" />
                      ) : projectState.currentSection === key ? (
                        <Loader2 className="w-4 h-4 animate-spin" />
                      ) : (
                        <Circle className="w-4 h-4" />
                      )}
                      <span className="text-sm font-medium">{name}</span>
                    </div>
                  ))}
                </div>
              </div>

              {/* Right Panel: Preview */}
              <div className="flex-1 overflow-y-auto p-6">
                <div className="max-w-3xl mx-auto">
                  <div className="prose prose prose-slate dark:prose-invert prose-sm max-w-none">
                    <ReactMarkdown
                      remarkPlugins={[remarkGfm, remarkMath]}
                      rehypePlugins={[rehypeKatex]}
                    >
                      {projectState.taskContent || "*等待生成...*"}
                    </ReactMarkdown>
                  </div>
                </div>
              </div>
            </div>
          )}

          {/* Step 3: Task/Syllabus Review */}
          {step === "task_review" && (
            <div className="flex-1 flex flex-col overflow-hidden">
              {/* Preview Area */}
              <div className="flex-1 overflow-y-auto p-6">
                <div className="max-w-3xl mx-auto">
                  <div className="prose prose prose-slate dark:prose-invert max-w-none">
                    <ReactMarkdown
                      remarkPlugins={[remarkGfm, remarkMath]}
                      rehypePlugins={[rehypeKatex]}
                    >
                      {projectState.taskContent}
                    </ReactMarkdown>
                  </div>
                </div>
              </div>

              {/* Action Bar */}
              <div className="px-6 py-4 border-t border-slate-200 dark:border-slate-700 bg-white dark:bg-slate-800 flex items-center justify-between shrink-0">
                <div className="flex items-center gap-3">
                  {/* Download DOCX */}
                  <button
                    onClick={() => {
                      const url = `${apiUrl}/api/v1/project/${projectState.sessionId}/download-task?format=docx`;
                      window.open(url, "_blank");
                    }}
                    className="flex items-center gap-2 px-4 py-2 bg-slate-600 text-white rounded-lg hover:bg-slate-700 transition-colors"
                  >
                    <FileDown className="w-4 h-4" />
                    下载 Word
                  </button>

                  {/* Download PDF */}
                  <button
                    onClick={() => {
                      const url = `${apiUrl}/api/v1/project/${projectState.sessionId}/download-task?format=pdf`;
                      window.open(url, "_blank");
                    }}
                    className="flex items-center gap-2 px-4 py-2 bg-green-600 text-white rounded-lg hover:bg-green-700 transition-colors"
                  >
                    <FileDown className="w-4 h-4" />
                    下载 PDF
                  </button>
                </div>

                {/* Generate Code Button */}
                <button
                  onClick={startCodeGeneration}
                  className="flex items-center gap-2 px-6 py-2 bg-blue-600 text-white rounded-lg hover:bg-blue-700 transition-colors font-medium"
                >
                  <Zap className="w-4 h-4" />
                  生成代码仓库
                  <ArrowRight className="w-4 h-4" />
                </button>
              </div>
            </div>
          )}

          {/* Step 4: Code Generating */}
          {step === "code_generating" && (
            <div className="flex-1 flex overflow-hidden">
              {/* Left Panel: Agent Logs */}
              <div className="w-96 border-r border-slate-200 dark:border-slate-700 bg-slate-900 flex flex-col overflow-hidden">
                <div className="px-4 py-3 border-b border-slate-700 flex items-center gap-2">
                  <Terminal className="w-4 h-4 text-green-400" />
                  <h3 className="text-sm font-semibold text-slate-200">
                    Agent 日志
                  </h3>
                </div>
                <div
                  ref={logContainerRef}
                  className="flex-1 overflow-y-auto p-3 space-y-1 font-mono text-xs"
                >
                  {projectState.logs.map((log, idx) => (
                    <div
                      key={idx}
                      className={`px-2 py-1 rounded break-words ${
                        log.type === "error"
                          ? "bg-red-900/50 text-red-400"
                          : log.type === "status"
                            ? "bg-blue-900/50 text-blue-400"
                            : "text-slate-400"
                      }`}
                    >
                      <span className="text-slate-500">
                        {log.timestamp}
                      </span>{" "}
                      {log.content}
                    </div>
                  ))}
                </div>
              </div>

              {/* Right Panel: File Tree & Progress */}
              <div className="flex-1 overflow-y-auto p-6">
                <div className="max-w-3xl mx-auto space-y-6">
                  {/* File Tree */}
                  {projectState.generatedFiles.length > 0 && (
                    <div>
                      <h3 className="text-sm font-semibold text-slate-700 dark:text-slate-200 mb-3">
                        生成的文件
                      </h3>
                      <div className="bg-white dark:bg-slate-800 border border-slate-200 dark:border-slate-700 rounded-lg p-4">
                        {projectState.generatedFiles.map(
                          (node: any, idx: number) => (
                            <FileTreeNode key={idx} node={node} />
                          ),
                        )}
                      </div>
                    </div>
                  )}

                  {/* Verify Result */}
                  {projectState.verifyPassed !== null && (
                    <div
                      className={`p-4 rounded-lg border ${
                        projectState.verifyPassed
                          ? "bg-green-50 dark:bg-green-900/20 border-green-200 dark:border-green-800"
                          : "bg-red-50 dark:bg-red-900/20 border-red-200 dark:border-red-800"
                      }`}
                    >
                      <div className="flex items-center gap-2">
                        {projectState.verifyPassed ? (
                          <CheckCircle2 className="w-5 h-5 text-green-600 dark:text-green-400" />
                        ) : (
                          <X className="w-5 h-5 text-red-600 dark:text-red-400" />
                        )}
                        <span
                          className={`font-medium ${
                            projectState.verifyPassed
                              ? "text-green-700 dark:text-green-400"
                              : "text-red-700 dark:text-red-400"
                          }`}
                        >
                          {projectState.verifyPassed
                            ? "验证通过"
                            : "验证失败"}
                        </span>
                      </div>
                    </div>
                  )}

                  {/* Error Display */}
                  {projectState.error && (
                    <div className="p-4 bg-red-50 dark:bg-red-900/20 text-red-700 dark:text-red-400 rounded-lg border border-red-200 dark:border-red-800">
                      {projectState.error}
                    </div>
                  )}
                </div>
              </div>
            </div>
          )}

          {/* Step 5: Complete */}
          {step === "complete" && (
            <div className="flex-1 overflow-y-auto p-8">
              <div className="max-w-2xl mx-auto text-center space-y-6">
                <div className="w-20 h-20 bg-green-100 dark:bg-green-900/50 rounded-full flex items-center justify-center mx-auto">
                  <CheckCircle2 className="w-10 h-10 text-green-600 dark:text-green-400" />
                </div>
                <div>
                  <h2 className="text-2xl font-bold text-slate-900 dark:text-slate-100 mb-2">
                    生成完成！
                  </h2>
                  <p className="text-slate-600 dark:text-slate-400">
                    您的任务书和代码仓库已成功生成
                  </p>
                </div>

                <div className="flex items-center justify-center gap-3">
                  <button
                    onClick={() => {
                      const url = `${apiUrl}/api/v1/project/${projectState.sessionId}/download-task?format=docx`;
                      window.open(url, "_blank");
                    }}
                    className="flex items-center gap-2 px-6 py-3 bg-slate-600 text-white rounded-lg hover:bg-slate-700 transition-colors"
                  >
                    <FileDown className="w-5 h-5" />
                    下载任务书
                  </button>

                  <button
                    onClick={() => {
                      const url = `${apiUrl}/api/v1/project/${projectState.sessionId}/download-repo`;
                      window.open(url, "_blank");
                    }}
                    className="flex items-center gap-2 px-6 py-3 bg-blue-600 text-white rounded-lg hover:bg-blue-700 transition-colors"
                  >
                    <Download className="w-5 h-5" />
                    下载代码
                  </button>
                </div>

                <button
                  onClick={resetProject}
                  className="flex items-center gap-2 px-6 py-3 bg-green-600 text-white rounded-lg hover:bg-green-700 transition-colors"
                >
                  <Play className="w-5 h-5" />
                  创建新项目
                </button>
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
