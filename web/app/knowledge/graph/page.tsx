"use client";

import { useState, useEffect, useCallback } from "react";
import { useSearchParams } from "next/navigation";
import dynamic from "next/dynamic";
import { apiUrl } from "@/lib/api";
import { useTranslation } from "react-i18next";
import {
  ArrowLeft,
  RefreshCw,
  Loader2,
  AlertCircle,
  X,
} from "lucide-react";
import Link from "next/link";
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

export default function KnowledgeGraphPage() {
  const searchParams = useSearchParams();
  const kbName = searchParams.get("kb") || "";
  const { t } = useTranslation();

  const [graphData, setGraphData] = useState<GraphData | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [selectedNodeId, setSelectedNodeId] = useState<string | null>(null);
  const [levelFilter, setLevelFilter] = useState<number>(0); // 0=all, 1=chapters only, 2=+sections
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
    } else {
      setError("No knowledge base specified");
      setLoading(false);
    }
  }, [kbName, fetchGraph]);

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
