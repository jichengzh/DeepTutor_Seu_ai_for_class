"use client";

import { useEffect, useRef, useState, useCallback, useMemo } from "react";
import { useTranslation } from "react-i18next";
import cytoscape, { type Core } from "cytoscape";

// ── Book-color palette ───────────────────────────────────────────────────────
// Each book (source_file) gets a unique hue.
// L1 (chapter) nodes: deep/saturated shade of that hue  — round-rectangle
// L2 (section) nodes: lighter tint of the same hue      — ellipse
//
// Colors are designed to look good on the dark (#1a1a2e) background.

const BOOK_PALETTE: Array<{ l1bg: string; l1border: string; l2bg: string; l2border: string; label: string }> = [
  { l1bg: "#7c3aed", l1border: "#6d28d9", l2bg: "#a78bfa", l2border: "#7c3aed", label: "#c4b5fd" }, // violet
  { l1bg: "#0891b2", l1border: "#0e7490", l2bg: "#22d3ee", l2border: "#0891b2", label: "#67e8f9" }, // cyan
  { l1bg: "#b45309", l1border: "#92400e", l2bg: "#fbbf24", l2border: "#b45309", label: "#fde68a" }, // amber
  { l1bg: "#15803d", l1border: "#166534", l2bg: "#4ade80", l2border: "#15803d", label: "#86efac" }, // green
  { l1bg: "#be123c", l1border: "#9f1239", l2bg: "#fb7185", l2border: "#be123c", label: "#fda4af" }, // rose
  { l1bg: "#0369a1", l1border: "#075985", l2bg: "#38bdf8", l2border: "#0369a1", label: "#7dd3fc" }, // sky
  { l1bg: "#7e22ce", l1border: "#6b21a8", l2bg: "#c084fc", l2border: "#7e22ce", label: "#e9d5ff" }, // purple
  { l1bg: "#b91c1c", l1border: "#991b1b", l2bg: "#f87171", l2border: "#b91c1c", label: "#fca5a5" }, // red
  { l1bg: "#0f766e", l1border: "#115e59", l2bg: "#2dd4bf", l2border: "#0f766e", label: "#99f6e4" }, // teal
  { l1bg: "#a16207", l1border: "#854d0e", l2bg: "#facc15", l2border: "#a16207", label: "#fef08a" }, // yellow
];

const FALLBACK_COLORS = { l1bg: "#64748b", l1border: "#475569", l2bg: "#94a3b8", l2border: "#64748b", label: "#cbd5e1" };

/** Assign a palette index to each unique source_file, deterministically. */
function buildBookColorMap(nodes: GraphNode[]): Map<string, number> {
  const books = Array.from(new Set(nodes.map((n) => n.source_file).filter(Boolean)));
  const map = new Map<string, number>();
  books.forEach((b, i) => map.set(b, i % BOOK_PALETTE.length));
  return map;
}

function styleFor(level: number, sourceFile: string, bookColorMap: Map<string, number>) {
  const idx = bookColorMap.get(sourceFile);
  const palette = idx !== undefined ? BOOK_PALETTE[idx] : FALLBACK_COLORS;
  return {
    bg: level === 1 ? palette.l1bg : palette.l2bg,
    border: level === 1 ? palette.l1border : palette.l2border,
    labelColor: palette.label,
    textSize: level === 1 ? "14px" : "11px",
    fontWeight: level === 1 ? 700 : 500,
    shape: (level === 1 ? "round-rectangle" : "ellipse") as "round-rectangle" | "ellipse",
  };
}

function nodeSize(level: number, itemCount: number): number {
  const base = level === 1 ? 80 : 48;
  return base + Math.min(24, itemCount * 0.2);
}

// ── Types ────────────────────────────────────────────────────────────────────

interface GraphNode {
  id: string;
  title: string;
  level: number;
  item_count: number;
  body_preview: string;
  key_topics: string[];
  source_file: string;
}

interface GraphEdge {
  source: string;
  target: string;
  type: string;
  weight?: number;
}

export interface GraphData {
  nodes: GraphNode[];
  edges: GraphEdge[];
}

interface Props {
  /** Full dataset — filtering is handled internally so positions are preserved */
  data: GraphData;
  /** 1 = chapters only, 2 = chapters + sections (capped at 200 total) */
  levelFilter: number;
  selectedNodeId: string | null;
  onNodeSelect: (id: string | null) => void;
  /** Exposed so parent can read the currently-visible node count */
  onVisibleCountChange?: (count: number) => void;
}

// ── Node cap ─────────────────────────────────────────────────────────────────
const MAX_NODES = 100;

/**
 * Returns the subset of nodes to display given the level filter.
 * Always includes all L1 nodes. Then fills up to MAX_NODES with L2 nodes
 * sorted by item_count descending (more content = more important).
 */
function selectVisibleNodes(nodes: GraphNode[], levelFilter: number): GraphNode[] {
  const l1 = nodes.filter((n) => n.level === 1);
  if (levelFilter <= 1) return l1;

  const l2 = nodes
    .filter((n) => n.level === 2)
    .sort((a, b) => b.item_count - a.item_count);

  const budget = Math.max(0, MAX_NODES - l1.length);
  return [...l1, ...l2.slice(0, budget)];
}

// ── Connectivity guarantee ───────────────────────────────────────────────────
// BFS over visible nodes; for each disconnected component, add one synthetic
// "bridge" edge to the main (largest) component. Bridge edges render faintly.

function ensureConnected(nodes: GraphNode[], edges: GraphEdge[]): GraphEdge[] {
  if (nodes.length < 2) return edges;

  const idSet = new Set(nodes.map((n) => n.id));
  const adj = new Map<string, Set<string>>();
  for (const n of nodes) adj.set(n.id, new Set());
  for (const e of edges) {
    if (idSet.has(e.source) && idSet.has(e.target)) {
      adj.get(e.source)!.add(e.target);
      adj.get(e.target)!.add(e.source);
    }
  }

  const visited = new Set<string>();
  const components: string[][] = [];
  for (const n of nodes) {
    if (visited.has(n.id)) continue;
    const comp: string[] = [];
    const queue = [n.id];
    visited.add(n.id);
    while (queue.length) {
      const cur = queue.shift()!;
      comp.push(cur);
      for (const nb of adj.get(cur)!) {
        if (!visited.has(nb)) { visited.add(nb); queue.push(nb); }
      }
    }
    components.push(comp);
  }

  if (components.length <= 1) return edges;

  const nodesById = new Map(nodes.map((n) => [n.id, n]));
  const mainComp = components.reduce((a, b) => (a.length >= b.length ? a : b));
  const mainHub =
    mainComp.find((id) => nodesById.get(id)?.level === 1) || mainComp[0];

  const bridges: GraphEdge[] = [];
  for (const comp of components) {
    if (comp.includes(mainHub)) continue;
    const anchor = comp.find((id) => nodesById.get(id)?.level === 1) || comp[0];
    bridges.push({ source: anchor, target: mainHub, type: "bridge" });
  }
  return [...edges, ...bridges];
}

// ── Boilerplate stripper ─────────────────────────────────────────────────────
// Removes recurring publisher/translation noise that appears in many nodes.
// Add more patterns here as needed without touching anything else.
const BOILERPLATE_PATTERNS = [
  // O'Reilly AI-translation notice (Chinese)
  /\n*本作品已使用人工智能进行翻译[^]*$/,
  // Generic "translation feedback" lines
  /\n*欢迎您提供反馈和意见[^]*$/,
  // "translation-feedback@..." lines
  /\n*translation-feedback@\S+[^]*$/,
];

function stripBoilerplate(text: string): string {
  let out = text;
  for (const re of BOILERPLATE_PATTERNS) out = out.replace(re, "");
  return out.trim();
}

// ── Build cytoscape element descriptor for a node ───────────────────────────

function nodeElement(node: GraphNode, bookColorMap: Map<string, number>) {
  const s = styleFor(node.level, node.source_file, bookColorMap);
  // Backend now sends clean titles; stripBoilerplate is a safety net for stale cache
  const label = stripBoilerplate(node.title) || node.title;
  return {
    data: {
      id: node.id,
      label,
      fullTitle: node.title,
      level: node.level,
      itemCount: node.item_count,
      bodyPreview: node.body_preview,
      keyTopics: node.key_topics,
      sourceFile: node.source_file,
      size: nodeSize(node.level, node.item_count),
      bgColor: s.bg,
      borderColor: s.border,
      labelColor: s.labelColor,
      textSize: s.textSize,
      fontWeight: s.fontWeight,
      shape: s.shape,
    },
  };
}

// ── Cytoscape stylesheet (defined once, outside component) ───────────────────

const CY_STYLE: cytoscape.Stylesheet[] = [
  {
    selector: "node",
    style: {
      label: "data(label)",
      "text-wrap": "wrap",
      "text-max-width": "110px",
      "font-size": "data(textSize)" as any,
      "font-weight": "data(fontWeight)" as any,
      "font-family": "Inter, system-ui, sans-serif",
      "text-valign": "center",
      "text-halign": "center",
      "background-color": "data(bgColor)",
      "background-opacity": 0.95,
      width: "data(size)",
      height: "data(size)",
      "border-width": (ele: any) => (ele.data("level") === 1 ? 3 : 2),
      "border-color": "data(borderColor)",
      "border-opacity": 0.9,
      color: "data(labelColor)",
      "text-outline-color": "data(borderColor)",
      "text-outline-width": 1,
      "text-outline-opacity": 0.6,
      shape: "data(shape)",
      "overlay-opacity": 0,
      "transition-property": "border-width, border-color, width, height, background-opacity",
      "transition-duration": 200,
    } as any,
  },
  {
    selector: "node.hovered",
    style: { "border-width": 4, "background-opacity": 1, "z-index": 10 } as any,
  },
  {
    selector: "node:selected",
    style: {
      "border-color": "#f97316",
      "border-width": 5,
      "background-opacity": 1,
      "text-outline-color": "#c2410c",
      "text-outline-width": 2,
      "z-index": 20,
    } as any,
  },
  {
    selector: "edge[edgeType='contains']",
    style: {
      "line-color": "#94a3b8",
      "target-arrow-color": "#94a3b8",
      "target-arrow-shape": "triangle",
      "arrow-scale": 0.9,
      width: 1.5,
      opacity: 0.6,
      "curve-style": "bezier",
    },
  },
  {
    selector: "edge[edgeType='follows']",
    style: {
      "line-color": "#cbd5e1",
      "line-style": "dashed",
      "line-dash-pattern": [6, 4],
      "target-arrow-color": "#cbd5e1",
      "target-arrow-shape": "vee",
      "arrow-scale": 0.7,
      width: 1,
      opacity: 0.45,
      "curve-style": "bezier",
    },
  },
  {
    selector: "edge[edgeType='related']",
    style: {
      "line-color": "#a78bfa",
      "target-arrow-shape": "none",
      opacity: 0.5,
      width: (ele: any) => Math.min(4, Math.max(1, ele.data("weight") * 0.7)),
      "curve-style": "unbundled-bezier",
      "control-point-distances": [50],
      "control-point-weights": [0.5],
      "line-style": "solid",
    } as any,
  },
  {
    selector: "edge[edgeType='bridge']",
    style: {
      "line-color": "#e2e8f0",
      "line-style": "dashed",
      "line-dash-pattern": [3, 6],
      "target-arrow-shape": "none",
      width: 1,
      opacity: 0.3,
      "curve-style": "bezier",
    },
  },
];

// ── Component ────────────────────────────────────────────────────────────────

export default function GraphViewer({
  data,
  levelFilter,
  selectedNodeId,
  onNodeSelect,
  onVisibleCountChange,
}: Props) {
  const { t } = useTranslation();
  const containerRef = useRef<HTMLDivElement>(null);
  const cyRef = useRef<Core | null>(null);
  // Persisted positions: nodeId → {x, y}
  const positionCacheRef = useRef<Map<string, { x: number; y: number }>>(new Map());
  const [hoverNodeId, setHoverNodeId] = useState<string | null>(null);
  const [ready, setReady] = useState(false);

  // Book → palette index map, rebuilt when data changes
  const bookColorMap = useMemo(() => buildBookColorMap(data?.nodes ?? []), [data]);
  // Legend: unique books visible in current render
  const bookLegend = useMemo(() => {
    const books = Array.from(new Set((data?.nodes ?? []).map((n) => n.source_file).filter(Boolean)));
    return books.map((b) => {
      const idx = bookColorMap.get(b) ?? 0;
      const palette = BOOK_PALETTE[idx % BOOK_PALETTE.length];
      return { name: b, color: palette.l1bg, textColor: palette.label };
    });
  }, [data, bookColorMap]);

  // Wait for container to have non-zero dimensions before init
  useEffect(() => {
    const el = containerRef.current;
    if (!el) return;
    const check = () => {
      if (el.offsetWidth > 0 && el.offsetHeight > 0) setReady(true);
      else requestAnimationFrame(check);
    };
    check();
  }, []);

  // ── Initial build: runs only when data changes (new KB load) ──────────────
  useEffect(() => {
    if (!ready || !containerRef.current || !data || data.nodes.length === 0) return;

    // Destroy previous instance and clear position cache for new data
    if (cyRef.current) {
      try { cyRef.current.destroy(); } catch { /* ignore */ }
      cyRef.current = null;
    }
    positionCacheRef.current = new Map();

    const visibleNodes = selectVisibleNodes(data.nodes, levelFilter);
    const visibleIds = new Set(visibleNodes.map((n) => n.id));
    const rawEdges = data.edges.filter(
      (e) => visibleIds.has(e.source) && visibleIds.has(e.target)
    );
    const allEdges = ensureConnected(visibleNodes, rawEdges);

    onVisibleCountChange?.(visibleNodes.length);

    const elements = [
      ...visibleNodes.map((n) => nodeElement(n, bookColorMap)),
      ...allEdges.map((edge, i) => ({
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
        style: CY_STYLE,
        layout: {
          name: "cose",
          animate: true,
          animationDuration: 800,
          padding: 60,
          fit: true,
          nodeRepulsion: () => 8000,
          idealEdgeLength: () => 140,
          nodeOverlap: 20,
          gravity: 0.4,
          numIter: 1200,
          edgeElasticity: () => 80,
          nestingFactor: 1.2,
          randomize: true,
          componentSpacing: 80,
          // Save positions after layout finishes
          stop: () => {
            cy.nodes().forEach((n: any) => {
              positionCacheRef.current.set(n.id(), n.position());
            });
          },
        } as any,
        minZoom: 0.1,
        maxZoom: 5,
        wheelSensitivity: 0.2,
        boxSelectionEnabled: false,
      });
    } catch (e) {
      console.warn("Cytoscape init error:", e);
      return;
    }

    // Also save positions on every drag-end so manual moves are remembered
    cy.on("dragfree", "node", (evt: any) => {
      positionCacheRef.current.set(evt.target.id(), evt.target.position());
    });

    cy.on("tap", "node", (evt: any) => onNodeSelect(evt.target.id()));
    cy.on("tap", (evt: any) => { if (evt.target === cy) onNodeSelect(null); });
    cy.on("mouseover", "node", (evt: any) => {
      setHoverNodeId(evt.target.id());
      evt.target.addClass("hovered");
    });
    cy.on("mouseout", "node", (evt: any) => {
      setHoverNodeId(null);
      evt.target.removeClass("hovered");
    });

    cyRef.current = cy;

    return () => {
      // Snapshot positions before destroy
      if (cyRef.current) {
        try {
          cyRef.current.nodes().forEach((n: any) => {
            positionCacheRef.current.set(n.id(), n.position());
          });
          cyRef.current.destroy();
        } catch { /* ignore */ }
        cyRef.current = null;
      }
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [data, ready, bookColorMap]);

  // ── Incremental filter update: add/remove nodes without rebuilding ─────────
  useEffect(() => {
    const cy = cyRef.current;
    if (!cy || !data) return;

    const visibleNodes = selectVisibleNodes(data.nodes, levelFilter);
    const visibleIds = new Set(visibleNodes.map((n) => n.id));

    onVisibleCountChange?.(visibleNodes.length);

    // Nodes currently in the graph
    const currentIds = new Set(cy.nodes().map((n: any) => n.id() as string));

    // Nodes to add
    const toAdd = visibleNodes.filter((n) => !currentIds.has(n.id));
    // Nodes to remove
    const toRemove = [...currentIds].filter((id) => !visibleIds.has(id));

    if (toAdd.length === 0 && toRemove.length === 0) return;

    // Remove outgoing edges first, then nodes
    if (toRemove.length > 0) {
      toRemove.forEach((id) => {
        const node = cy.getElementById(id);
        if (node.length) node.connectedEdges().remove();
        node.remove();
      });
    }

    // Add new nodes, using cached positions when available
    if (toAdd.length > 0) {
      const newElems: any[] = toAdd.map((node) => {
        const el = nodeElement(node, bookColorMap);
        const cached = positionCacheRef.current.get(node.id);
        return cached ? { ...el, position: cached } : el;
      });
      cy.add(newElems);
    }

    // Rebuild edges for the current visible set (remove all, re-add correct ones)
    cy.edges().remove();
    const rawEdges = data.edges.filter(
      (e) => visibleIds.has(e.source) && visibleIds.has(e.target)
    );
    const allEdges = ensureConnected(visibleNodes, rawEdges);
    cy.add(
      allEdges.map((edge, i) => ({
        group: "edges" as const,
        data: {
          id: `ef${i}`,
          source: edge.source,
          target: edge.target,
          edgeType: edge.type,
          weight: edge.weight || 1,
        },
      }))
    );

    // For newly added nodes that have no cached position, run a quick layout
    // only on those nodes (others stay put via `positions` callback)
    const uncachedNewIds = toAdd
      .filter((n) => !positionCacheRef.current.has(n.id))
      .map((n) => n.id);

    if (uncachedNewIds.length > 0) {
      const newNodeCollection = cy.nodes().filter((n: any) =>
        uncachedNewIds.includes(n.id())
      );
      // Position new nodes near their parent (L1) if found, else use cose on all
      newNodeCollection.forEach((n: any) => {
        // Try to place near the chapter node it belongs to
        const parentEdge = cy.edges().filter(
          (e: any) => e.data("target") === n.id() && e.data("edgeType") === "contains"
        );
        if (parentEdge.length > 0) {
          const parentId = parentEdge[0].data("source");
          const parentNode = cy.getElementById(parentId);
          if (parentNode.length > 0) {
            const pp = parentNode.position();
            const angle = Math.random() * 2 * Math.PI;
            const dist = 120 + Math.random() * 60;
            n.position({
              x: pp.x + Math.cos(angle) * dist,
              y: pp.y + Math.sin(angle) * dist,
            });
            positionCacheRef.current.set(n.id(), n.position());
          }
        }
      });
    }
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [levelFilter, bookColorMap]);

  // ── Selected node highlight ───────────────────────────────────────────────
  useEffect(() => {
    const cy = cyRef.current;
    if (!cy) return;
    try {
      cy.nodes().unselect();
      if (selectedNodeId) {
        const node = cy.getElementById(selectedNodeId);
        if (node.length > 0) {
          node.select();
          cy.animate({ center: { eles: node }, zoom: 1.8 }, { duration: 400 });
        }
      }
    } catch { /* cy destroyed */ }
  }, [selectedNodeId]);

  const tooltipNode = hoverNodeId
    ? data.nodes.find((n) => n.id === hoverNodeId)
    : null;

  return (
    <>
      {/* Graph canvas — dark background evokes Obsidian */}
      <div
        ref={containerRef}
        className="w-full h-full"
        style={{
          background: "radial-gradient(ellipse at 50% 40%, #1e1b4b 0%, #0f172a 70%, #020617 100%)",
        }}
      />

      {/* Legend — one entry per book/source_file */}
      {bookLegend.length > 0 && (
        <div className="absolute bottom-4 left-4 bg-slate-900/80 backdrop-blur-sm border border-slate-700/60 rounded-xl px-3 py-2 flex flex-col gap-1.5 text-xs shadow-lg max-w-xs">
          <p className="text-slate-400 font-semibold mb-0.5 tracking-wide uppercase" style={{ fontSize: "10px" }}>
            {t("Source")}
          </p>
          {bookLegend.map(({ name, color, textColor }) => (
            <div key={name} className="flex items-center gap-2 min-w-0">
              {/* L1 chip */}
              <span className="inline-block w-3.5 h-3 rounded-sm shrink-0" style={{ backgroundColor: color }} />
              <span
                className="truncate leading-tight"
                style={{ color: textColor, maxWidth: "180px" }}
                title={name}
              >
                {name}
              </span>
            </div>
          ))}
          <div className="flex items-center gap-2 mt-1 pt-1 border-t border-slate-700/60">
            <span className="inline-block w-6 h-px shrink-0" style={{ backgroundColor: "#94a3b8" }} />
            <span className="text-slate-400">{t("Related")}</span>
          </div>
        </div>
      )}

      {/* Hover tooltip */}
      {tooltipNode && (
        <div
          className="absolute z-20 bg-slate-900/95 border border-slate-600/70 rounded-xl shadow-2xl p-4 max-w-xs pointer-events-none"
          style={{ top: 16, right: selectedNodeId ? 420 : 16 }}
        >
          <div className="flex items-center gap-2 mb-2">
            <span
              className="inline-block w-2.5 h-2.5 rounded-full shrink-0"
              style={{ backgroundColor: styleFor(tooltipNode.level, tooltipNode.source_file, bookColorMap).bg }}
            />
            <h3 className="font-semibold text-sm text-white leading-tight">
              {stripBoilerplate(tooltipNode.title)}
            </h3>
          </div>
          {tooltipNode.key_topics.length > 0 && (
            <div className="flex flex-wrap gap-1 mb-2">
              {tooltipNode.key_topics.slice(0, 6).map((topic, i) => (
                <span
                  key={i}
                  className="px-1.5 py-0.5 bg-indigo-900/60 text-indigo-300 rounded text-xs"
                >
                  {topic}
                </span>
              ))}
            </div>
          )}
          {tooltipNode.body_preview && (
            <p className="text-xs text-slate-400 line-clamp-3 leading-relaxed">
              {stripBoilerplate(tooltipNode.body_preview)}
            </p>
          )}
        </div>
      )}
    </>
  );
}
