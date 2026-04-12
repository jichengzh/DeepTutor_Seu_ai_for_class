"use client";

import { useEffect, useRef, useState, useCallback } from "react";
import cytoscape, { type Core } from "cytoscape";

// ── Color palette ────────────────────────────────────────────────────────────

const LEVEL_COLORS: Record<number, { bg: string; border: string; text: string }> = {
  1: { bg: "#3b82f6", border: "#2563eb", text: "#ffffff" },   // blue  — chapters
  2: { bg: "#10b981", border: "#059669", text: "#ffffff" },   // green — sections
  3: { bg: "#f59e0b", border: "#d97706", text: "#ffffff" },   // amber — sub-sections
  4: { bg: "#ef4444", border: "#dc2626", text: "#ffffff" },   // red
  5: { bg: "#8b5cf6", border: "#7c3aed", text: "#ffffff" },   // purple
};

const DEFAULT_COLOR = { bg: "#94a3b8", border: "#64748b", text: "#ffffff" };

function colorFor(level: number) {
  return LEVEL_COLORS[level] || DEFAULT_COLOR;
}

// ── Node size by level (chapters are largest) ────────────────────────────────

function nodeSize(level: number, itemCount: number): number {
  const base = level === 1 ? 70 : level === 2 ? 50 : 38;
  return Math.max(base, Math.min(base + 30, base + itemCount * 0.15));
}

// ── Types ────────────────────────────────────────────────────────────────────

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
  type: string;
  weight?: number;
}

interface GraphData {
  nodes: GraphNode[];
  edges: GraphEdge[];
}

interface Props {
  data: GraphData;
  layout: string;
  selectedNodeId: string | null;
  onNodeSelect: (id: string | null) => void;
}

// ── Component ────────────────────────────────────────────────────────────────

export default function GraphViewer({
  data,
  layout,
  selectedNodeId,
  onNodeSelect,
}: Props) {
  const containerRef = useRef<HTMLDivElement>(null);
  const cyRef = useRef<Core | null>(null);
  const [hoverNodeId, setHoverNodeId] = useState<string | null>(null);
  const [ready, setReady] = useState(false);

  // Wait for container to have non-zero dimensions before init
  useEffect(() => {
    const el = containerRef.current;
    if (!el) return;

    const check = () => {
      if (el.offsetWidth > 0 && el.offsetHeight > 0) {
        setReady(true);
      } else {
        requestAnimationFrame(check);
      }
    };
    check();
  }, []);

  // Build Cytoscape instance
  useEffect(() => {
    if (!ready || !containerRef.current || !data || data.nodes.length === 0) return;

    // Destroy previous instance
    if (cyRef.current) {
      try { cyRef.current.destroy(); } catch { /* ignore */ }
      cyRef.current = null;
    }

    // Filter edges: keep only those whose both endpoints exist
    const nodeIds = new Set(data.nodes.map((n) => n.id));
    const validEdges = data.edges.filter(
      (e) => nodeIds.has(e.source) && nodeIds.has(e.target)
    );

    const elements = [
      ...data.nodes.map((node) => ({
        data: {
          id: node.id,
          label: node.level === 1 ? node.title : node.title.replace(/^\d+\.\d+(\.\d+)?\s*/, ""),
          fullLabel: node.title,
          level: node.level,
          itemCount: node.item_count,
          bodyPreview: node.body_preview,
          keyTopics: node.key_topics,
        },
      })),
      ...validEdges.map((edge, i) => ({
        data: {
          id: `e${i}`,
          source: edge.source,
          target: edge.target,
          edgeType: edge.type,
          weight: edge.weight || 1,
        },
      })),
    ];

    let cy: Core;
    try {
      cy = cytoscape({
        container: containerRef.current,
        elements,

        style: [
          // ── Node base style ──
          {
            selector: "node",
            style: {
              label: "data(label)",
              "text-wrap": "wrap",
              "text-max-width": "100px",
              "font-size": (ele: any) => (ele.data("level") === 1 ? "13px" : "10px"),
              "font-weight": (ele: any) => (ele.data("level") === 1 ? 700 : 500),
              "text-valign": "center",
              "text-halign": "center",
              "background-color": (ele: any) => colorFor(ele.data("level")).bg,
              "background-opacity": 0.92,
              width: (ele: any) => nodeSize(ele.data("level"), ele.data("itemCount")),
              height: (ele: any) => nodeSize(ele.data("level"), ele.data("itemCount")),
              "border-width": (ele: any) => (ele.data("level") === 1 ? 3 : 2),
              "border-color": (ele: any) => colorFor(ele.data("level")).border,
              "border-opacity": 0.8,
              color: "#ffffff",
              "text-outline-color": (ele: any) => colorFor(ele.data("level")).border,
              "text-outline-width": 1.5,
              "text-outline-opacity": 0.6,
              shape: (ele: any) => (ele.data("level") === 1 ? "round-rectangle" : "ellipse"),
              "overlay-opacity": 0,
              "transition-property": "border-width, border-color, background-color",
              "transition-duration": 200,
            } as any,
          },
          // ── Hover ──
          {
            selector: "node:active",
            style: {
              "overlay-opacity": 0.08,
              "overlay-color": "#3b82f6",
            },
          },
          // ── Selected ──
          {
            selector: "node:selected",
            style: {
              "border-color": "#f97316",
              "border-width": 5,
              "background-opacity": 1,
              "text-outline-color": "#c2410c",
              "text-outline-width": 2,
            } as any,
          },

          // ── Edge: contains (parent→child) ──
          {
            selector: "edge[edgeType='contains']",
            style: {
              "line-color": "#64748b",
              "target-arrow-color": "#64748b",
              "target-arrow-shape": "triangle",
              "arrow-scale": 1,
              width: 2.5,
              opacity: 0.8,
              "curve-style": "bezier",
            },
          },
          // ── Edge: follows (sequential) ──
          {
            selector: "edge[edgeType='follows']",
            style: {
              "line-color": "#475569",
              "line-style": "dashed",
              "target-arrow-color": "#475569",
              "target-arrow-shape": "vee",
              "arrow-scale": 0.8,
              width: 2,
              opacity: 0.7,
              "curve-style": "bezier",
            },
          },
          // ── Edge: related (cross-chapter) ──
          {
            selector: "edge[edgeType='related']",
            style: {
              "line-color": "#7c3aed",
              "target-arrow-shape": "none",
              opacity: 0.6,
              width: (ele: any) => Math.min(5, Math.max(1.5, ele.data("weight") * 0.8)),
              "curve-style": "unbundled-bezier",
              "control-point-distances": [40],
              "control-point-weights": [0.5],
              "line-style": "dotted",
            } as any,
          },
        ],

        // ── Layout ──
        layout: {
          name: layout,
          animate: true,
          animationDuration: 600,
          padding: 50,
          fit: true,
          ...(layout === "cose"
            ? {
                nodeRepulsion: () => 6000,
                idealEdgeLength: () => 120,
                nodeOverlap: 30,
                gravity: 0.3,
                numIter: 1000,
                edgeElasticity: () => 100,
              }
            : {}),
          ...(layout === "circle"
            ? { radius: undefined, startAngle: 0, clockwise: true }
            : {}),
          ...(layout === "breadthfirst"
            ? {
                directed: true,
                spacingFactor: 1.4,
                roots: data.nodes.filter((n) => n.level === 1).map((n) => n.id),
              }
            : {}),
          ...(layout === "grid"
            ? { rows: Math.ceil(Math.sqrt(data.nodes.length)), condense: true }
            : {}),
        } as any,

        minZoom: 0.15,
        maxZoom: 4,
        wheelSensitivity: 0.25,
        boxSelectionEnabled: false,
      });
    } catch (e) {
      console.warn("Cytoscape init error:", e);
      return;
    }

    // Events
    cy.on("tap", "node", (evt: any) => onNodeSelect(evt.target.id()));
    cy.on("tap", (evt: any) => { if (evt.target === cy) onNodeSelect(null); });
    cy.on("mouseover", "node", (evt: any) => setHoverNodeId(evt.target.id()));
    cy.on("mouseout", "node", () => setHoverNodeId(null));

    cyRef.current = cy;

    return () => {
      try { cy.destroy(); } catch { /* ignore */ }
      cyRef.current = null;
    };
  }, [data, layout, ready]);

  // Selected node highlighting
  useEffect(() => {
    const cy = cyRef.current;
    if (!cy) return;
    try {
      cy.nodes().unselect();
      if (selectedNodeId) {
        const node = cy.getElementById(selectedNodeId);
        if (node.length > 0) {
          node.select();
          cy.animate({ center: { eles: node }, zoom: 1.5 }, { duration: 400 });
        }
      }
    } catch { /* cy destroyed */ }
  }, [selectedNodeId]);

  // Tooltip
  const tooltipNode = hoverNodeId
    ? data.nodes.find((n) => n.id === hoverNodeId)
    : null;

  return (
    <>
      <div
        ref={containerRef}
        className="w-full h-full"
        style={{ background: "radial-gradient(circle at 50% 50%, #f8fafc 0%, #e2e8f0 100%)" }}
      />

      {/* Legend */}
      <div className="absolute bottom-4 left-4 bg-white/90 dark:bg-slate-800/90 backdrop-blur-sm border border-slate-200 dark:border-slate-700 rounded-lg px-3 py-2 flex gap-4 text-xs shadow-sm">
        {[1, 2, 3].map((level) => (
          <div key={level} className="flex items-center gap-1.5">
            <span
              className="inline-block w-3 h-3 rounded-sm"
              style={{ backgroundColor: colorFor(level).bg }}
            />
            <span className="text-slate-600 dark:text-slate-300">
              {level === 1 ? "章" : level === 2 ? "节" : "小节"}
            </span>
          </div>
        ))}
      </div>

      {/* Tooltip */}
      {tooltipNode && (
        <div
          className="absolute z-20 bg-white dark:bg-slate-800 border border-slate-200 dark:border-slate-600 rounded-xl shadow-xl p-4 max-w-xs pointer-events-none"
          style={{ top: 16, right: 16 }}
        >
          <div className="flex items-center gap-2 mb-2">
            <span
              className="inline-block w-2.5 h-2.5 rounded-full shrink-0"
              style={{ backgroundColor: colorFor(tooltipNode.level).bg }}
            />
            <h3 className="font-semibold text-sm text-slate-800 dark:text-slate-200 leading-tight">
              {tooltipNode.title}
            </h3>
          </div>
          {tooltipNode.key_topics.length > 0 && (
            <div className="flex flex-wrap gap-1 mb-2">
              {tooltipNode.key_topics.slice(0, 5).map((topic, i) => (
                <span
                  key={i}
                  className="px-1.5 py-0.5 bg-blue-50 dark:bg-blue-900/30 text-blue-600 dark:text-blue-400 rounded text-xs"
                >
                  {topic}
                </span>
              ))}
            </div>
          )}
          {tooltipNode.body_preview && (
            <p className="text-xs text-slate-500 dark:text-slate-400 line-clamp-3 leading-relaxed">
              {tooltipNode.body_preview}
            </p>
          )}
        </div>
      )}
    </>
  );
}
