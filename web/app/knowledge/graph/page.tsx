"use client";

import { Suspense, useState, useEffect, useCallback } from "react";
import { useSearchParams, useRouter } from "next/navigation";
import Link from "next/link";
import dynamic from "next/dynamic";
import { apiUrl } from "@/lib/api";
import { useTranslation } from "react-i18next";
import {
  ArrowLeft,
  RefreshCw,
  Database,
  Network,
  Loader2,
  AlertCircle,
  X,
} from "lucide-react";
import NodeDetail from "./components/NodeDetail";
import GraphControls from "./components/GraphControls";

// Cytoscape requires DOM, must disable SSR
const GraphViewer = dynamic(() => import("./components/GraphViewer"), {
  ssr: false,
  loading: () => (
    <div className="flex items-center justify-center h-full">
      <Loader2 className="w-8 h-8 animate-spin" />
    </div>
  ),
});

interface GraphNode {
  id: string;
  title: string;
  level: number;
  item_count: number;
  body_preview: string;
  key_topics: string[];
}

interface GraphEdge {
  source: string;
  target: string;
  type: "contains" | "follows" | "related";
  weight?: number;
}

interface GraphData {
  nodes: GraphNode[];
  edges: GraphEdge[];
  metadata: {
    generated_at: string;
    total_entities?: number;
    total_relations?: number;
  };
}

export default function KnowledgeGraphPageWrapper() {
  return (
    <Suspense fallback={<div className="flex items-center justify-center h-screen">Loading...</div>}>
      <KnowledgeGraphPage />
    </Suspense>
  );
}

// KB selector shown when no kb is specified in URL
function KbSelector() {
  const { t } = useTranslation();
  const router = useRouter();
  const [kbs, setKbs] = useState<Array<{ name: string; statistics: any }>>([]);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    fetch(apiUrl("/api/v1/knowledge/list"))
      .then((r) => r.json())
      .then((data) => {
        const list = Array.isArray(data) ? data : data.knowledge_bases ?? [];
        // Only show KBs that have content_list (can generate graph)
        setKbs(list.filter((kb: any) => kb.statistics?.content_lists > 0));
      })
      .catch(() => {})
      .finally(() => setLoading(false));
  }, []);

  return (
    <div className="h-screen flex flex-col bg-slate-50 dark:bg-slate-900">
      <div className="h-14 border-b border-slate-200 dark:border-slate-700 flex items-center px-4 gap-3 bg-white dark:bg-slate-800">
        <Link href="/knowledge">
          <div className="p-1 hover:bg-slate-100 dark:hover:bg-slate-700 rounded-md transition-colors">
            <ArrowLeft className="w-5 h-5 text-slate-600 dark:text-slate-300" />
          </div>
        </Link>
        <h1 className="font-semibold text-slate-800 dark:text-slate-200">
          {t("Knowledge Graph")}
        </h1>
      </div>
      <div className="flex-1 flex items-center justify-center p-8">
        <div className="max-w-lg w-full">
          <div className="text-center mb-8">
            <Network className="w-16 h-16 mx-auto mb-4 text-indigo-400" />
            <h2 className="text-xl font-semibold text-slate-800 dark:text-slate-200 mb-2">
              {t("Knowledge Graph")}
            </h2>
            <p className="text-sm text-slate-500 dark:text-slate-400">
              选择一个知识库来查看章节知识图谱
            </p>
          </div>
          {loading ? (
            <div className="flex justify-center">
              <Loader2 className="w-8 h-8 animate-spin text-blue-500" />
            </div>
          ) : kbs.length === 0 ? (
            <div className="text-center text-slate-500 dark:text-slate-400">
              <Database className="w-10 h-10 mx-auto mb-3 text-slate-300" />
              <p className="text-sm">暂无可用的知识库</p>
              <p className="text-xs mt-1">请先在知识库页面上传文档并完成处理</p>
            </div>
          ) : (
            <div className="space-y-3">
              {kbs.map((kb) => (
                <button
                  key={kb.name}
                  onClick={() => router.push(`/knowledge/graph?kb=${encodeURIComponent(kb.name)}`)}
                  className="w-full flex items-center gap-4 p-4 bg-white dark:bg-slate-800 border border-slate-200 dark:border-slate-700 rounded-xl hover:border-indigo-400 dark:hover:border-indigo-500 hover:shadow-md transition-all text-left"
                >
                  <div className="w-10 h-10 bg-indigo-50 dark:bg-indigo-900/30 rounded-lg flex items-center justify-center shrink-0">
                    <Database className="w-5 h-5 text-indigo-500" />
                  </div>
                  <div className="flex-1 min-w-0">
                    <p className="font-medium text-slate-800 dark:text-slate-200 truncate">
                      {kb.name}
                    </p>
                    <p className="text-xs text-slate-500 dark:text-slate-400">
                      {kb.statistics?.documents ?? 0} 文档 · {kb.statistics?.content_lists ?? 0} 内容列表
                    </p>
                  </div>
                  <Network className="w-5 h-5 text-indigo-400 shrink-0" />
                </button>
              ))}
            </div>
          )}
        </div>
      </div>
    </div>
  );
}

function KnowledgeGraphPage() {
  const searchParams = useSearchParams();
  const kbName = searchParams.get("kb") || "";
  const router = useRouter();
  const { t } = useTranslation();

  const [graphData, setGraphData] = useState<GraphData | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [selectedNodeId, setSelectedNodeId] = useState<string | null>(null);
  const [levelFilter, setLevelFilter] = useState<number>(1); // default: chapters only
  const [layout, setLayout] = useState<string>("cose");
  const [regenerating, setRegenerating] = useState(false);

  const fetchGraph = useCallback(
    async (regenerate = false) => {
      setLoading(true);
      setError(null);
      if (regenerate) setRegenerating(true);

      try {
        const res = await fetch(
          apiUrl(`/api/v1/knowledge/${kbName}/chapter-graph?regenerate=${regenerate}`)
        );
        if (!res.ok) {
          const errorText = await res.text();
          throw new Error(errorText || "Failed to load graph data");
        }
        const data: GraphData = await res.json();
        setGraphData(data);
      } catch (e) {
        setError(e instanceof Error ? e.message : String(e));
      } finally {
        setLoading(false);
        setRegenerating(false);
      }
    },
    [kbName]
  );

  useEffect(() => {
    if (kbName) {
      fetchGraph();
    }
  }, [kbName, fetchGraph]);

  // If no KB specified, show selector
  if (!kbName) {
    return <KbSelector />;
  }

  // Filter nodes and edges by level
  const filteredData = graphData
    ? {
        ...graphData,
        nodes:
          levelFilter > 0
            ? graphData.nodes.filter((n) => n.level <= levelFilter)
            : graphData.nodes,
        edges:
          levelFilter > 0
            ? graphData.edges.filter((e) => {
                const nodeIds = new Set(
                  graphData.nodes.filter((n) => n.level <= levelFilter).map((n) => n.id)
                );
                return nodeIds.has(e.source) && nodeIds.has(e.target);
              })
            : graphData.edges,
      }
    : null;

  const maxLevel = graphData
    ? Math.max(...graphData.nodes.map((n) => n.level), 1)
    : 1;

  const handleRegenerate = () => {
    if (confirm(t("This will regenerate the chapter graph. Continue?"))) {
      fetchGraph(true);
    }
  };

  return (
    <div className="h-screen flex flex-col bg-slate-50 dark:bg-slate-900">
      {/* Top bar */}
      <div className="h-14 border-b border-slate-200 dark:border-slate-700 flex items-center px-4 gap-3 bg-white dark:bg-slate-800">
        {kbName ? (
          <Link href="/knowledge">
            <div className="p-1 hover:bg-slate-100 dark:hover:bg-slate-700 rounded-md transition-colors">
              <ArrowLeft className="w-5 h-5 text-slate-600 dark:text-slate-300" />
            </div>
          </Link>
        ) : (
          <div className="p-1">
            <ArrowLeft className="w-5 h-5 text-slate-600 dark:text-slate-300" />
          </div>
        )}
        <h1 className="font-semibold text-slate-800 dark:text-slate-200 flex-1 truncate">
          {kbName} — {t("Knowledge Graph")}
        </h1>
        <button
          onClick={handleRegenerate}
          disabled={regenerating}
          className="p-2 hover:bg-slate-100 dark:hover:bg-slate-700 rounded-md transition-colors disabled:opacity-50 disabled:cursor-not-allowed"
          title={t("Regenerate")}
        >
          {regenerating ? (
            <Loader2 className="w-4 h-4 animate-spin" />
          ) : (
            <RefreshCw className="w-4 h-4 text-slate-600 dark:text-slate-300" />
          )}
        </button>
        {graphData && (
          <div className="flex items-center gap-4 text-xs text-slate-500 dark:text-slate-400">
            <span>{graphData.nodes.length} {t("nodes")}</span>
            <span>•</span>
            <span>{graphData.edges.length} {t("edges")}</span>
          </div>
        )}
      </div>

      {/* Main content */}
      <div className="flex-1 flex overflow-hidden">
        {/* Left control panel */}
        <GraphControls
          levelFilter={levelFilter}
          onLevelFilterChange={setLevelFilter}
          layout={layout}
          onLayoutChange={setLayout}
          maxLevel={maxLevel}
          loading={loading}
        />

        {/* Center graph canvas */}
        <div className="flex-1 relative bg-slate-50 dark:bg-slate-900">
          {loading ? (
            <div className="absolute inset-0 flex items-center justify-center">
              <div className="text-center">
                <Loader2 className="w-12 h-12 animate-spin mx-auto mb-4 text-blue-500" />
                <p className="text-sm text-slate-600 dark:text-slate-400">
                  {t("Loading graph...")}
                </p>
              </div>
            </div>
          ) : error ? (
            <div className="absolute inset-0 flex items-center justify-center p-8">
              <div className="text-center max-w-md">
                <AlertCircle className="w-12 h-12 mx-auto mb-4 text-red-500" />
                <p className="text-lg font-semibold text-red-600 dark:text-red-400 mb-2">
                  {t("Error")}
                </p>
                <p className="text-sm text-slate-600 dark:text-slate-400 mb-4">
                  {error}
                </p>
                {kbName && (
                  <button
                    onClick={() => fetchGraph()}
                    className="px-4 py-2 bg-blue-500 text-white rounded-md hover:bg-blue-600 transition-colors"
                  >
                    {t("Retry")}
                  </button>
                )}
              </div>
            </div>
          ) : graphData && graphData.nodes.length === 0 ? (
            <div className="absolute inset-0 flex items-center justify-center p-8">
              <div className="text-center max-w-md">
                <AlertCircle className="w-12 h-12 mx-auto mb-4 text-slate-400" />
                <p className="text-lg font-semibold text-slate-700 dark:text-slate-300 mb-2">
                  {t("No chapters found")}
                </p>
                <p className="text-sm text-slate-600 dark:text-slate-400">
                  {t(
                    "The knowledge base has no headings. Make sure documents contain chapter/section headings."
                  )}
                </p>
              </div>
            </div>
          ) : filteredData ? (
            <GraphViewer
              data={filteredData}
              layout={layout}
              selectedNodeId={selectedNodeId}
              onNodeSelect={setSelectedNodeId}
            />
          ) : null}
        </div>

        {/* Right detail panel */}
        {selectedNodeId && (
          <div className="w-80 md:w-96 border-l border-slate-200 dark:border-slate-700 bg-white dark:bg-slate-800">
            <div className="flex items-center justify-between p-3 border-b border-slate-200 dark:border-slate-700">
              <h2 className="font-semibold text-sm text-slate-800 dark:text-slate-200">
                {t("Node Detail")}
              </h2>
              <button
                onClick={() => setSelectedNodeId(null)}
                className="p-1 hover:bg-slate-100 dark:hover:bg-slate-700 rounded-md transition-colors"
              >
                <X className="w-4 h-4 text-slate-600 dark:text-slate-300" />
              </button>
            </div>
            <NodeDetail kbName={kbName} nodeId={selectedNodeId} />
          </div>
        )}
      </div>
    </div>
  );
}
