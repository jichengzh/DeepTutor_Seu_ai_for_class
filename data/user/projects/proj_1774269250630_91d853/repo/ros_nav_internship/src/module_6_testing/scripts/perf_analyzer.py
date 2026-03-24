#!/usr/bin/env python3
"""
perf_analyzer.py — ROS2 Performance Analyser

Parses the output files produced by:
  ros2 topic hz  /topic  > hz_output.txt
  ros2 topic delay /topic > delay_output.txt

Computes statistical analysis (mean, std, percentiles) and generates:
  - A matplotlib latency heat-map  (latency_heatmap.png)
  - A performance summary JSON     (perf_summary.json)

Usage:
    python3 perf_analyzer.py \
        --hz-file      results/hz_output.txt       \
        --delay-file   results/delay_output.txt    \
        --output-dir   results/                    \
        [--no-plot]
"""

import argparse
import json
import math
import re
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple


# ─────────────────────────────────────────────────────────────────────────────
# Parsing helpers
# ─────────────────────────────────────────────────────────────────────────────

def _parse_hz_file(path: str) -> Dict[str, List[float]]:
    """
    Parse `ros2 topic hz` output.

    Expected sample lines:
        average rate: 99.872
        min: 0.004s  max: 0.019s  std dev: 0.00122s  window: 100
    Returns dict with keys: rate, min_s, max_s, std_s
    """
    result: Dict[str, List[float]] = {
        "rate": [],
        "min_s": [],
        "max_s": [],
        "std_s": [],
    }

    try:
        text = Path(path).read_text()
    except OSError as exc:
        print(f"[WARN] Cannot read hz file: {exc}", file=sys.stderr)
        return result

    rate_re  = re.compile(r"average rate:\s*([\d.]+)")
    stats_re = re.compile(
        r"min:\s*([\d.]+)s\s+max:\s*([\d.]+)s\s+std dev:\s*([\d.]+)s"
    )

    for line in text.splitlines():
        m = rate_re.search(line)
        if m:
            result["rate"].append(float(m.group(1)))
        m = stats_re.search(line)
        if m:
            result["min_s"].append(float(m.group(1)))
            result["max_s"].append(float(m.group(2)))
            result["std_s"].append(float(m.group(3)))

    return result


def _parse_delay_file(path: str) -> Dict[str, List[float]]:
    """
    Parse `ros2 topic delay` output.

    Expected sample lines:
        average delay: 0.00341
        min: 0.001s  max: 0.012s  std dev: 0.00089s  window: 100
    Returns dict with keys: delay_ms (converted), min_ms, max_ms, std_ms
    """
    result: Dict[str, List[float]] = {
        "delay_ms": [],
        "min_ms": [],
        "max_ms": [],
        "std_ms": [],
    }

    try:
        text = Path(path).read_text()
    except OSError as exc:
        print(f"[WARN] Cannot read delay file: {exc}", file=sys.stderr)
        return result

    delay_re = re.compile(r"average delay:\s*([\d.]+)")
    stats_re = re.compile(
        r"min:\s*([\d.]+)s\s+max:\s*([\d.]+)s\s+std dev:\s*([\d.]+)s"
    )

    for line in text.splitlines():
        m = delay_re.search(line)
        if m:
            result["delay_ms"].append(float(m.group(1)) * 1000.0)
        m = stats_re.search(line)
        if m:
            result["min_ms"].append(float(m.group(1)) * 1000.0)
            result["max_ms"].append(float(m.group(2)) * 1000.0)
            result["std_ms"].append(float(m.group(3)) * 1000.0)

    return result


# ─────────────────────────────────────────────────────────────────────────────
# Statistics
# ─────────────────────────────────────────────────────────────────────────────

def _statistics(samples: List[float]) -> Dict[str, float]:
    """Compute descriptive statistics over a list of samples."""
    if not samples:
        return {k: 0.0 for k in
                ("mean", "std", "min", "max", "p50", "p90", "p95", "p99",
                 "count")}

    n = len(samples)
    s = sorted(samples)

    mean = sum(s) / n
    variance = sum((x - mean) ** 2 for x in s) / n
    std = math.sqrt(variance)

    def _percentile(p: float) -> float:
        idx  = p / 100.0 * (n - 1)
        lo   = int(math.floor(idx))
        hi   = min(lo + 1, n - 1)
        frac = idx - lo
        return s[lo] * (1.0 - frac) + s[hi] * frac

    return {
        "mean":  round(mean, 4),
        "std":   round(std, 4),
        "min":   round(s[0], 4),
        "max":   round(s[-1], 4),
        "p50":   round(_percentile(50), 4),
        "p90":   round(_percentile(90), 4),
        "p95":   round(_percentile(95), 4),
        "p99":   round(_percentile(99), 4),
        "count": float(n),
    }


# ─────────────────────────────────────────────────────────────────────────────
# Heatmap generation
# ─────────────────────────────────────────────────────────────────────────────

def _generate_heatmap(
    delay_samples: List[float],
    output_path: str,
) -> bool:
    """
    Generate a latency heat-map using matplotlib.

    The heat-map bins samples by index (time) on the X axis and latency
    value on the Y axis, giving a visual representation of latency drift.

    Returns True on success, False if matplotlib is unavailable.
    """
    try:
        import matplotlib  # type: ignore
        matplotlib.use("Agg")           # non-interactive backend
        import matplotlib.pyplot as plt  # type: ignore
        import numpy as np              # type: ignore
    except ImportError:
        print("[WARN] matplotlib/numpy not installed — skipping heatmap.",
              file=sys.stderr)
        return False

    if not delay_samples:
        print("[WARN] No delay samples — skipping heatmap.", file=sys.stderr)
        return False

    samples = np.array(delay_samples)
    n       = len(samples)

    # Build 2-D histogram: time (x) vs latency bucket (y)
    time_bins    = min(50, n)
    latency_bins = 40
    x = np.arange(n)

    heatmap, xedges, yedges = np.histogram2d(
        x,
        samples,
        bins=[time_bins, latency_bins],
    )

    fig, axes = plt.subplots(2, 1, figsize=(12, 8))
    fig.suptitle("ROS2 Topic Latency Analysis", fontsize=14, fontweight="bold")

    # ── Top: heat-map ───────────────────────────────────────────────────────
    ax = axes[0]
    im = ax.imshow(
        heatmap.T,
        aspect="auto",
        origin="lower",
        extent=[xedges[0], xedges[-1], yedges[0], yedges[-1]],
        cmap="hot_r",
        interpolation="nearest",
    )
    fig.colorbar(im, ax=ax, label="Sample density")
    ax.set_xlabel("Sample window index")
    ax.set_ylabel("Latency (ms)")
    ax.set_title("Latency Heat-Map")

    # Overlay percentile lines
    stats = _statistics(delay_samples)
    for label, val, color in [
        ("p50", stats["p50"], "blue"),
        ("p95", stats["p95"], "orange"),
        ("p99", stats["p99"], "red"),
    ]:
        ax.axhline(val, color=color, linestyle="--", linewidth=1,
                   label=f"{label}={val:.2f} ms")
    ax.legend(fontsize=8)

    # ── Bottom: time-series ─────────────────────────────────────────────────
    ax2 = axes[1]
    ax2.plot(samples, color="steelblue", linewidth=0.7, label="Latency (ms)")
    ax2.axhline(stats["mean"], color="green",  linestyle="-",  linewidth=1.5,
                label=f"mean={stats['mean']:.2f} ms")
    ax2.axhline(stats["p95"], color="orange", linestyle="--", linewidth=1,
                label=f"p95={stats['p95']:.2f} ms")
    ax2.axhline(stats["p99"], color="red",    linestyle=":",  linewidth=1,
                label=f"p99={stats['p99']:.2f} ms")
    ax2.fill_between(range(n),
                     stats["mean"] - stats["std"],
                     stats["mean"] + stats["std"],
                     alpha=0.2, color="green", label="±1σ")
    ax2.set_xlabel("Sample index")
    ax2.set_ylabel("Latency (ms)")
    ax2.set_title("Latency Time-Series")
    ax2.legend(fontsize=8)
    ax2.grid(True, alpha=0.3)

    plt.tight_layout()
    out = Path(output_path)
    out.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(str(out), dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"[INFO] Latency heat-map saved: {output_path}")
    return True


# ─────────────────────────────────────────────────────────────────────────────
# Performance summary
# ─────────────────────────────────────────────────────────────────────────────

def _build_summary(
    hz_data:    Dict[str, List[float]],
    delay_data: Dict[str, List[float]],
) -> dict:
    """Combine parsed data into a performance summary dict."""

    summary: dict = {
        "generated_utc": _utc_now(),
        "frequency": {
            "rate_hz":    _statistics(hz_data.get("rate", [])),
            "period_min_ms": _statistics(
                [v * 1000 for v in hz_data.get("min_s", [])]
            ),
            "period_max_ms": _statistics(
                [v * 1000 for v in hz_data.get("max_s", [])]
            ),
            "period_std_ms": _statistics(
                [v * 1000 for v in hz_data.get("std_s", [])]
            ),
        },
        "latency": {
            "delay_ms": _statistics(delay_data.get("delay_ms", [])),
            "min_ms":   _statistics(delay_data.get("min_ms", [])),
            "max_ms":   _statistics(delay_data.get("max_ms", [])),
            "std_ms":   _statistics(delay_data.get("std_ms", [])),
        },
    }

    # ── Pass/fail assertions (ASIL-B budget) ─────────────────────────────────
    delay_stats = summary["latency"]["delay_ms"]
    assertions  = {}
    assertions["p99_below_50ms"]  = delay_stats.get("p99",  0.0) < 50.0
    assertions["p95_below_20ms"]  = delay_stats.get("p95",  0.0) < 20.0
    assertions["mean_below_10ms"] = delay_stats.get("mean", 0.0) < 10.0

    rate_stats = summary["frequency"]["rate_hz"]
    if rate_stats.get("count", 0) > 0:
        assertions["rate_within_5pct"] = (
            abs(rate_stats.get("mean", 0.0) - 100.0) / 100.0 < 0.05
        )

    assertions["overall_pass"] = all(assertions.values())
    summary["assertions"] = assertions

    return summary


def _utc_now() -> str:
    import datetime
    return datetime.datetime.utcnow().isoformat() + "Z"


# ─────────────────────────────────────────────────────────────────────────────
# main
# ─────────────────────────────────────────────────────────────────────────────

def main() -> int:
    parser = argparse.ArgumentParser(
        description="Analyse ROS2 topic hz/delay output and produce "
                    "statistical summary + latency heat-map.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--hz-file",
                        default="",
                        help="Path to 'ros2 topic hz' output text file")
    parser.add_argument("--delay-file",
                        default="",
                        help="Path to 'ros2 topic delay' output text file")
    parser.add_argument("--output-dir",
                        default="results",
                        help="Directory for output files (default: results/)")
    parser.add_argument("--no-plot",
                        action="store_true",
                        help="Skip matplotlib heat-map generation")
    parser.add_argument("--verbose", "-v",
                        action="store_true",
                        help="Print summary to stdout")
    args = parser.parse_args()

    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    # ── Parse ────────────────────────────────────────────────────────────────
    hz_data    = _parse_hz_file(args.hz_file)    if args.hz_file    else {}
    delay_data = _parse_delay_file(args.delay_file) if args.delay_file else {}

    # ── Compute summary ──────────────────────────────────────────────────────
    summary = _build_summary(hz_data, delay_data)

    # ── Write JSON ───────────────────────────────────────────────────────────
    json_path = out_dir / "perf_summary.json"
    json_path.write_text(
        json.dumps(summary, indent=2), encoding="utf-8"
    )
    print(f"[INFO] Performance summary written: {json_path}")

    # ── Generate heat-map ────────────────────────────────────────────────────
    if not args.no_plot:
        delay_samples = delay_data.get("delay_ms", [])
        _generate_heatmap(delay_samples, str(out_dir / "latency_heatmap.png"))

    # ── Verbose output ───────────────────────────────────────────────────────
    if args.verbose:
        ds = summary["latency"]["delay_ms"]
        print(
            f"\n{'='*55}\n"
            f"  Latency Statistics\n"
            f"  samples : {int(ds.get('count', 0))}\n"
            f"  mean    : {ds.get('mean', 0.0):.3f} ms\n"
            f"  std     : {ds.get('std',  0.0):.3f} ms\n"
            f"  p50     : {ds.get('p50',  0.0):.3f} ms\n"
            f"  p95     : {ds.get('p95',  0.0):.3f} ms\n"
            f"  p99     : {ds.get('p99',  0.0):.3f} ms\n"
            f"  max     : {ds.get('max',  0.0):.3f} ms\n"
            f"\n  Assertions:\n"
            + "\n".join(
                f"    {'PASS' if v else 'FAIL'}  {k}"
                for k, v in summary.get("assertions", {}).items()
            )
            + f"\n{'='*55}\n"
        )

    overall = summary.get("assertions", {}).get("overall_pass", True)
    return 0 if overall else 1


if __name__ == "__main__":
    sys.exit(main())
