"use client";

import { useTranslation } from "react-i18next";
import { Filter, Network } from "lucide-react";

interface Props {
  levelFilter: number;
  onLevelFilterChange: (level: number) => void;
  loading: boolean;
}

const LEVEL_OPTIONS = [
  { value: 1, labelKey: "Chapters only (L1)" },
  { value: 2, labelKey: "Chapters + Sections (L1-2)" },
];

export default function GraphControls({
  levelFilter,
  onLevelFilterChange,
  loading,
}: Props) {
  const { t } = useTranslation();

  return (
    <div className="w-52 border-r border-slate-200 dark:border-slate-700 bg-white dark:bg-slate-800 flex flex-col">
      {/* Header */}
      <div className="p-3 border-b border-slate-200 dark:border-slate-700">
        <div className="flex items-center gap-2 text-slate-600 dark:text-slate-300">
          <Network className="w-4 h-4" />
          <h2 className="text-xs font-semibold">{t("Graph Controls")}</h2>
        </div>
      </div>

      <div className="flex-1 overflow-y-auto p-3 space-y-5">
        {/* Level filter */}
        <div>
          <h3 className="text-xs font-semibold text-slate-500 dark:text-slate-400 mb-2 flex items-center gap-1.5">
            <Filter className="w-3 h-3" />
            {t("Node Depth")}
          </h3>
          <div className="space-y-1">
            {LEVEL_OPTIONS.map((opt) => (
              <button
                key={opt.value}
                onClick={() => onLevelFilterChange(opt.value)}
                disabled={loading}
                className={`w-full text-left px-3 py-2 rounded-lg text-xs transition-colors disabled:opacity-50 disabled:cursor-not-allowed ${
                  levelFilter === opt.value
                    ? "bg-indigo-500 text-white font-medium"
                    : "bg-slate-100 dark:bg-slate-700 text-slate-700 dark:text-slate-300 hover:bg-slate-200 dark:hover:bg-slate-600"
                }`}
              >
                {t(opt.labelKey)}
              </button>
            ))}
          </div>
        </div>

        {/* Info */}
        <div className="text-xs text-slate-400 dark:text-slate-500 leading-relaxed">
          <p>{t("All nodes are connected. Click a node to view its details.")}</p>
        </div>
      </div>
    </div>
  );
}
