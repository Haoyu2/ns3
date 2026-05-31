#!/usr/bin/env python3
"""Summarize tcp-aqm-benchmark traces into a campaign-level CSV."""

from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path

TRACE_PREFIXES = ["tcp-aqm-benchmark", "tcp-validation"]


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[3]


def trace_path(run_dir: Path, suffix: str) -> Path:
    for prefix in TRACE_PREFIXES:
        path = run_dir / f"{prefix}-{suffix}"
        if path.exists():
            return path
    return run_dir / f"{TRACE_PREFIXES[0]}-{suffix}"


def parse_two_column(path: Path, min_time: float) -> list[tuple[float, float]]:
    """Parse a two-column trace, keeping rows with t >= min_time.

    Callers should make sure ``min_time`` is at least the relevant flow start
    time, otherwise pre-flow zero samples will be included in the result. See
    :func:`effective_warmup` for the canonical guard.
    """
    if not path.exists():
        return []
    rows: list[tuple[float, float]] = []
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        parts = line.split()
        if len(parts) < 2:
            continue
        try:
            t = float(parts[0])
            v = float(parts[1])
        except ValueError:
            continue
        if t >= min_time:
            rows.append((t, v))
    return rows


def parse_seconds(value: object, default: float = 0.0) -> float:
    """Parse an ns-3-style duration string (``"5s"``, ``"100ms"``...) to seconds."""
    if value is None:
        return default
    text = str(value).strip().lower()
    if not text:
        return default
    try:
        if text.endswith("ms"):
            return float(text[:-2]) / 1000.0
        if text.endswith("us"):
            return float(text[:-2]) / 1_000_000.0
        if text.endswith("ns"):
            return float(text[:-2]) / 1_000_000_000.0
        if text.endswith("s"):
            return float(text[:-1])
        return float(text)
    except ValueError:
        return default


def effective_warmup(cfg: dict, requested: float) -> tuple[float, float]:
    """Return (first_flow_warmup, second_flow_warmup) clamped to flow start times.

    The raw analyzer warmup is a plain ``t >= warmup`` threshold; if a user
    passes a warmup smaller than the first-flow start time, pre-flow zero
    throughput samples contaminate the mean. Clamping below avoids that without
    forcing every campaign to know its own flow timing.
    """
    first_start = parse_seconds(cfg.get("first_start_time"), 0.0)
    second_start = parse_seconds(cfg.get("second_start_time"), 0.0)
    second_jitter = parse_seconds(cfg.get("second_start_jitter"), 0.0)
    first_warmup = max(requested, first_start)
    second_warmup = max(requested, second_start + second_jitter)
    return first_warmup, second_warmup


def count_lines(path: Path, min_time: float) -> int:
    if not path.exists():
        return 0
    count = 0
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        parts = line.split()
        if not parts:
            continue
        try:
            t = float(parts[0])
        except ValueError:
            continue
        if t >= min_time:
            count += 1
    return count


def mean(values: list[float]) -> float:
    return sum(values) / len(values) if values else math.nan


def percentile(values: list[float], pct: float) -> float:
    if not values:
        return math.nan
    ordered = sorted(values)
    index = (len(ordered) - 1) * pct
    lower = math.floor(index)
    upper = math.ceil(index)
    if lower == upper:
        return ordered[int(index)]
    return ordered[lower] * (upper - index) + ordered[upper] * (index - lower)


def read_metadata(run_dir: Path) -> dict:
    metadata_path = run_dir / "metadata.json"
    if not metadata_path.exists():
        return {}
    return json.loads(metadata_path.read_text(encoding="utf-8"))


def summarize_run(run_dir: Path, warmup: float) -> dict[str, object] | None:
    metadata = read_metadata(run_dir)
    if not metadata:
        return None
    cfg = metadata.get("config", {})
    sweep = metadata.get("sweep", {}) or {}
    analysis_metrics = metadata.get("analysis_metrics", []) or []

    # Clamp the warmup per flow so that pre-flow zero samples cannot deflate
    # the mean throughput, regardless of what the caller asked for.
    first_warmup, second_warmup = effective_warmup(cfg, warmup)
    # Use the first-flow warmup for shared signals (ping, queue, cwnd); they
    # exist for the whole simulation but are only meaningful once the first
    # flow has started.
    throughput = [v for _, v in parse_two_column(trace_path(run_dir, "first-tcp-throughput.dat"), first_warmup)]
    ping = [v for _, v in parse_two_column(trace_path(run_dir, "ping.dat"), first_warmup)]
    qdelay = [v for _, v in parse_two_column(trace_path(run_dir, "queue-length.dat"), first_warmup)]
    cwnd = [v for _, v in parse_two_column(trace_path(run_dir, "first-tcp-cwnd.dat"), first_warmup)]
    second_throughput = [
        v for _, v in parse_two_column(trace_path(run_dir, "second-tcp-throughput.dat"), second_warmup)
    ]
    first_throughput_mean = mean(throughput)
    second_throughput_mean = mean(second_throughput)
    has_second_flow = bool(cfg.get("second_tcp", ""))
    if has_second_flow and not math.isnan(first_throughput_mean) and not math.isnan(second_throughput_mean):
        total_throughput_mean = first_throughput_mean + second_throughput_mean
        # Jain's fairness index hard-coded for the two-flow case used in this
        # study. Generalizing to N flows is straightforward but the harness
        # presently produces traces for at most two flows.
        jain_fairness = (
            total_throughput_mean * total_throughput_mean
        ) / (2 * (first_throughput_mean * first_throughput_mean + second_throughput_mean * second_throughput_mean))
    else:
        total_throughput_mean = first_throughput_mean
        jain_fairness = math.nan

    return {
        "run_id": metadata.get("run_id", run_dir.name),
        "returncode": metadata.get("returncode"),
        "sweep_name": sweep.get("name", ""),
        "sweep_parameter": sweep.get("parameter", ""),
        "sweep_value": sweep.get("value", ""),
        "analysis_metrics": ",".join(str(metric) for metric in analysis_metrics),
        "first_tcp": cfg.get("first_tcp", ""),
        "second_tcp": cfg.get("second_tcp", ""),
        "topology": cfg.get("topology", ""),
        "queue": cfg.get("queue", ""),
        "base_rtt": cfg.get("base_rtt", ""),
        "ecn": int(bool(cfg.get("ecn", False))),
        "rng_run": cfg.get("rng_run", ""),
        "stop_time": cfg.get("stop_time", ""),
        "first_start_time": cfg.get("first_start_time", ""),
        "second_start_time": cfg.get("second_start_time", ""),
        "second_start_jitter": cfg.get("second_start_jitter", ""),
        "link_rate": cfg.get("link_rate", ""),
        "throughput_mean_mbps": first_throughput_mean,
        "throughput_p05_mbps": percentile(throughput, 0.05),
        "throughput_p95_mbps": percentile(throughput, 0.95),
        "second_throughput_mean_mbps": second_throughput_mean,
        "total_throughput_mean_mbps": total_throughput_mean,
        "jain_fairness": jain_fairness,
        "ping_rtt_mean_ms": mean(ping),
        "ping_rtt_p95_ms": percentile(ping, 0.95),
        "queue_delay_mean_ms": mean(qdelay),
        "queue_delay_p95_ms": percentile(qdelay, 0.95),
        "queue_delay_max_ms": max(qdelay) if qdelay else math.nan,
        "cwnd_mean_segments": mean(cwnd),
        "drop_count": count_lines(trace_path(run_dir, "queue-drop.dat"), first_warmup),
        "mark_count": count_lines(trace_path(run_dir, "queue-mark.dat"), first_warmup),
        "first_start_jitter": cfg.get("first_start_jitter", "0s"),
        "warmup_first_flow_s": first_warmup,
        "warmup_second_flow_s": second_warmup,
        "elapsed_wall_seconds": metadata.get("elapsed_wall_seconds", math.nan),
        "git_commit": metadata.get("git", {}).get("commit", ""),
    }


def write_summary(rows: list[dict[str, object]], output: Path) -> None:
    if not rows:
        raise SystemExit("no completed runs found")
    output.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = list(rows[0].keys())
    with output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def ci95(values: list[float]) -> float:
    clean = [value for value in values if not math.isnan(value)]
    if len(clean) < 2:
        return math.nan
    return 1.96 * statistics.stdev(clean) / math.sqrt(len(clean))


def aggregate_rows(rows: list[dict[str, object]]) -> list[dict[str, object]]:
    group_keys = [
        "sweep_name",
        "sweep_parameter",
        "sweep_value",
        "first_tcp",
        "second_tcp",
        "topology",
        "queue",
        "base_rtt",
        "ecn",
        "stop_time",
        "first_start_time",
        "second_start_time",
        "second_start_jitter",
        "link_rate",
        "git_commit",
    ]
    metrics = [
        "throughput_mean_mbps",
        "second_throughput_mean_mbps",
        "total_throughput_mean_mbps",
        "jain_fairness",
        "ping_rtt_mean_ms",
        "ping_rtt_p95_ms",
        "queue_delay_mean_ms",
        "queue_delay_p95_ms",
        "queue_delay_max_ms",
        "cwnd_mean_segments",
        "drop_count",
        "mark_count",
        "elapsed_wall_seconds",
    ]
    groups: dict[tuple[object, ...], list[dict[str, object]]] = defaultdict(list)
    for row in rows:
        if row.get("returncode") == 0:
            groups[tuple(row.get(key, "") for key in group_keys)].append(row)

    output_rows: list[dict[str, object]] = []
    for key, group in sorted(groups.items()):
        out = {name: value for name, value in zip(group_keys, key)}
        out["n_runs"] = len(group)
        for metric in metrics:
            values = [float(row.get(metric, math.nan)) for row in group]
            clean = [value for value in values if not math.isnan(value)]
            out[f"{metric}_mean"] = mean(clean)
            out[f"{metric}_ci95"] = ci95(clean)
        output_rows.append(out)
    return output_rows


def selected_metric_names(rows: list[dict[str, object]]) -> list[str]:
    metrics: set[str] = set()
    for row in rows:
        for metric in str(row.get("analysis_metrics", "")).split(","):
            metric = metric.strip()
            if metric:
                metrics.add(metric)
    return sorted(metrics)


def write_selected_summary(rows: list[dict[str, object]], output: Path) -> bool:
    metrics = selected_metric_names(rows)
    if not metrics:
        return False
    identity = [
        "run_id",
        "sweep_name",
        "sweep_parameter",
        "sweep_value",
        "first_tcp",
        "second_tcp",
        "topology",
        "queue",
        "base_rtt",
        "ecn",
        "rng_run",
    ]
    fieldnames = [name for name in identity if name in rows[0]]
    fieldnames.extend(metric for metric in metrics if metric in rows[0])
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({name: row.get(name, "") for name in fieldnames})
    return True


def write_selected_aggregate(rows: list[dict[str, object]], output: Path, metrics: list[str]) -> bool:
    if not metrics:
        return False
    identity = [
        "sweep_name",
        "sweep_parameter",
        "sweep_value",
        "first_tcp",
        "second_tcp",
        "topology",
        "queue",
        "base_rtt",
        "ecn",
        "n_runs",
    ]
    fieldnames = [name for name in identity if name in rows[0]]
    for metric in metrics:
        for suffix in ("_mean", "_ci95"):
            column = f"{metric}{suffix}"
            if column in rows[0]:
                fieldnames.append(column)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({name: row.get(name, "") for name in fieldnames})
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--results-dir", type=Path, default=None)
    parser.add_argument("--output", type=Path, default=None)
    parser.add_argument("--aggregate-output", type=Path, default=None)
    parser.add_argument("--selected-output", type=Path, default=None)
    parser.add_argument("--selected-aggregate-output", type=Path, default=None)
    parser.add_argument("--warmup", type=float, default=10.0)
    args = parser.parse_args()

    repo = repo_root_from_script()
    results_dir = args.results_dir or (
        repo / "contrib" / "tcp-aqm-config" / "results" / "single-flow"
    )
    output = args.output or results_dir / "summary.csv"
    aggregate_output = args.aggregate_output or results_dir / "summary-aggregate.csv"
    selected_output = args.selected_output or results_dir / "summary-selected.csv"
    selected_aggregate_output = args.selected_aggregate_output or results_dir / "summary-selected-aggregate.csv"

    rows = []
    for run_dir in sorted(results_dir.iterdir()):
        if run_dir.is_dir():
            row = summarize_run(run_dir, args.warmup)
            if row is not None:
                rows.append(row)

    write_summary(rows, output)
    aggregate = aggregate_rows(rows)
    write_summary(aggregate, aggregate_output)
    selected_metrics = selected_metric_names(rows)
    wrote_selected = write_selected_summary(rows, selected_output)
    wrote_selected_aggregate = write_selected_aggregate(
        aggregate,
        selected_aggregate_output,
        selected_metrics,
    )
    print(f"wrote {len(rows)} rows to {output}")
    print(f"wrote {len(aggregate)} aggregate rows to {aggregate_output}")
    if wrote_selected:
        print(f"wrote selected metrics to {selected_output}")
    if wrote_selected_aggregate:
        print(f"wrote selected aggregate metrics to {selected_aggregate_output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
