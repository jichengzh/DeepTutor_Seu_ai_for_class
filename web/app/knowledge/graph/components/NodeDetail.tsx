"use client";

import { useEffect, useState } from "react";
import { apiUrl } from "@/lib/api";
import { useTranslation } from "react-i18next";
import ReactMarkdown from "react-markdown";
import remarkGfm from "remark-gfm";
import { Loader2, AlertCircle, FileText, ChevronDown, ChevronUp } from "lucide-react";

// ── Boilerplate stripper (mirrors GraphViewer) ───────────────────────────────
const BOILERPLATE_PATTERNS = [
  /\n*本作品已使用人工智能进行翻译[^]*$/,
  /\n*欢迎您提供反馈和意见[^]*$/,
  /\n*translation-feedback@\S+[^]*$/,
];

function stripBoilerplate(text: string): string {
  let out = text;
  for (const re of BOILERPLATE_PATTERNS) out = out.replace(re, "");
  return out.trim();
}

interface NumberedItem {
  identifier: string;
  type: string;
  text: string;
}

interface RelatedChapter {
  id: string;
  title: string;
}

interface NodeDetailData {
  id: string;
  title: string;
  level: number;
  source_file: string;
  page_idx: number;
  item_count: number;
  body_preview: string;
  key_topics: string[];
  full_body?: string;
  numbered_items?: NumberedItem[];
  related_chapters?: RelatedChapter[];
}

interface Props {
  kbName: string;
  nodeId: string;
  onRelatedNodeSelect?: (id: string) => void;
}

const LEVEL_LABELS: Record<number, string> = {
  1: "Chapter",
  2: "Section",
  3: "Subsection",
  4: "Sub-subsection",
};

// Prose styles for markdown rendering
const PROSE_COMPONENTS = {
  h1: ({ children }: any) => (
    <h1 className="text-sm font-bold text-slate-800 dark:text-slate-100 mt-3 mb-1">{children}</h1>
  ),
  h2: ({ children }: any) => (
    <h2 className="text-xs font-bold text-slate-700 dark:text-slate-200 mt-2.5 mb-1">{children}</h2>
  ),
  h3: ({ children }: any) => (
    <h3 className="text-xs font-semibold text-slate-600 dark:text-slate-300 mt-2 mb-0.5">{children}</h3>
  ),
  p: ({ children }: any) => (
    <p className="text-xs text-slate-700 dark:text-slate-300 leading-relaxed mb-1.5">{children}</p>
  ),
  ul: ({ children }: any) => (
    <ul className="list-disc list-inside text-xs text-slate-700 dark:text-slate-300 space-y-0.5 mb-1.5 ml-1">{children}</ul>
  ),
  ol: ({ children }: any) => (
    <ol className="list-decimal list-inside text-xs text-slate-700 dark:text-slate-300 space-y-0.5 mb-1.5 ml-1">{children}</ol>
  ),
  li: ({ children }: any) => <li className="leading-relaxed">{children}</li>,
  strong: ({ children }: any) => (
    <strong className="font-semibold text-slate-800 dark:text-slate-100">{children}</strong>
  ),
  em: ({ children }: any) => (
    <em className="italic text-slate-600 dark:text-slate-400">{children}</em>
  ),
  code: ({ children, className }: any) => {
    const isBlock = className?.startsWith("language-");
    return isBlock ? (
      <code className="block bg-slate-100 dark:bg-slate-700/60 rounded px-2 py-1.5 text-xs font-mono text-slate-700 dark:text-slate-300 whitespace-pre-wrap my-1.5">
        {children}
      </code>
    ) : (
      <code className="bg-slate-100 dark:bg-slate-700/60 rounded px-1 py-0.5 text-xs font-mono text-slate-700 dark:text-slate-300">
        {children}
      </code>
    );
  },
  blockquote: ({ children }: any) => (
    <blockquote className="border-l-2 border-indigo-400 pl-3 text-xs italic text-slate-500 dark:text-slate-400 my-1.5">
      {children}
    </blockquote>
  ),
  hr: () => <hr className="border-slate-200 dark:border-slate-700 my-2" />,
  table: ({ children }: any) => (
    <div className="overflow-x-auto my-1.5">
      <table className="text-xs border-collapse w-full">{children}</table>
    </div>
  ),
  th: ({ children }: any) => (
    <th className="border border-slate-200 dark:border-slate-600 px-2 py-1 bg-slate-50 dark:bg-slate-700/50 font-semibold text-slate-700 dark:text-slate-300 text-left">
      {children}
    </th>
  ),
  td: ({ children }: any) => (
    <td className="border border-slate-200 dark:border-slate-600 px-2 py-1 text-slate-600 dark:text-slate-400">
      {children}
    </td>
  ),
};

export default function NodeDetail({ kbName, nodeId, onRelatedNodeSelect }: Props) {
  const { t } = useTranslation();
  const [detail, setDetail] = useState<NodeDetailData | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [expandedItems, setExpandedItems] = useState<Set<string>>(new Set());

  useEffect(() => {
    setLoading(true);
    setError(null);
    setDetail(null);

    fetch(apiUrl(`/api/v1/knowledge/${kbName}/chapter-graph/${nodeId}`))
      .then((res) => {
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        return res.json();
      })
      .then((data: NodeDetailData) => setDetail(data))
      .catch((e) => setError(e.message || "Failed to load node detail"))
      .finally(() => setLoading(false));
  }, [kbName, nodeId]);

  const toggleItem = (id: string) => {
    setExpandedItems((prev) => {
      const next = new Set(prev);
      if (next.has(id)) next.delete(id);
      else next.add(id);
      return next;
    });
  };

  if (loading) {
    return (
      <div className="flex items-center justify-center h-48">
        <Loader2 className="w-6 h-6 animate-spin text-slate-400" />
      </div>
    );
  }

  if (error) {
    return (
      <div className="flex flex-col items-center justify-center h-48 text-center px-4 gap-2">
        <AlertCircle className="w-6 h-6 text-red-500" />
        <p className="text-sm text-red-600 dark:text-red-400">{error}</p>
      </div>
    );
  }

  if (!detail) return null;

  const levelColor =
    detail.level === 1
      ? "bg-violet-100 dark:bg-violet-900/30 text-violet-700 dark:text-violet-300"
      : detail.level === 2
      ? "bg-cyan-100 dark:bg-cyan-900/30 text-cyan-700 dark:text-cyan-300"
      : "bg-slate-100 dark:bg-slate-700 text-slate-600 dark:text-slate-400";

  const body = stripBoilerplate(detail.full_body || detail.body_preview || "");
  const displayTitle = stripBoilerplate(detail.title);

  // Title style varies by level to convey hierarchy
  const titleClass =
    detail.level === 1
      ? "font-bold text-lg text-slate-900 dark:text-white mb-2 break-words leading-snug"
      : detail.level === 2
      ? "font-semibold text-base text-slate-800 dark:text-slate-100 mb-2 break-words leading-snug"
      : "font-medium text-sm text-slate-700 dark:text-slate-200 mb-2 break-words leading-snug";

  // Left-border accent by level
  const borderAccent =
    detail.level === 1
      ? "border-l-4 border-violet-500 pl-3"
      : detail.level === 2
      ? "border-l-2 border-cyan-400 pl-3"
      : "border-l border-slate-300 dark:border-slate-600 pl-3";

  return (
    <div className="flex-1 overflow-y-auto">
      {/* Title and metadata */}
      <div className="p-4 border-b border-slate-200 dark:border-slate-700">
        <div className={`${borderAccent} mb-3`}>
          <h3 className={titleClass}>{displayTitle}</h3>
        </div>
        <div className="flex flex-wrap gap-2">
          <span className={`px-2 py-0.5 rounded text-xs font-medium ${levelColor}`}>
            {LEVEL_LABELS[detail.level] || `Level ${detail.level}`}
          </span>
          <span className="px-2 py-0.5 bg-slate-100 dark:bg-slate-700 text-slate-500 dark:text-slate-400 rounded text-xs">
            {t("Page")} {detail.page_idx + 1}
          </span>
          <span className="px-2 py-0.5 bg-slate-100 dark:bg-slate-700 text-slate-500 dark:text-slate-400 rounded text-xs">
            {detail.item_count} {t("items")}
          </span>
        </div>
      </div>

      {/* Key topics */}
      {detail.key_topics && detail.key_topics.length > 0 && (
        <div className="p-4 border-b border-slate-200 dark:border-slate-700">
          <h4 className="text-xs font-semibold text-slate-400 dark:text-slate-500 uppercase tracking-wide mb-2">
            {t("Key Topics")}
          </h4>
          <div className="flex flex-wrap gap-1.5">
            {detail.key_topics.map((topic, i) => (
              <span
                key={i}
                className="px-2 py-0.5 bg-indigo-50 dark:bg-indigo-900/25 text-indigo-600 dark:text-indigo-400 rounded-full text-xs border border-indigo-100 dark:border-indigo-800"
              >
                {topic}
              </span>
            ))}
          </div>
        </div>
      )}

      {/* Body — rendered as Markdown */}
      {body && (
        <div className="p-4 border-b border-slate-200 dark:border-slate-700">
          <h4 className="text-xs font-semibold text-slate-400 dark:text-slate-500 uppercase tracking-wide mb-2">
            {t("Content")}
          </h4>
          <div className="prose-node">
            <ReactMarkdown
              remarkPlugins={[remarkGfm]}
              components={PROSE_COMPONENTS}
            >
              {body}
            </ReactMarkdown>
          </div>
        </div>
      )}

      {/* Numbered items — collapsible */}
      {detail.numbered_items && detail.numbered_items.length > 0 && (
        <div className="p-4 border-b border-slate-200 dark:border-slate-700">
          <h4 className="text-xs font-semibold text-slate-400 dark:text-slate-500 uppercase tracking-wide mb-3">
            {t("Numbered Items")}
          </h4>
          <div className="space-y-1.5">
            {detail.numbered_items.map((item) => {
              const isExpanded = expandedItems.has(item.identifier);
              return (
                <div
                  key={item.identifier}
                  className="bg-slate-50 dark:bg-slate-700/40 rounded-lg overflow-hidden border border-slate-100 dark:border-slate-700"
                >
                  <button
                    onClick={() => toggleItem(item.identifier)}
                    className="w-full flex items-center justify-between p-2.5 text-left hover:bg-slate-100 dark:hover:bg-slate-700 transition-colors"
                  >
                    <div className="flex items-center gap-2 flex-1 min-w-0">
                      <FileText className="w-3.5 h-3.5 text-slate-400 flex-shrink-0" />
                      <span className="text-xs font-medium text-slate-700 dark:text-slate-300 truncate">
                        {item.identifier}
                      </span>
                    </div>
                    {isExpanded ? (
                      <ChevronUp className="w-3.5 h-3.5 text-slate-400 flex-shrink-0" />
                    ) : (
                      <ChevronDown className="w-3.5 h-3.5 text-slate-400 flex-shrink-0" />
                    )}
                  </button>
                  {isExpanded && (
                    <div className="px-3 pb-3 pt-0.5">
                      <span className="px-1.5 py-0.5 bg-violet-50 dark:bg-violet-900/30 text-violet-600 dark:text-violet-400 rounded text-xs mb-2 inline-block">
                        {item.type}
                      </span>
                      <p className="text-xs text-slate-600 dark:text-slate-400 leading-relaxed whitespace-pre-wrap">
                        {item.text}
                      </p>
                    </div>
                  )}
                </div>
              );
            })}
          </div>
        </div>
      )}

      {/* Related chapters */}
      {detail.related_chapters && detail.related_chapters.length > 0 && (
        <div className="p-4">
          <h4 className="text-xs font-semibold text-slate-400 dark:text-slate-500 uppercase tracking-wide mb-3">
            {t("Related Chapters")}
          </h4>
          <div className="space-y-1.5">
            {detail.related_chapters.map((ch) => (
              <button
                key={ch.id}
                onClick={() => onRelatedNodeSelect?.(ch.id)}
                className="w-full text-left px-3 py-2 bg-indigo-50 dark:bg-indigo-900/20 text-indigo-700 dark:text-indigo-400 rounded-lg text-xs hover:bg-indigo-100 dark:hover:bg-indigo-900/40 transition-colors truncate border border-indigo-100 dark:border-indigo-800/50"
              >
                {ch.title}
              </button>
            ))}
          </div>
        </div>
      )}
    </div>
  );
}
