"use client";

import { CheckCircle, MessageSquare, FileText } from "lucide-react";

interface SectionInfo {
  key: string;
  title: string;
  hasRevision: boolean; // whether this section has been revised
  hasChat: boolean;     // whether this section has chat history
}

interface Props {
  sections: SectionInfo[];
  selectedKey: string | null;
  onSelect: (key: string) => void;
}

export default function SectionNav({ sections, selectedKey, onSelect }: Props) {
  return (
    <div className="flex flex-col h-full">
      <div className="px-3 py-2 border-b border-gray-200 dark:border-gray-700">
        <h3 className="text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
          章节导航
        </h3>
      </div>
      <div className="flex-1 overflow-y-auto py-1">
        {sections.map((sec) => {
          const isSelected = sec.key === selectedKey;
          return (
            <button
              key={sec.key}
              onClick={() => onSelect(sec.key)}
              className={`w-full text-left px-3 py-2.5 flex items-center gap-2 text-sm transition-colors ${
                isSelected
                  ? "bg-blue-50 dark:bg-blue-900/30 text-blue-700 dark:text-blue-300 border-r-2 border-blue-500"
                  : "text-gray-700 dark:text-gray-300 hover:bg-gray-50 dark:hover:bg-gray-800"
              }`}
            >
              <FileText className={`w-3.5 h-3.5 shrink-0 ${
                isSelected ? "text-blue-500" : "text-gray-400"
              }`} />
              <span className="flex-1 truncate text-xs">{sec.title}</span>
              {sec.hasRevision && (
                <span title="已修改"><CheckCircle className="w-3.5 h-3.5 text-green-500 shrink-0" /></span>
              )}
              {sec.hasChat && !sec.hasRevision && (
                <span title="有对话"><MessageSquare className="w-3 h-3 text-amber-400 shrink-0" /></span>
              )}
            </button>
          );
        })}
      </div>
    </div>
  );
}
