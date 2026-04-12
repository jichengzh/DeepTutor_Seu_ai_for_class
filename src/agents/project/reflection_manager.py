# -*- coding: utf-8 -*-
"""
ReflectionManager — 全局审阅反思文档管理。

存储路径：data/user/project_reflections.json
每条反思包含：rule（精炼规则）、source（来源主题）、created_at、use_count
最多保留 MAX_ENTRIES 条，超出时淘汰最旧且使用次数最低的条目。
"""

import json
import time
from pathlib import Path
from typing import Any


MAX_ENTRIES = 50


class ReflectionManager:
    """全局反思文档的持久化管理。"""

    def __init__(self, base_dir: str | None = None):
        if base_dir is None:
            project_root = Path(__file__).resolve().parents[3]
            base_dir_path = project_root / "data" / "user"
        else:
            base_dir_path = Path(base_dir)

        base_dir_path.mkdir(parents=True, exist_ok=True)
        self.file_path = base_dir_path / "project_reflections.json"
        self._ensure_file()

    # ------------------------------------------------------------------
    # Internal
    # ------------------------------------------------------------------

    def _ensure_file(self):
        if not self.file_path.exists():
            self._save({"version": "1.0", "entries": []})

    def _load(self) -> dict[str, Any]:
        try:
            with open(self.file_path, encoding="utf-8") as f:
                return json.load(f)
        except (json.JSONDecodeError, FileNotFoundError):
            return {"version": "1.0", "entries": []}

    def _save(self, data: dict[str, Any]):
        with open(self.file_path, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2, ensure_ascii=False)

    def _entries(self) -> list[dict[str, Any]]:
        return self._load().get("entries", [])

    def _save_entries(self, entries: list[dict[str, Any]]):
        data = self._load()
        data["entries"] = entries
        self._save(data)

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def list_entries(self) -> list[dict[str, Any]]:
        """返回所有反思条目（按创建时间降序）。"""
        return sorted(self._entries(), key=lambda e: e.get("created_at", 0), reverse=True)

    def add_entry(self, rule: str, source: str = "") -> dict[str, Any]:
        """
        添加一条反思。如果已有语义相似条目（简单文本相似度），则合并。
        """
        entries = self._entries()
        rule_stripped = rule.strip()
        if not rule_stripped:
            return {}

        # 简单去重：检查是否已有高度相似的条目
        for entry in entries:
            if self._is_similar(entry["rule"], rule_stripped):
                # 合并：保留更长的版本，更新时间和来源
                if len(rule_stripped) > len(entry["rule"]):
                    entry["rule"] = rule_stripped
                if source and source not in entry.get("source", ""):
                    entry["source"] = f"{entry.get('source', '')}, {source}".strip(", ")
                entry["updated_at"] = time.time()
                self._save_entries(entries)
                return entry

        # 新增
        new_entry = {
            "id": f"ref_{int(time.time() * 1000)}",
            "rule": rule_stripped,
            "source": source,
            "use_count": 0,
            "created_at": time.time(),
            "updated_at": time.time(),
        }
        entries.insert(0, new_entry)

        # 超出上限时淘汰
        if len(entries) > MAX_ENTRIES:
            # 按 use_count 升序 + created_at 升序排列，淘汰末尾
            entries.sort(key=lambda e: (e.get("use_count", 0), e.get("created_at", 0)))
            entries = entries[-(MAX_ENTRIES):]

        self._save_entries(entries)
        return new_entry

    def add_entries_batch(self, rules: list[str], source: str = "") -> int:
        """批量添加反思条目，返回实际新增数量。"""
        added = 0
        for rule in rules:
            result = self.add_entry(rule, source)
            if result:
                added += 1
        return added

    def delete_entry(self, entry_id: str) -> bool:
        entries = self._entries()
        new_entries = [e for e in entries if e.get("id") != entry_id]
        if len(new_entries) < len(entries):
            self._save_entries(new_entries)
            return True
        return False

    def get_prompt_text(self) -> str:
        """
        将反思条目格式化为可注入 prompt 的文本。
        同时递增 use_count。
        """
        entries = self._entries()
        if not entries:
            return ""

        # 按 use_count 降序，取最相关的条目
        sorted_entries = sorted(entries, key=lambda e: e.get("use_count", 0), reverse=True)
        lines = []
        for i, entry in enumerate(sorted_entries[:30], 1):  # 最多注入 30 条
            lines.append(f"{i}. {entry['rule']}")
            entry["use_count"] = entry.get("use_count", 0) + 1

        self._save_entries(entries)

        return (
            "根据历史审阅经验，生成文档时请注意以下要点：\n"
            + "\n".join(lines)
        )

    def clear_all(self):
        self._save_entries([])

    # ------------------------------------------------------------------
    # Similarity
    # ------------------------------------------------------------------

    @staticmethod
    def _is_similar(a: str, b: str, threshold: float = 0.6) -> bool:
        """基于字符 bigram 的 Jaccard 相似度判断（兼容中英文）。"""
        if not a or not b:
            return False

        def bigrams(s: str) -> set[str]:
            s = s.lower().replace(" ", "")
            return {s[i:i + 2] for i in range(len(s) - 1)} if len(s) >= 2 else {s}

        set_a = bigrams(a)
        set_b = bigrams(b)
        intersection = set_a & set_b
        union = set_a | set_b
        return len(intersection) / len(union) >= threshold if union else False


# Singleton
_reflection_manager: ReflectionManager | None = None


def get_reflection_manager() -> ReflectionManager:
    global _reflection_manager
    if _reflection_manager is None:
        _reflection_manager = ReflectionManager()
    return _reflection_manager


__all__ = ["ReflectionManager", "get_reflection_manager"]
