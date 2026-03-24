"use client";

import { useEffect, useState } from "react";
import { apiUrl } from "@/lib/api";
import { useRouter } from "next/navigation";
import {
  FolderGit2,
  Trash2,
  Download,
  Clock,
  CheckCircle,
  AlertCircle,
  Loader2,
} from "lucide-react";

interface SessionSummary {
  session_id: string;
  theme: string;
  status: "init" | "task_generating" | "task_generated" | "code_generating" | "complete";
  created_at: number;
  updated_at: number;
  task_md_path?: string;
  repo_path?: string;
}

const STATUS_CONFIG: Record<
  SessionSummary["status"],
  { label: string; color: string; spin: boolean }
> = {
  init:            { label: "初始化",     color: "text-gray-400",   spin: false },
  task_generating: { label: "生成任务书中", color: "text-blue-400",   spin: true  },
  task_generated:  { label: "任务书就绪",  color: "text-yellow-500", spin: false },
  code_generating: { label: "生成代码中",  color: "text-purple-400", spin: true  },
  complete:        { label: "已完成",     color: "text-green-400",  spin: false },
};

function formatDate(ts: number) {
  return new Date(ts * 1000).toLocaleString("zh-CN", {
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
  });
}

export default function ProjectHistoryPage() {
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
      const r = await fetch(apiUrl(`/api/v1/project/sessions/${sessionId}`), {
        method: "DELETE",
      });
      if (r.ok) {
        setSessions((prev) => prev.filter((s) => s.session_id !== sessionId));
      } else {
        alert("删除失败：" + (await r.text()));
      }
    } finally {
      setDeleting(null);
    }
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
      {/* Title row */}
      <div className="flex items-center justify-between mb-6">
        <div className="flex items-center gap-2">
          <FolderGit2 className="w-6 h-6 text-blue-400" />
          <h1 className="text-xl font-semibold text-gray-800 dark:text-gray-100">历史项目</h1>
          <span className="text-sm text-gray-400 ml-1">({sessions.length})</span>
        </div>
        <button
          onClick={() => router.push("/project")}
          className="px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white rounded-lg text-sm transition-colors"
        >
          + 新建项目
        </button>
      </div>

      {sessions.length === 0 ? (
        <div className="flex flex-col items-center justify-center flex-1 gap-4 text-gray-400">
          <FolderGit2 className="w-16 h-16 opacity-20" />
          <p>暂无历史项目</p>
          <button
            onClick={() => router.push("/project")}
            className="text-blue-400 hover:underline text-sm"
          >
            创建第一个项目 →
          </button>
        </div>
      ) : (
        <div className="flex flex-col gap-3 overflow-y-auto">
          {sessions.map((session) => {
            const cfg = STATUS_CONFIG[session.status] ?? STATUS_CONFIG.init;
            const StatusIcon = cfg.spin ? Loader2 : session.status === "complete" ? CheckCircle : Clock;
            return (
              <div
                key={session.session_id}
                className="bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-700 rounded-xl p-4 hover:border-gray-300 dark:hover:border-gray-600 transition-colors"
              >
                {/* Theme + status */}
                <div className="flex items-start justify-between gap-4 mb-2">
                  <div className="flex-1 min-w-0">
                    <h2 className="text-gray-800 dark:text-gray-100 font-medium truncate">
                      {session.theme || "（无主题）"}
                    </h2>
                    <p className="text-gray-400 text-xs mt-0.5 font-mono truncate">
                      {session.session_id}
                    </p>
                  </div>
                  <div className={`flex items-center gap-1 text-sm shrink-0 ${cfg.color}`}>
                    <StatusIcon className={`w-4 h-4 ${cfg.spin ? "animate-spin" : ""}`} />
                    <span>{cfg.label}</span>
                  </div>
                </div>

                {/* Timestamps */}
                <div className="flex items-center gap-4 text-xs text-gray-400 mb-3">
                  <span>创建：{formatDate(session.created_at)}</span>
                  <span>更新：{formatDate(session.updated_at)}</span>
                </div>

                {/* Actions */}
                <div className="flex items-center gap-2 flex-wrap">
                  {session.task_md_path && (
                    <a
                      href={apiUrl(`/api/v1/project/${session.session_id}/download-task?format=md`)}
                      download
                      className="flex items-center gap-1 px-3 py-1.5 bg-gray-100 dark:bg-gray-700 hover:bg-gray-200 dark:hover:bg-gray-600 text-gray-600 dark:text-gray-300 rounded-lg text-xs transition-colors"
                    >
                      <Download className="w-3 h-3" /> 下载任务书
                    </a>
                  )}
                  {session.status === "complete" && (
                    <a
                      href={apiUrl(`/api/v1/project/${session.session_id}/download-repo`)}
                      download
                      className="flex items-center gap-1 px-3 py-1.5 bg-blue-600 hover:bg-blue-700 text-white rounded-lg text-xs transition-colors"
                    >
                      <Download className="w-3 h-3" /> 下载代码 zip
                    </a>
                  )}
                  <button
                    onClick={() => handleDelete(session.session_id)}
                    disabled={deleting === session.session_id}
                    className="flex items-center gap-1 px-3 py-1.5 bg-red-50 dark:bg-red-900/30 hover:bg-red-100 dark:hover:bg-red-900/50 text-red-500 rounded-lg text-xs transition-colors disabled:opacity-50 ml-auto"
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
