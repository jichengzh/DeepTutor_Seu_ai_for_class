"use client";

import { useState } from "react";
import { useTranslation } from "react-i18next";
import { Layout, Filter, Search } from "lucide-react";

interface Props {
  levelFilter: number;
  onLevelFilterChange: (level: number) => void;
  layout: string;
  onLayoutChange: (layout: string) => void;
  maxLevel: number;
  loading: boolean;
}

const LAYOUT_OPTIONS = [
  { id: "cose", label: "Force Directed", labelKey: "Force Directed" },
  { id: "circle", label: "Circle", labelKey: "Circle" },
  { id: "breadthfirst", label: "Hierarchical", labelKey: "Hierarchical" },
  { id: "grid", label: "Grid", labelKey: "Grid" },
];

export default function GraphControls({
  levelFilter,
  onLevelFilterChange,
  layout,
  onLayoutChange,
  maxLevel,
  loading,
}: Props) {
  const { t } = useTranslation();
  const [searchQuery, setSearchQuery] = useState("");

  const handleLevelFilter = (level: number) => {
    onLevelFilterChange(level === levelFilter ? 0 : level);
  };

  return (
    <div className="w-56 border-r border-slate-200 dark:border-slate-700 bg-white dark:bg-slate-800 flex flex-col">
      {/* Header */}
      <div className="p-3 border-b border-slate-200 dark:border-slate-700">
        <div className="flex items-center gap-2 text-slate-600 dark:text-slate-300">
          <Layout className="w-4 h-4" />
          <h2 className="text-xs font-semibold">{t("Controls")}</h2>
        </div>
      </div>

      {/* Controls */}
      <div className="flex-1 overflow-y-auto p-3 space-y-6">
        {/* Layout selection */}
        <div>
          <h3 className="text-xs font-semibold text-slate-500 dark:text-slate-400 mb-2 flex items-center gap-1.5">
            <Layout className="w-3 h-3" />
            {t("Layout")}
          </h3>
          <div className="space-y-1">
            {LAYOUT_OPTIONS.map((opt) => (
              <button
                key={opt.id}
                onClick={() => onLayoutChange(opt.id)}
                disabled={loading}
                className={`w-full text-left px-3 py-2 rounded-lg text-xs transition-colors disabled:opacity-50 disabled:cursor-not-allowed ${
                  layout === opt.id
                    ? "bg-blue-500 text-white font-medium"
                    : "bg-slate-100 dark:bg-slate-700 text-slate-700 dark:text-slate-300 hover:bg-slate-200 dark:hover:bg-slate-600"
                }`}
              >
                {t(opt.labelKey)}
              </button>
            ))}
          </div>
        </div>

        {/* Level filter */}
        <div>
          <h3 className="text-xs font-semibold text-slate-500 dark:text-slate-400 mb-2 flex items-center gap-1.5">
            <Filter className="w-3 h-3" />
            {t("Level Filter")}
          </h3>
          <div className="space-y-1">
            {/* All levels */}
            <button
              onClick={() => onLevelFilterChange(0)}
              disabled={loading}
              className={`w-full text-left px-3 py-2 rounded-lg text-xs transition-colors disabled:opacity-50 disabled:cursor-not-allowed ${
                levelFilter === 0
                  ? "bg-blue-500 text-white font-medium"
                  : "bg-slate-100 dark:bg-slate-700 text-slate-700 dark:text-slate-300 hover:bg-slate-200 dark:hover:bg-slate-600"
              }`}
            >
              {t("All Levels")}
            </button>

            {/* Level options */}
            {Array.from({ length: Math.min(maxLevel, 3) }, (_, i) => i + 1).map((level) => (
              <button
                key={level}
                onClick={() => handleLevelFilter(level)}
                disabled={loading}
                className={`w-full text-left px-3 py-2 rounded-lg text-xs transition-colors disabled:opacity-50 disabled:cursor-not-allowed ${
                  levelFilter === level
                    ? "bg-blue-500 text-white font-medium"
                    : "bg-slate-100 dark:bg-slate-700 text-slate-700 dark:text-slate-300 hover:bg-slate-200 dark:hover:bg-slate-600"
                }`}
              >
                {level === 1
                  ? t("Chapters Only")
                  : `${t("Up to Level")} ${level}`}
              </button>
            ))}
          </div>
        </div>

        {/* Search */}
        <div>
          <h3 className="text-xs font-semibold text-slate-500 dark:text-slate-400 mb-2 flex items-center gap-1.5">
            <Search className="w-3 h-3" />
            {t("Search")}
          </h3>
          <input
            type="text"
            value={searchQuery}
            onChange={(e) => setSearchQuery(e.target.value)}
            placeholder={t("Search nodes...")}
            disabled={loading}
            className="w-full px-3 py-2 bg-slate-100 dark:bg-slate-700 border border-slate-200 dark:border-slate-600 rounded-lg text-xs text-slate-700 dark:text-slate-300 placeholder-slate-500 dark:placeholder-slate-500 focus:outline-none focus:ring-2 focus:ring-blue-500 disabled:opacity-50 disabled:cursor-not-allowed"
          />
          {searchQuery && (
            <p className="mt-2 text-xs text-slate-500 dark:text-slate-400">
              {t("Search feature coming soon.")}
            </p>
          )}
        </div>
      </div>
    </div>
  );
}
