#!/usr/bin/env python3
"""
map_metadata_store.py — SQLite-backed semantic map metadata store.

Provides:
  - SemanticObject  dataclass (mirrors the C++ struct in map_metadata_store.hpp)
  - MapMetadataStore class with full CRUD, JSON export, and context manager support

Schema (table: semantic_objects)
─────────────────────────────────
  id          INTEGER PRIMARY KEY AUTOINCREMENT
  map_name    TEXT    NOT NULL
  x           REAL    NOT NULL
  y           REAL    NOT NULL
  z           REAL    DEFAULT 0.0
  category    TEXT    NOT NULL
  confidence  REAL    NOT NULL
  source_id   INTEGER DEFAULT 0
  timestamp   REAL    NOT NULL
  extra_json  TEXT    DEFAULT '{}'

Usage example
─────────────
  from scripts.map_metadata_store import MapMetadataStore, SemanticObject
  import time

  with MapMetadataStore("/tmp/mymap.db") as store:
      obj = SemanticObject(
          map_name="floor_1", x=1.2, y=3.4, z=0.0,
          category="chair", confidence=0.92, source_id=0,
          timestamp=time.time())
      oid = store.insert_object(obj)
      retrieved = store.get_object(oid)
      store.update_confidence(oid, 0.95)
      all_objs = store.get_all_objects()
      store.export_to_json("/tmp/floor_1_metadata.json")
"""

from __future__ import annotations

import contextlib
import json
import sqlite3
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Iterator, List, Optional


# ─────────────────────────────────────────────────────────────────────────────
# Data model
# ─────────────────────────────────────────────────────────────────────────────

@dataclass
class SemanticObject:
    """
    Represents one semantic observation stored in the metadata database.

    Fields
    ──────
    map_name   : identifier of the map this object belongs to
    x, y, z    : world-frame position in metres
    category   : human-readable semantic class (e.g. "chair", "door")
    confidence : detection confidence [0.0 .. 1.0]
    source_id  : sensor / agent that produced this observation (0 = default)
    timestamp  : UNIX timestamp of observation
    extra_json : arbitrary key/value metadata serialised as a JSON string
    id         : database primary key (auto-assigned on insert; -1 before insert)
    """
    map_name:    str
    x:           float
    y:           float
    category:    str
    confidence:  float
    timestamp:   float
    z:           float   = 0.0
    source_id:   int     = 0
    extra_json:  str     = "{}"
    id:          int     = -1

    def to_dict(self) -> dict:
        return asdict(self)

    @classmethod
    def from_row(cls, row: sqlite3.Row) -> "SemanticObject":
        return cls(
            id=row["id"],
            map_name=row["map_name"],
            x=row["x"],
            y=row["y"],
            z=row["z"],
            category=row["category"],
            confidence=row["confidence"],
            source_id=row["source_id"],
            timestamp=row["timestamp"],
            extra_json=row["extra_json"] or "{}",
        )


# ─────────────────────────────────────────────────────────────────────────────
# Store
# ─────────────────────────────────────────────────────────────────────────────

class MapMetadataStore:
    """
    SQLite-backed persistent store for semantic map metadata.

    Thread-safety
    ─────────────
    Each call creates/uses a single connection.  For multi-threaded use, create
    one MapMetadataStore instance per thread or protect calls with a threading.Lock.

    Context manager
    ───────────────
    Use ``with MapMetadataStore(path) as store:`` to ensure the connection is
    committed and closed even if an exception is raised.
    """

    _SCHEMA = """
    CREATE TABLE IF NOT EXISTS semantic_objects (
        id          INTEGER PRIMARY KEY AUTOINCREMENT,
        map_name    TEXT    NOT NULL,
        x           REAL    NOT NULL,
        y           REAL    NOT NULL,
        z           REAL    NOT NULL DEFAULT 0.0,
        category    TEXT    NOT NULL,
        confidence  REAL    NOT NULL,
        source_id   INTEGER NOT NULL DEFAULT 0,
        timestamp   REAL    NOT NULL,
        extra_json  TEXT    NOT NULL DEFAULT '{}'
    );

    CREATE INDEX IF NOT EXISTS idx_map_name
        ON semantic_objects (map_name);

    CREATE INDEX IF NOT EXISTS idx_category
        ON semantic_objects (category);

    CREATE INDEX IF NOT EXISTS idx_confidence
        ON semantic_objects (confidence);
    """

    def __init__(self, db_path: str = ":memory:") -> None:
        """
        Open (or create) the SQLite database at *db_path*.

        Use ":memory:" for an ephemeral in-process database (useful for tests).
        """
        self._db_path = db_path
        self._conn: Optional[sqlite3.Connection] = None
        self._connect()

    # ── connection management ─────────────────────────────────────────────────

    def _connect(self) -> None:
        self._conn = sqlite3.connect(self._db_path, check_same_thread=False)
        self._conn.row_factory = sqlite3.Row
        self._conn.execute("PRAGMA journal_mode=WAL;")
        self._conn.execute("PRAGMA foreign_keys=ON;")
        self._conn.executescript(self._SCHEMA)
        self._conn.commit()

    def close(self) -> None:
        """Commit any pending changes and close the database connection."""
        if self._conn is not None:
            self._conn.commit()
            self._conn.close()
            self._conn = None

    # ── context manager ───────────────────────────────────────────────────────

    def __enter__(self) -> "MapMetadataStore":
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> bool:
        self.close()
        return False  # do not suppress exceptions

    # ── CRUD ──────────────────────────────────────────────────────────────────

    def insert_object(self, obj: SemanticObject) -> int:
        """
        Insert a SemanticObject into the database.

        Returns the assigned integer id.
        """
        assert self._conn is not None, "Database connection is closed."
        cur = self._conn.execute(
            """
            INSERT INTO semantic_objects
                (map_name, x, y, z, category, confidence, source_id, timestamp, extra_json)
            VALUES
                (:map_name, :x, :y, :z, :category, :confidence,
                 :source_id, :timestamp, :extra_json)
            """,
            {
                "map_name":   obj.map_name,
                "x":          obj.x,
                "y":          obj.y,
                "z":          obj.z,
                "category":   obj.category,
                "confidence": obj.confidence,
                "source_id":  obj.source_id,
                "timestamp":  obj.timestamp,
                "extra_json": obj.extra_json,
            },
        )
        self._conn.commit()
        return cur.lastrowid  # type: ignore[return-value]

    def get_object(self, object_id: int) -> Optional[SemanticObject]:
        """Return the SemanticObject with the given *object_id*, or None."""
        assert self._conn is not None
        cur = self._conn.execute(
            "SELECT * FROM semantic_objects WHERE id = ?", (object_id,)
        )
        row = cur.fetchone()
        return SemanticObject.from_row(row) if row else None

    def update_confidence(self, object_id: int, new_confidence: float) -> bool:
        """
        Update the confidence score for a given object.

        Returns True if a row was updated, False if the id was not found.
        """
        assert self._conn is not None
        new_confidence = float(max(0.0, min(1.0, new_confidence)))
        cur = self._conn.execute(
            "UPDATE semantic_objects SET confidence = ? WHERE id = ?",
            (new_confidence, object_id),
        )
        self._conn.commit()
        return cur.rowcount > 0

    def update_position(self, object_id: int,
                         x: float, y: float, z: float = 0.0) -> bool:
        """Update the world-frame position of an object."""
        assert self._conn is not None
        cur = self._conn.execute(
            "UPDATE semantic_objects SET x=?, y=?, z=? WHERE id=?",
            (x, y, z, object_id),
        )
        self._conn.commit()
        return cur.rowcount > 0

    def update_extra(self, object_id: int, extra: dict) -> bool:
        """Merge *extra* dict into the stored extra_json field."""
        assert self._conn is not None
        existing = self.get_object(object_id)
        if existing is None:
            return False
        merged = {**json.loads(existing.extra_json), **extra}
        cur = self._conn.execute(
            "UPDATE semantic_objects SET extra_json=? WHERE id=?",
            (json.dumps(merged), object_id),
        )
        self._conn.commit()
        return cur.rowcount > 0

    def delete_object(self, object_id: int) -> bool:
        """Delete an object by id. Returns True if a row was removed."""
        assert self._conn is not None
        cur = self._conn.execute(
            "DELETE FROM semantic_objects WHERE id=?", (object_id,)
        )
        self._conn.commit()
        return cur.rowcount > 0

    # ── query methods ─────────────────────────────────────────────────────────

    def get_all_objects(self) -> List[SemanticObject]:
        """Return all stored SemanticObjects ordered by timestamp."""
        assert self._conn is not None
        cur = self._conn.execute(
            "SELECT * FROM semantic_objects ORDER BY timestamp"
        )
        return [SemanticObject.from_row(row) for row in cur.fetchall()]

    def get_by_map(self, map_name: str) -> List[SemanticObject]:
        """Return all objects belonging to *map_name*."""
        assert self._conn is not None
        cur = self._conn.execute(
            "SELECT * FROM semantic_objects WHERE map_name=? ORDER BY timestamp",
            (map_name,),
        )
        return [SemanticObject.from_row(row) for row in cur.fetchall()]

    def get_by_category(self, category: str) -> List[SemanticObject]:
        """Return all objects with the given semantic *category*."""
        assert self._conn is not None
        cur = self._conn.execute(
            "SELECT * FROM semantic_objects WHERE category=? ORDER BY confidence DESC",
            (category,),
        )
        return [SemanticObject.from_row(row) for row in cur.fetchall()]

    def filter_by_confidence(self, min_confidence: float) -> List[SemanticObject]:
        """Return objects whose confidence is >= *min_confidence*."""
        assert self._conn is not None
        cur = self._conn.execute(
            "SELECT * FROM semantic_objects WHERE confidence >= ? ORDER BY confidence DESC",
            (float(min_confidence),),
        )
        return [SemanticObject.from_row(row) for row in cur.fetchall()]

    def count(self) -> int:
        """Return total number of stored objects."""
        assert self._conn is not None
        cur = self._conn.execute("SELECT COUNT(*) FROM semantic_objects")
        return int(cur.fetchone()[0])

    def map_names(self) -> List[str]:
        """Return the list of distinct map names present in the store."""
        assert self._conn is not None
        cur = self._conn.execute(
            "SELECT DISTINCT map_name FROM semantic_objects ORDER BY map_name"
        )
        return [row[0] for row in cur.fetchall()]

    # ── export / import ───────────────────────────────────────────────────────

    def export_to_json(self, output_path: str,
                        map_name: Optional[str] = None) -> int:
        """
        Export the store (or a single map) to a JSON file.

        The file contains a JSON array of object dicts.
        Returns the number of exported objects.

        Parameters
        ──────────
        output_path : destination file path (will be overwritten)
        map_name    : if given, export only objects for this map
        """
        objects = self.get_by_map(map_name) if map_name else self.get_all_objects()
        records = [obj.to_dict() for obj in objects]

        path = Path(output_path)
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("w", encoding="utf-8") as fh:
            json.dump(records, fh, indent=2)

        return len(records)

    def import_from_json(self, input_path: str,
                          overwrite_map: bool = False) -> int:
        """
        Import objects from a JSON file produced by :meth:`export_to_json`.

        If *overwrite_map* is True, existing objects for each map_name found
        in the file are deleted before importing.

        Returns the number of inserted objects.
        """
        path = Path(input_path)
        if not path.exists():
            raise FileNotFoundError(f"JSON file not found: {input_path}")

        with path.open("r", encoding="utf-8") as fh:
            records: list = json.load(fh)

        if overwrite_map:
            map_names_in_file = {r["map_name"] for r in records}
            for mn in map_names_in_file:
                assert self._conn is not None
                self._conn.execute(
                    "DELETE FROM semantic_objects WHERE map_name=?", (mn,)
                )
            assert self._conn is not None
            self._conn.commit()

        inserted = 0
        for record in records:
            obj = SemanticObject(
                map_name=record.get("map_name", "unknown"),
                x=record.get("x", 0.0),
                y=record.get("y", 0.0),
                z=record.get("z", 0.0),
                category=record.get("category", "unknown"),
                confidence=record.get("confidence", 0.0),
                source_id=record.get("source_id", 0),
                timestamp=record.get("timestamp", time.time()),
                extra_json=json.dumps(record.get("extra_json", {}))
                    if isinstance(record.get("extra_json"), dict)
                    else record.get("extra_json", "{}"),
            )
            self.insert_object(obj)
            inserted += 1

        return inserted

    # ── utility ───────────────────────────────────────────────────────────────

    @contextlib.contextmanager
    def transaction(self) -> Iterator[None]:
        """
        Context manager for explicit transaction control.

        Example::

            with store.transaction():
                store.insert_object(obj1)
                store.insert_object(obj2)
        """
        assert self._conn is not None
        try:
            yield
            self._conn.commit()
        except Exception:
            self._conn.rollback()
            raise

    def vacuum(self) -> None:
        """Run VACUUM to reclaim freed space in the database file."""
        assert self._conn is not None
        self._conn.execute("VACUUM")


# ─────────────────────────────────────────────────────────────────────────────
# CLI helper (for quick inspection)
# ─────────────────────────────────────────────────────────────────────────────

def _cli() -> None:
    import argparse

    parser = argparse.ArgumentParser(
        description="Inspect or export a MapMetadataStore SQLite database."
    )
    parser.add_argument("db", help="Path to the SQLite database file.")
    parser.add_argument("--export", metavar="OUT.json",
                        help="Export all objects to a JSON file.")
    parser.add_argument("--map", metavar="MAP_NAME",
                        help="Filter by map name.")
    parser.add_argument("--count", action="store_true",
                        help="Print the total object count and exit.")
    args = parser.parse_args()

    with MapMetadataStore(args.db) as store:
        if args.count:
            print(f"{store.count()} objects in '{args.db}'")
            return

        if args.export:
            n = store.export_to_json(args.export, map_name=args.map)
            print(f"Exported {n} objects to '{args.export}'")
            return

        # Default: print summary
        print(f"Database: {args.db}")
        print(f"Total objects: {store.count()}")
        print(f"Maps: {store.map_names()}")
        for obj in (store.get_by_map(args.map) if args.map else store.get_all_objects()):
            print(f"  [{obj.id:4d}] {obj.map_name:20s} ({obj.x:7.2f}, {obj.y:7.2f}, {obj.z:5.2f})"
                  f"  {obj.category:20s}  conf={obj.confidence:.2f}")


if __name__ == "__main__":
    _cli()
