#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
Chapter-level Knowledge Graph Generator

Extracts chapter/section-level structure from content_list files and
builds a simplified knowledge graph with entities mapped to chapters.
"""

from datetime import datetime
import json
import re
from pathlib import Path
from typing import Any, Dict, List, Set, Tuple


# ── Heuristic level inference ────────────────────────────────────────────────

# Patterns that indicate a top-level chapter heading (→ level 1)
_RE_CHAPTER = re.compile(
    r"^(第\s*[一二三四五六七八九十百\d]+\s*[章篇部]"   # 第1章, 第二篇
    r"|chapter\s+\d+"                                   # Chapter 1
    r"|part\s+[ivxlcdm\d]+"                             # Part I, Part 2
    r")",
    re.IGNORECASE,
)

# Patterns that indicate a numbered section (→ level 2 or 3)
_RE_SECTION_2 = re.compile(r"^\d+\.\d+\s")         # "1.1 Title"
_RE_SECTION_3 = re.compile(r"^\d+\.\d+\.\d+\s")    # "1.1.1 Title"

# Short generic sub-headings commonly used as level-3 leaves
_SHORT_SUBSECTION_TITLES = frozenset({
    "问题", "解决方案", "解决方式", "讨论", "另请参见", "另请参阅",
    "提示", "警告", "备注", "注意", "示例", "摘要", "总结", "小结",
    "problem", "solution", "discussion", "see also", "tip", "warning",
    "note", "example", "summary",
})


def _infer_heading_level(title: str) -> int:
    """
    Infer a heading level from its text when the parser reports all
    headings as the same level (typically text_level=1).

    Returns 1, 2, or 3.
    """
    t = title.strip()
    t_lower = t.lower()

    if _RE_CHAPTER.match(t):
        return 1
    if _RE_SECTION_3.match(t):
        return 3
    if _RE_SECTION_2.match(t):
        return 2
    if t_lower in _SHORT_SUBSECTION_TITLES:
        return 3
    # Default: level 2 (section under a chapter)
    return 2


def _needs_level_inference(headings: List[Dict[str, Any]]) -> bool:
    """Return True when all headings share the same level (flat structure)."""
    levels = {h["level"] for h in headings}
    return len(levels) <= 1 and len(headings) > 1


# ─────────────────────────────────────────────────────────────────────────────


def extract_chapter_structure(content_list_dir: Path) -> List[Dict[str, Any]]:
    """
    Extract chapter/section structure from content_list JSON files.

    Args:
        content_list_dir: Path to the content_list directory containing
                         per-JSON files for each document.

    Returns:
        List of chapter nodes with content_range for entity mapping.
    """
    chapters = []

    for json_file in sorted(content_list_dir.glob("*.json")):
        doc_name = json_file.stem  # e.g., "textbook"

        try:
            with open(json_file, encoding="utf-8") as f:
                items = json.load(f)
        except (json.JSONDecodeError, IOError) as e:
            continue

        # First pass: find all heading items (text_level > 0)
        headings = []
        for idx, item in enumerate(items):
            if item.get("type") == "text" and item.get("text_level", 0) > 0:
                headings.append({
                    "idx": idx,
                    "title": item["text"].strip(),
                    "level": item["text_level"],
                    "page_idx": item.get("page_idx", 0),
                    "doc_name": doc_name,
                })

        # If parser produced flat levels, apply heuristic inference
        if headings and _needs_level_inference(headings):
            for h in headings:
                h["level"] = _infer_heading_level(h["title"])

        # Fallback: no headings → treat entire document as single node
        if not headings:
            body_texts = [
                item.get("text", "") for item in items
                if item.get("type") == "text"
            ]
            body_preview = " ".join(body_texts)[:200]
            chapters.append({
                "id": f"{doc_name}_doc",
                "title": doc_name[0:50],  # Truncate long filenames
                "level": 1,
                "source_file": doc_name,
                "page_idx": 0,
                "content_range": (0, len(items)),
                "item_count": len(body_texts),
                "body_preview": body_preview,
                "key_topics": [],
            })
            continue

        # Second pass: for each heading, collect its subordinate body content
        for i, heading in enumerate(headings):
            start_idx = heading["idx"]
            # Find end index (next heading with same or higher level)
            end_idx = len(items)
            for j in range(i + 1, len(headings)):
                if headings[j]["level"] <= heading["level"]:
                    end_idx = headings[j]["idx"]
                    break

            # Collect body items under this heading
            body_items = items[start_idx + 1 : end_idx]
            body_texts = [
                item.get("text", "") for item in body_items
                if item.get("type") == "text" and item.get("text_level", 0) == 0
            ]
            body_preview = " ".join(body_texts)[:200]

            node_id = f"{doc_name}_h{i}"
            chapters.append({
                "id": node_id,
                "title": heading["title"][:100],  # Truncate long titles
                "level": heading["level"],
                "source_file": doc_name,
                "page_idx": heading["page_idx"],
                "content_range": (start_idx, end_idx),
                "item_count": len(body_items),
                "body_preview": body_preview,
                "key_topics": [],  # Filled by entity mapping
            })

    return chapters


def map_entities_to_chapters(
    rag_storage_dir: Path,
    chapters: List[Dict[str, Any]],
    content_list_dir: Path
) -> Tuple[List[Dict[str, Any]], Dict[str, str]]:
    """
    Map LightRAG entities to chapters and extract key_topics per chapter.

    Args:
        rag_storage_dir: Path to RAG storage directory.
        chapters: List of chapter nodes (will be modified in-place).
        content_list_dir: Path to content_list directory.

    Returns:
        Tuple of (modified_chapters, chunk_to_chapter_mapping).
    """
    entities_file = rag_storage_dir / "kv_store_full_entities.json"
    chunks_file = rag_storage_dir / "kv_store_text_chunks.json"

    if not entities_file.exists() or not chunks_file.exists():
        return chapters, {}

    try:
        with open(entities_file, encoding="utf-8") as f:
            entities = json.load(f)
        with open(chunks_file, encoding="utf-8") as f:
            chunks = json.load(f)
    except (json.JSONDecodeError, IOError):
        return chapters, {}

    # Step 1: Build feature text for each chapter (first 500 chars)
    chapter_texts = {}
    for ch in chapters:
        doc_file = content_list_dir / f"{ch['source_file']}.json"
        if doc_file.exists():
            try:
                with open(doc_file, encoding="utf-8") as f:
                    items = json.load(f)
                start, end = ch["content_range"]
                section_text = " ".join(
                    item.get("text", "") for item in items[start:end]
                    if item.get("type") == "text"
                )
                chapter_texts[ch["id"]] = section_text
            except (json.JSONDecodeError, IOError):
                chapter_texts[ch["id"]] = ""

    # Step 2: Map each chunk to its chapter via text matching
    chunk_to_chapter = {}
    for chunk_id, chunk_data in chunks.items():
        chunk_content = chunk_data.get("content", "")[:200]
        if not chunk_content:
            continue

        best_match = None
        best_score = 0
        for ch in chapters:
            ch_text = chapter_texts.get(ch["id"], "")
            if not ch_text:
                continue

            # Simple matching: check if chunk appears in chapter text
            if chunk_content[:100] in ch_text:
                score = len(chunk_content) if chunk_content[200:] in ch_text else len(chunk_content[:100])
                if score > best_score:
                    best_score = score
                    best_match = ch["id"]

        if best_match:
            chunk_to_chapter[chunk_id] = best_match

    # Step 3: Map entities to chapters
    chapter_entity_counts: Dict[str, Dict[str, int]] = {
        ch["id"]: {} for ch in chapters
    }

    for entity_name, entity_data in entities.items():
        source_ids = entity_data.get("source_id", "").split("<SEP>")
        for source_id in source_ids:
            source_id = source_id.strip()
            if source_id in chunk_to_chapter:
                ch_id = chunk_to_chapter[source_id]
                counts = chapter_entity_counts[ch_id]
                counts[entity_name] = counts.get(entity_name, 0) + 1

    # Step 4: Extract top-10 entities as key_topics per chapter
    for ch in chapters:
        counts = chapter_entity_counts.get(ch["id"], {})
        sorted_entities = sorted(counts.items(), key=lambda x: -x[1])
        ch["key_topics"] = [name for name, _ in sorted_entities[:10]]

    return chapters, chunk_to_chapter


def build_chapter_edges(
    chapters: List[Dict[str, Any]],
    rag_storage_dir: Path,
    chunk_to_chapter: Dict[str, str]
) -> List[Dict[str, Any]]:
    """
    Build edges between chapter nodes.

    Edge types:
    - "contains": parent-child hierarchy (chapter → section)
    - "follows": sequential order (chapter N → chapter N+1, same level)
    - "related": cross-chapter relationships based on shared entities

    Args:
        chapters: List of chapter nodes.
        rag_storage_dir: Path to RAG storage directory.
        chunk_to_chapter: Mapping from chunk ID to chapter ID.

    Returns:
        List of edge dictionaries.
    """
    edges = []

    # --- 1. Hierarchy edges (contains) ---
    # Group by document, sort by content_range start
    doc_groups: Dict[str, List[Dict[str, Any]]] = {}
    for ch in chapters:
        doc_groups.setdefault(ch["source_file"], []).append(ch)

    for doc_name, doc_chapters in doc_groups.items():
        doc_chapters.sort(key=lambda x: x["content_range"][0])

        # Use stack to build hierarchy
        stack: List[Tuple[Dict[str, Any], int]] = []
        for ch in doc_chapters:
            # Pop all levels >= current (find parent)
            while stack and stack[-1][1] >= ch["level"]:
                stack.pop()
            if stack:
                edges.append({
                    "source": stack[-1][0]["id"],
                    "target": ch["id"],
                    "type": "contains",
                })
            stack.append((ch, ch["level"]))

    # --- 2. Sequential edges (follows) ---
    # Same-level headings in sequence
    for doc_name, doc_chapters in doc_groups.items():
        by_level: Dict[int, List[Dict[str, Any]]] = {}
        for ch in doc_chapters:
            by_level.setdefault(ch["level"], []).append(ch)

        for level, chs in by_level.items():
            chs.sort(key=lambda x: x["content_range"][0])
            for i in range(len(chs) - 1):
                edges.append({
                    "source": chs[i]["id"],
                    "target": chs[i + 1]["id"],
                    "type": "follows",
                })

    # --- 3. Cross-chapter related edges ---
    # Based on LightRAG relations where entities belong to different chapters
    relations_file = rag_storage_dir / "kv_store_full_relations.json"
    entities_file = rag_storage_dir / "kv_store_full_entities.json"

    if relations_file.exists() and entities_file.exists():
        try:
            with open(relations_file, encoding="utf-8") as f:
                relations = json.load(f)
            with open(entities_file, encoding="utf-8") as f:
                entities = json.load(f)
        except (json.JSONDecodeError, IOError):
            relations = {}
            entities = {}

        # entity -> set of chapters it belongs to
        entity_chapters: Dict[str, Set[str]] = {}
        for entity_name, entity_data in entities.items():
            source_ids = entity_data.get("source_id", "").split("<SEP>")
            ch_set = set()
            for sid in source_ids:
                sid = sid.strip()
                if sid in chunk_to_chapter:
                    ch_set.add(chunk_to_chapter[sid])
            entity_chapters[entity_name] = ch_set

        # Count cross-chapter edges
        cross_edges: Dict[Tuple[str, str], int] = {}
        for rel_key, rel_data in relations.items():
            src = rel_data.get("src_id", "")
            tgt = rel_data.get("tgt_id", "")
            src_chs = entity_chapters.get(src, set())
            tgt_chs = entity_chapters.get(tgt, set())

            for s_ch in src_chs:
                for t_ch in tgt_chs:
                    if s_ch != t_ch:
                        key = tuple(sorted([s_ch, t_ch]))
                        cross_edges[key] = cross_edges.get(key, 0) + 1

        # Only keep edges with weight >= 3 (filter noise)
        for (ch_a, ch_b), weight in cross_edges.items():
            if weight >= 3:
                edges.append({
                    "source": ch_a,
                    "target": ch_b,
                    "type": "related",
                    "weight": weight,
                })

    return edges


def get_or_generate_chapter_graph(
    kb_dir: Path,
    regenerate: bool = False
) -> Dict[str, Any]:
    """
    Get or generate chapter-level knowledge graph for a knowledge base.

    Args:
        kb_dir: Path to the knowledge base directory.
        regenerate: If True, force regeneration even if cache exists.

    Returns:
        Dictionary with "nodes", "edges", and "metadata".

    Raises:
        FileNotFoundError: If content_list doesn't exist.
    """
    cache_file = kb_dir / "chapter_graph.json"

    if not regenerate and cache_file.exists():
        try:
            with open(cache_file, encoding="utf-8") as f:
                return json.load(f)
        except (json.JSONDecodeError, IOError):
            pass  # Continue to regenerate

    content_list_dir = kb_dir / "content_list"
    rag_storage_dir = kb_dir / "rag_storage"

    if not content_list_dir.exists():
        raise FileNotFoundError("No content_list found for this knowledge base")

    # Step 1: Extract chapter structure
    chapters = extract_chapter_structure(content_list_dir)

    if not chapters:
        return {
            "nodes": [],
            "edges": [],
            "metadata": {
                "generated_at": datetime.now().isoformat(),
                "total_entities": 0,
                "total_relations": 0,
            },
        }

    # Step 2: Map entities to chapters
    chapters, chunk_to_chapter = map_entities_to_chapters(
        rag_storage_dir, chapters, content_list_dir
    )

    # Step 3: Build edges
    edges = build_chapter_edges(chapters, rag_storage_dir, chunk_to_chapter)

    # Step 4: Clean up internal fields (content_range)
    for ch in chapters:
        if "content_range" in ch:
            del ch["content_range"]

    # Step 5: Read metadata statistics
    metadata = {"generated_at": datetime.now().isoformat()}
    entities_file = rag_storage_dir / "kv_store_full_entities.json"
    relations_file = rag_storage_dir / "kv_store_full_relations.json"

    if entities_file.exists():
        try:
            with open(entities_file, encoding="utf-8") as f:
                metadata["total_entities"] = len(json.load(f))
        except (json.JSONDecodeError, IOError):
            metadata["total_entities"] = 0
    else:
        metadata["total_entities"] = 0

    if relations_file.exists():
        try:
            with open(relations_file, encoding="utf-8") as f:
                metadata["total_relations"] = len(json.load(f))
        except (json.JSONDecodeError, IOError):
            metadata["total_relations"] = 0
    else:
        metadata["total_relations"] = 0

    result = {
        "nodes": chapters,
        "edges": edges,
        "metadata": metadata,
    }

    # Cache the result
    try:
        with open(cache_file, "w", encoding="utf-8") as f:
            json.dump(result, f, ensure_ascii=False, indent=2)
    except IOError:
        pass  # Continue even if caching fails

    return result


def get_chapter_node_detail(kb_dir: Path, node_id: str) -> Dict[str, Any]:
    """
    Get detailed information for a specific chapter node.

    Args:
        kb_dir: Path to the knowledge base directory.
        node_id: ID of the chapter node.

    Returns:
        Dictionary with node details, full body, numbered items, and related chapters.

    Raises:
        ValueError: If node is not found.
    """
    # Get the graph (may use cache)
    graph = get_or_generate_chapter_graph(kb_dir)

    node = next((n for n in graph["nodes"] if n["id"] == node_id), None)
    if not node:
        raise ValueError(f"Node {node_id} not found")

    # Get full body from content_list
    content_list_file = kb_dir / "content_list" / f"{node['source_file']}.json"
    full_body = ""
    page_start = node.get("page_idx", 0)
    page_end = page_start + 10  # Default page range: 10 pages

    if content_list_file.exists():
        try:
            with open(content_list_file, encoding="utf-8") as f:
                items = json.load(f)

            # Re-extract structure to find content_range
            chapters = extract_chapter_structure(kb_dir / "content_list")
            target = next((c for c in chapters if c["id"] == node_id), None)

            if target:
                start, end = target["content_range"]
                body_items = items[start:end]
                full_body = "\n\n".join(
                    item.get("text", "") for item in body_items
                    if item.get("type") == "text"
                )
                # Update page range
                pages_with_idx = [item.get("page_idx") for item in body_items if item.get("page_idx")]
                if pages_with_idx:
                    page_start = min(pages_with_idx)
                    page_end = max(pages_with_idx) + 1
        except (json.JSONDecodeError, IOError):
            full_body = node.get("body_preview", "")

    # Get numbered items in this chapter's page range
    numbered_items_in_chapter = []
    numbered_items_file = kb_dir / "numbered_items.json"

    if numbered_items_file.exists():
        try:
            with open(numbered_items_file, encoding="utf-8") as f:
                all_items = json.load(f)

            for identifier, item_data in all_items.items():
                page = item_data.get("page", 0)
                if page_start <= page < page_end:
                    text = item_data.get("text", "")
                    numbered_items_in_chapter.append({
                        "identifier": identifier,
                        "type": item_data.get("type", ""),
                        "text": text[:300] if len(text) > 300 else text,
                    })
        except (json.JSONDecodeError, IOError):
            pass

    # Limit to 20 items
    numbered_items_in_chapter = numbered_items_in_chapter[:20]

    # Find related chapters
    related_chapters = []
    for edge in graph["edges"]:
        related_id = None
        if edge["source"] == node_id and edge["type"] == "related":
            related_id = edge["target"]
        elif edge["target"] == node_id and edge["type"] == "related":
            related_id = edge["source"]

        if related_id:
            related = next((n for n in graph["nodes"] if n["id"] == related_id), None)
            if related:
                related_chapters.append({
                    "id": related["id"],
                    "title": related["title"],
                })

    return {
        **node,
        "full_body": full_body,
        "numbered_items": numbered_items_in_chapter,
        "related_chapters": related_chapters,
    }


# CLI for testing
if __name__ == "__main__":
    import sys
    import argparse

    parser = argparse.ArgumentParser(description="Generate chapter-level knowledge graph")
    parser.add_argument("kb_name", help="Knowledge base name")
    parser.add_argument("--base-dir", default="data/knowledge_bases", help="Base directory for KBs")
    parser.add_argument("--regenerate", action="store_true", help="Force regeneration")

    args = parser.parse_args()

    kb_dir = Path(args.base_dir) / args.kb_name

    try:
        graph = get_or_generate_chapter_graph(kb_dir, args.regenerate)
        print(f"Generated graph: {len(graph['nodes'])} nodes, {len(graph['edges'])} edges")
        print(json.dumps(graph, ensure_ascii=False, indent=2))
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
