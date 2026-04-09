"use client";

import { useEffect, useRef, useState } from "react";
import cytoscape, { type Core } from "cytoscape";

// Level colors for nodes
const LEVEL_COLORS: Record<number, string> = {
  1: "#3b82f6", // blue - 一级标题（章）
  2: "#10b981", // green - 二级标题（节）
  3: "#f59e0b", // amber - 三级标题
  4: "#ef4444", // red - 四级标题
  5: "#8b5cf6", // purple - 五级及以上
};

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
}

interface Props {
  data: GraphData;
  layout: string;
  selectedNodeId: string | null;
  onNodeSelect: (id: string | null) => void;
}

export default function GraphViewer({
  data,
  layout,
  selectedNodeId,
  onNodeSelect,
}: Props) {
  const containerRef = useRef<HTMLDivElement>(null);
  const cyRef = useRef<Core | null>(null);
  const [hoverNodeId, setHoverNodeId] = useState<string | null>(null);

  // Initialize Cytoscape
  useEffect(() => {
    if (!containerRef.current || !data) return;

    // Convert to Cytoscape elements format
    const elements = [
      ...data.nodes.map((node) => ({
        data: {
          id: node.id,
          label: node.title,
          level: node.level,
          itemCount: node.item_count,
          bodyPreview: node.body_preview,
          keyTopics: node.key_topics,
        },
      })),
      ...data.edges.map((edge, i) => ({
        data: {
          id: `e${i}`,
          source: edge.source,
          target: edge.target,
          edgeType: edge.type,
          weight: edge.weight || 1,
        },
      })),
    ];

    const cy = cytoscape({
      container: containerRef.current,
      elements,
      style: [
        {
          selector: "node",
          style: {
            label: "data(label)",
            "text-wrap": "wrap",
            "text-max-width": "120px",
            "font-size": "11px",
            "text-valign": "center",
            "text-halign": "center",
            "background-color": (ele: any) =>
              LEVEL_COLORS[ele.data("level")] || "#94a3b8",
            width: (ele: any) =>
              Math.max(40, Math.min(120, 35 + ele.data("itemCount") * 0.3)),
            height: (ele: any) =>
              Math.max(40, Math.min(120, 35 + ele.data("itemCount") * 0.3)),
            "border-width": 2,
            "border-color": "#fff",
            color: "#1e293b",
            "text-outline-color": "#fff",
            "text-outline-width": 2,
            shape: "ellipse",
          },
        },
        {
          selector: "node:selected",
          style: {
            "border-color": "#f97316",
            "border-width": 4,
            "text-outline-color": "#f97316",
            "text-outline-width": 3,
          },
        },
        {
          selector: "node:hover",
          style: {
            "border-color": "#f97316",
            "border-width": 3,
          },
        },
        {
          selector: "edge[edgeType='contains']",
          style: {
            "line-color": "#cbd5e1",
            width: 2,
            "curve-style": "bezier",
            "target-arrow-shape": "triangle",
            "target-arrow-color": "#cbd5e1",
            "arrow-scale": 0.8,
          },
        },
        {
          selector: "edge[edgeType='follows']",
          style: {
            "line-color": "#94a3b8",
            "line-style": "dashed",
            width: 1.5,
            "curve-style": "bezier",
            "target-arrow-shape": "triangle",
            "target-arrow-color": "#94a3b8",
            "arrow-scale": 0.7,
          },
        },
        {
          selector: "edge[edgeType='related']",
          style: {
            "line-color": "#f59e0b",
            opacity: 0.4,
            width: (ele: any) => Math.min(4, Math.max(1, ele.data("weight") * 0.5)),
            "curve-style": "bezier",
          },
        },
      ],
      layout: {
        name: layout,
        animate: true,
        padding: 30,
        fit: true,
        // Layout-specific options
        ...(layout === "cose"
          ? {
              nodeRepulsion: 4000,
              idealEdgeLength: 100,
              nodeOverlap: 20,
            }
          : {}),
        ...(layout === "circle"
          ? {
              radius: undefined,
              startAngle: 0,
              clockwise: true,
            }
          : {}),
        ...(layout === "breadthfirst"
          ? {
              directed: true,
              roots: data.nodes.filter((n) => n.level === 1).map((n) => n.id),
              minimizeDummyLinks: true,
            }
          : {}),
      },
      minZoom: 0.2,
      maxZoom: 3,
      wheelSensitivity: 0.3,
    });

    // Node click event
    cy.on("tap", "node", (evt: any) => {
      const nodeId = evt.target.id();
      onNodeSelect(nodeId);
    });

    // Background click event (deselect)
    cy.on("tap", (evt: any) => {
      if (evt.target === cy) {
        onNodeSelect(null);
      }
    });

    // Hover events for tooltips
    cy.on("mouseover", "node", (evt: any) => {
      const node = evt.target;
      setHoverNodeId(node.id());
    });

    cy.on("mouseout", "node", () => {
      setHoverNodeId(null);
    });

    cyRef.current = cy;

    return () => {
      cy.destroy();
    };
  }, [data, layout]);

  // Handle selected node highlighting
  useEffect(() => {
    const cy = cyRef.current;
    if (!cy) return;

    cy.nodes().unselect();
    if (selectedNodeId) {
      const selectedNode = cy.getElementById(selectedNodeId);
      if (selectedNode.length > 0) {
        selectedNode.select();
        // Animate to center: selected node
        cy.animate({
          center: { eles: selectedNode },
          zoom: 1.2,
        });
      }
    }
  }, [selectedNodeId]);

  // Tooltip content
  const tooltipNode = hoverNodeId
    ? data.nodes.find((n) => n.id === hoverNodeId)
    : null;

  return (
    <>
      <div ref={containerRef} className="w-full h-full" />

      {/* Tooltip */}
      {tooltipNode && (
        <div
          className="absolute z-10 bg-white dark:bg-slate-800 border border-slate-200 dark:border-slate-700 rounded-lg shadow-lg p-3 max-w-xs pointer-events-none"
          style={{
            top: "50%",
            left: "50%",
            transform: "translate(-50%, -50%)",
          }}
        >
          <h3 className="font-semibold text-sm text-slate-800 dark:text-slate-200 mb-1">
            {tooltipNode.title}
          </h3>
          <div className="text-xs text-slate-600 dark:text-slate-400 mb-2">
            {tooltipNode.key_topics.length > 0 && (
              <div className="flex flex-wrap gap-1 mt-1">
                {tooltipNode.key_topics.slice(0, 5).map((topic, i) => (
                  <span
                    key={i}
                    className="px-1.5 py-0.5 bg-blue-50 dark:bg-blue-900/30 text-blue-600 dark:text-blue-400 rounded text-xs"
                  >
                    {topic}
                  </span>
                ))}
                {tooltipNode.key_topics.length > 5 && (
                  <span className="text-xs text-slate-500">
                    +{tooltipNode.key_topics.length - 5}
                  </span>
                )}
              </div>
            )}
          </div>
          <p className="text-xs text-slate-500 dark:text-slate-400 line-clamp-2">
            {tooltipNode.body_preview}
          </p>
        </div>
      )}
    </>
  );
}
