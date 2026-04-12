"use client";

import { useEffect, useState } from "react";
import { apiUrl } from "@/lib/api";
import { useTranslation } from "react-i18next";
import { Loader2, AlertCircle, FileText, ChevronDown, ChevronUp } from "lucide-react";

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
}

const LEVEL_LABELS: Record<number, string> = {
  1: "Chapter",
  2: "Section",
  3: "Subsection",
  4: "Sub-subsection",
};

export default function NodeDetail({ kbName, nodeId }: Props) {
  const { t } = useTranslation();
  const [detail, setDetail] = useState<NodeDetailData | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [expandedItems, setExpandedItems] = useState<Set<string>>(new Set());

  useEffect(() => {
    setLoading(true);
    setError(null);

    fetch(apiUrl(`/api/v1/knowledge/${kbName}/chapter-graph/${nodeId}`))
      .then((res) => {
        if (!res.ok) {
          throw new Error(`HTTP ${res.status}`);
        }
        return res.json();
      })
      .then((data: NodeDetailData) => {
        setDetail(data);
      })
      .catch((e) => {
        setError(e.message || "Failed to load node detail");
      })
      .finally(() => {
        setLoading(false);
      });
  }, [kbName, nodeId]);

  const toggleItem = (id: string) => {
    setExpandedItems((prev) => {
      const next = new Set(prev);
      if (next.has(id)) {
        next.delete(id);
      } else {
        next.add(id);
      }
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
      <div className="flex items-center justify-center h-48 text-center px-4">
        <AlertCircle className="w-6 h-6 text-red-500 mx-auto mb-2" />
        <p className="text-sm text-red-600 dark:text-red-400">{error}</p>
      </div>
    );
  }

  if (!detail) {
    return null;
  }

  return (
    <div className="flex-1 overflow-y-auto">
      {/* Title and metadata */}
      <div className="p-4 border-b border-slate-200 dark:border-slate-700">
        <h3 className="font-semibold text-base text-slate-800 dark:text-slate-200 mb-2 break-words">
          {detail.title}
        </h3>
        <div className="flex flex-wrap gap-2 mb-3">
          <span className="px-2 py-1 bg-blue-50 dark:bg-blue-900/30 text-blue-600 dark:text-blue-400 rounded text-xs font-medium">
            {LEVEL_LABELS[detail.level] || `Level ${detail.level}`}
          </span>
          <span className="px-2 py-1 bg-slate-100 dark:bg-slate-700 text-slate-600 dark:text-slate-400 rounded text-xs">
            {t("Page")} {detail.page_idx + 1}
          </span>
          <span className="px-2 py-1 bg-slate-100 dark:bg-slate-700 text-slate-600 dark:text-slate-400 rounded text-xs">
            {detail.item_count} {t("items")}
          </span>
        </div>
      </div>

      {/* Body preview/full */}
      <div className="p-4 border-b border-slate-200 dark:border-slate-700">
        <h4 className="text-xs font-semibold text-slate-500 dark:text-slate-400 uppercase mb-2">
          {t("Content")}
        </h4>
        <p className="text-sm text-slate-700 dark:text-slate-300 leading-relaxed whitespace-pre-wrap">
          {detail.full_body || detail.body_preview}
        </p>
      </div>

      {/* Key topics */}
      {detail.key_topics && detail.key_topics.length > 0 && (
        <div className="p-4 border-b border-slate-200 dark:border-slate-700">
          <h4 className="text-xs font-semibold text-slate-500 dark:text-slate-400 uppercase mb-2">
            {t("Key Topics")}
          </h4>
          <div className="flex flex-wrap gap-1.5">
            {detail.key_topics.map((topic, i) => (
              <span
                key={i}
                className="px-2 py-1 bg-emerald-50 dark:bg-emerald-900/20 text-emerald-700 dark:text-emerald-400 rounded-full text-xs"
              >
                {topic}
              </span>
            ))}
          </div>
        </div>
      )}

      {/* Numbered items */}
      {detail.numbered_items && detail.numbered_items.length > 0 && (
        <div className="p-4 border-b border-slate-200 dark:border-slate-700">
          <h4 className="text-xs font-semibold text-slate-500 dark:text-slate-400 uppercase mb-3">
            {t("Numbered Items")}
          </h4>
          <div className="space-y-2">
            {detail.numbered_items.map((item) => {
              const isExpanded = expandedItems.has(item.identifier);
              return (
                <div
                  key={item.identifier}
                  className="bg-slate-50 dark:bg-slate-700/50 rounded-lg overflow-hidden"
                >
                  <button
                    onClick={() => toggleItem(item.identifier)}
                    className="w-full flex items-center justify-between p-2.5 text-left hover:bg-slate-100 dark:hover:bg-slate-700 transition-colors"
                  >
                    <div className="flex items-center gap-2 flex-1 min-w-0">
                      <FileText className="w-3.5 h-3.5 text-slate-500 flex-shrink-0" />
                      <span className="text-xs font-medium text-slate-700 dark:text-slate-300 truncate">
                        {item.identifier}
                      </span>
                    </div>
                    {isExpanded ? (
                      <ChevronUp className="w-3.5 h-3.5 text-slate-500 flex-shrink-0" />
                    ) : (
                      <ChevronDown className="w-3.5 h-3.5 text-slate-500 flex-shrink-0" />
                    )}
                  </button>
                  {isExpanded && (
                    <div className="px-2.5 pb-2.5">
                      <span className="px-1.5 py-0.5 bg-purple-50 dark:bg-purple-900/30 text-purple-600 dark:text-purple-400 rounded text-xs mb-1.5 inline-block">
                        {item.type}
                      </span>
                      <p className="text-xs text-slate-600 dark:text-slate-400 leading-relaxed">
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
          <h4 className="text-xs font-semibold text-slate-500 dark:text-slate-400 uppercase mb-3">
            {t("Related Chapters")}
          </h4>
          <div className="space-y-1.5">
            {detail.related_chapters.map((ch) => (
              <button
                key={ch.id}
                onClick={() => {
                  // Emit event to parent to change selected node
                  // This requires lifting state, for now just show
                }}
                className="w-full text-left px-3 py-2 bg-amber-50 dark:bg-amber-900/20 text-amber-700 dark:text-amber-400 rounded-lg text-xs hover:bg-amber-100 dark:hover:bg-amber-900/40 transition-colors truncate"
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
