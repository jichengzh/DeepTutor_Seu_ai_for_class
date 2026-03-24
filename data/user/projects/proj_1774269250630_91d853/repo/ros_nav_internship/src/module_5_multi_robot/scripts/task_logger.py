#!/usr/bin/env python3
"""
task_logger.py — ROS2 node that logs fleet events to a SQLite database.

Schema
──────
  tasks       : task lifecycle (created, started, completed, failed)
  assignments : CBBA allocation records
  conflicts   : conflict detection and resolution events

The node subscribes to:
  /fleet_status    (std_msgs/String, JSON)  — robot status updates
  /conflict_events (std_msgs/String, JSON)  — conflict resolution events
  /task_assignment (std_msgs/String, JSON)  — CBBA assignment output

Export helpers:
  to_csv(table, output_path)   — dump any table to CSV
  to_json(table, output_path)  — dump any table to JSON
  summary_stats()              — return dict with aggregate metrics
"""

import json
import csv
import sqlite3
import datetime
import os
import argparse

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy
from std_msgs.msg import String


# ─────────────────────────────────────────────────────────────────────────────
# Schema
# ─────────────────────────────────────────────────────────────────────────────

SCHEMA = """
CREATE TABLE IF NOT EXISTS tasks (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    task_id         INTEGER NOT NULL,
    robot_id        INTEGER,
    event           TEXT NOT NULL,   -- 'created' | 'started' | 'completed' | 'failed'
    timestamp       TEXT NOT NULL,
    x               REAL,
    y               REAL,
    priority        REAL DEFAULT 1.0,
    notes           TEXT
);

CREATE TABLE IF NOT EXISTS assignments (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp       TEXT NOT NULL,
    robot_id        INTEGER NOT NULL,
    task_ids        TEXT NOT NULL,   -- JSON array string
    converged       INTEGER,         -- 1 = converged, 0 = not
    iterations      INTEGER,
    total_value     REAL
);

CREATE TABLE IF NOT EXISTS conflicts (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp       TEXT NOT NULL,
    robot_a         INTEGER NOT NULL,
    robot_b         INTEGER NOT NULL,
    ttc             REAL,
    action          TEXT,
    resolved        INTEGER DEFAULT 0   -- 1 = resolved
);
"""


# ─────────────────────────────────────────────────────────────────────────────
# TaskLogger node
# ─────────────────────────────────────────────────────────────────────────────

class TaskLogger(Node):
    """ROS2 node that logs fleet events to SQLite."""

    def __init__(self, db_path: str = "/tmp/fleet_log.db"):
        super().__init__("task_logger")

        self.declare_parameter("db_path", db_path)
        db_path = self.get_parameter("db_path").get_parameter_value().string_value

        self.get_logger().info(f"Opening database at: {db_path}")
        self._conn = sqlite3.connect(db_path, check_same_thread=False)
        self._conn.executescript(SCHEMA)
        self._conn.commit()

        # QoS for assignment topic (RELIABLE + TRANSIENT_LOCAL)
        assignment_qos = QoSProfile(
            depth=10,
            reliability=ReliabilityPolicy.RELIABLE,
            durability=DurabilityPolicy.TRANSIENT_LOCAL,
        )

        self._fleet_sub = self.create_subscription(
            String, "/fleet_status", self._fleet_status_cb, 10
        )
        self._conflict_sub = self.create_subscription(
            String, "/conflict_events", self._conflict_events_cb, 10
        )
        self._assignment_sub = self.create_subscription(
            String, "/task_assignment", self._assignment_cb, assignment_qos
        )

        self.get_logger().info("TaskLogger ready — logging to " + db_path)

    # ── Private callbacks ────────────────────────────────────────────────────

    def _fleet_status_cb(self, msg: String) -> None:
        """Parse fleet_status JSON and log task-level events."""
        try:
            data = json.loads(msg.data)
        except json.JSONDecodeError as exc:
            self.get_logger().warn(f"fleet_status parse error: {exc}")
            return

        fleet = data.get("fleet_status", {})
        ts    = _now_iso()

        for robot_id_str, info in fleet.items():
            robot_id = int(robot_id_str)
            status   = info.get("status", "")

            # Map status string to task event
            event = None
            if "navigating" in status:
                event = "started"
            elif status == "goal_succeeded":
                event = "completed"
            elif status == "goal_failed":
                event = "failed"

            if event:
                # We don't have the exact task_id here; use robot_id as proxy key
                self._conn.execute(
                    "INSERT INTO tasks (task_id, robot_id, event, timestamp) "
                    "VALUES (?, ?, ?, ?)",
                    (-1, robot_id, event, ts),
                )

        self._conn.commit()

    def _conflict_events_cb(self, msg: String) -> None:
        """Parse conflict event JSON and insert into conflicts table."""
        try:
            data = json.loads(msg.data)
        except json.JSONDecodeError as exc:
            self.get_logger().warn(f"conflict_events parse error: {exc}")
            return

        ts       = _now_iso()
        robot_a  = data.get("robot_a", -1)
        robot_b  = data.get("robot_b", -1)
        ttc      = data.get("ttc", None)
        action   = data.get("action", "")
        resolved = 1 if action else 0

        self._conn.execute(
            "INSERT INTO conflicts (timestamp, robot_a, robot_b, ttc, action, resolved) "
            "VALUES (?, ?, ?, ?, ?, ?)",
            (ts, robot_a, robot_b, ttc, action, resolved),
        )
        self._conn.commit()

        self.get_logger().info(
            f"Logged conflict: robots {robot_a} & {robot_b}, TTC={ttc:.2f}s"
        )

    def _assignment_cb(self, msg: String) -> None:
        """Parse CBBA assignment JSON and insert into assignments table."""
        try:
            data = json.loads(msg.data)
        except json.JSONDecodeError as exc:
            self.get_logger().warn(f"task_assignment parse error: {exc}")
            return

        ts          = _now_iso()
        converged   = 1 if data.get("converged", False) else 0
        iterations  = data.get("iterations", 0)
        total_value = data.get("total_value", 0.0)
        assignment  = data.get("assignment", {})

        for robot_id_str, tasks in assignment.items():
            robot_id = int(robot_id_str)
            task_ids = json.dumps([t.get("task_id", -1) for t in tasks])

            self._conn.execute(
                "INSERT INTO assignments "
                "(timestamp, robot_id, task_ids, converged, iterations, total_value) "
                "VALUES (?, ?, ?, ?, ?, ?)",
                (ts, robot_id, task_ids, converged, iterations, total_value),
            )

            # Also log individual task creation events
            for task in tasks:
                self._conn.execute(
                    "INSERT INTO tasks "
                    "(task_id, robot_id, event, timestamp, x, y) "
                    "VALUES (?, ?, ?, ?, ?, ?)",
                    (
                        task.get("task_id", -1),
                        robot_id,
                        "created",
                        ts,
                        task.get("x", None),
                        task.get("y", None),
                    ),
                )

        self._conn.commit()
        self.get_logger().info(
            f"Logged assignment: {len(assignment)} robots, "
            f"converged={bool(converged)}, value={total_value:.3f}"
        )

    # ── Public export methods ────────────────────────────────────────────────

    def log_task_start(self, task_id: int, robot_id: int,
                        x: float = 0.0, y: float = 0.0) -> None:
        """Manually log a task-start event."""
        self._conn.execute(
            "INSERT INTO tasks (task_id, robot_id, event, timestamp, x, y) "
            "VALUES (?, ?, ?, ?, ?, ?)",
            (task_id, robot_id, "started", _now_iso(), x, y),
        )
        self._conn.commit()

    def log_task_completion(self, task_id: int, robot_id: int,
                             notes: str = "") -> None:
        """Manually log a task-completion event."""
        self._conn.execute(
            "INSERT INTO tasks (task_id, robot_id, event, timestamp, notes) "
            "VALUES (?, ?, ?, ?, ?)",
            (task_id, robot_id, "completed", _now_iso(), notes),
        )
        self._conn.commit()

    def to_csv(self, table: str, output_path: str) -> None:
        """Export a database table to a CSV file."""
        cursor = self._conn.execute(f"SELECT * FROM {table}")  # noqa: S608
        columns = [desc[0] for desc in cursor.description]
        rows    = cursor.fetchall()

        with open(output_path, "w", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(columns)
            writer.writerows(rows)

        self.get_logger().info(
            f"Exported {len(rows)} rows from '{table}' to {output_path}"
        )

    def to_json(self, table: str, output_path: str) -> None:
        """Export a database table to a JSON file."""
        cursor = self._conn.execute(f"SELECT * FROM {table}")  # noqa: S608
        columns = [desc[0] for desc in cursor.description]
        rows    = [dict(zip(columns, row)) for row in cursor.fetchall()]

        with open(output_path, "w") as f:
            json.dump(rows, f, indent=2)

        self.get_logger().info(
            f"Exported {len(rows)} rows from '{table}' to {output_path}"
        )

    def summary_stats(self) -> dict:
        """Return aggregate metrics across all tables."""
        stats = {}

        # Task stats
        for event in ("created", "started", "completed", "failed"):
            row = self._conn.execute(
                "SELECT COUNT(*) FROM tasks WHERE event=?", (event,)
            ).fetchone()
            stats[f"tasks_{event}"] = row[0]

        # Conflict stats
        row = self._conn.execute("SELECT COUNT(*) FROM conflicts").fetchone()
        stats["total_conflicts"] = row[0]

        row = self._conn.execute(
            "SELECT COUNT(*) FROM conflicts WHERE resolved=1"
        ).fetchone()
        stats["resolved_conflicts"] = row[0]

        row = self._conn.execute("SELECT AVG(ttc) FROM conflicts").fetchone()
        stats["avg_ttc_s"] = row[0]

        # Assignment stats
        row = self._conn.execute(
            "SELECT AVG(total_value) FROM assignments"
        ).fetchone()
        stats["avg_assignment_value"] = row[0]

        row = self._conn.execute(
            "SELECT AVG(iterations) FROM assignments"
        ).fetchone()
        stats["avg_cbba_iterations"] = row[0]

        return stats

    def destroy_node(self):
        if self._conn:
            self._conn.close()
        super().destroy_node()


# ─────────────────────────────────────────────────────────────────────────────
# Helpers
# ─────────────────────────────────────────────────────────────────────────────

def _now_iso() -> str:
    return datetime.datetime.utcnow().isoformat()


# ─────────────────────────────────────────────────────────────────────────────
# main
# ─────────────────────────────────────────────────────────────────────────────

def main(args=None):
    parser = argparse.ArgumentParser(description="Fleet task logger ROS2 node")
    parser.add_argument(
        "--db", default="/tmp/fleet_log.db",
        help="Path to SQLite database file"
    )
    parsed, remaining = parser.parse_known_args()

    rclpy.init(args=remaining)
    node = TaskLogger(db_path=parsed.db)
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        # Print summary before shutting down
        try:
            stats = node.summary_stats()
            print("\n=== Fleet Session Summary ===")
            for k, v in stats.items():
                print(f"  {k}: {v}")
        except Exception:
            pass
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
