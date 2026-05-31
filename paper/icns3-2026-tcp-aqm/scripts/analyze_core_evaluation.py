#!/usr/bin/env python3
"""Derive core evaluation tables from TCP/AQM campaign artifacts.

The script is intentionally dependency-free so the paper artifact can be
audited on a plain Python installation. It reads the existing aggregate CSVs
and, when raw run directories are available, recomputes warmup and stability
checks from the preserved traces.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
from collections import defaultdict
from pathlib import Path

import analyze_tcp_aqm


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[3]


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def write_rows(path: Path, rows: list[dict[str, object]]) -> None:
    if not rows:
        raise SystemExit(f"no rows to write for {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = list(rows[0].keys())
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def number(row: dict[str, object], key: str, default: float = math.nan) -> float:
    try:
        return float(row.get(key, default))
    except (TypeError, ValueError):
        return default


def pct_delta(new: float, old: float) -> float:
    if math.isnan(new) or math.isnan(old) or old == 0:
        return math.nan
    return 100.0 * (new - old) / old


def parse_seconds(value: str) -> float:
    value = value.strip().lower()
    if value.endswith("ms"):
        return float(value[:-2]) / 1000.0
    if value.endswith("us"):
        return float(value[:-2]) / 1000000.0
    if value.endswith("ns"):
        return float(value[:-2]) / 1000000000.0
    if value.endswith("s"):
        return float(value[:-1])
    return float(value)


def ecn_impact(single_rows: list[dict[str, str]]) -> list[dict[str, object]]:
    grouped: dict[tuple[str, str, str, str, str], dict[str, dict[str, str]]] = defaultdict(dict)
    for row in single_rows:
        tcp = row["first_tcp"]
        if tcp == "dctcp":
            continue
        key = (tcp, row["queue"], row["base_rtt"], row["stop_time"], row["link_rate"])
        grouped[key][row["ecn"]] = row

    out: list[dict[str, object]] = []
    for (tcp, queue, rtt, stop_time, link_rate), by_ecn in sorted(grouped.items()):
        if "0" not in by_ecn or "1" not in by_ecn:
            continue
        off = by_ecn["0"]
        on = by_ecn["1"]
        off_tput = number(off, "throughput_mean_mbps_mean")
        on_tput = number(on, "throughput_mean_mbps_mean")
        off_q = number(off, "queue_delay_mean_ms_mean")
        on_q = number(on, "queue_delay_mean_ms_mean")
        off_drop = number(off, "drop_count_mean")
        on_drop = number(on, "drop_count_mean")
        off_mark = number(off, "mark_count_mean")
        on_mark = number(on, "mark_count_mean")
        out.append(
            {
                "tcp": tcp,
                "queue": queue,
                "base_rtt": rtt,
                "stop_time": stop_time,
                "link_rate": link_rate,
                "throughput_no_ecn_mbps": off_tput,
                "throughput_ecn_mbps": on_tput,
                "throughput_delta_mbps": on_tput - off_tput,
                "throughput_delta_pct": pct_delta(on_tput, off_tput),
                "queue_delay_no_ecn_ms": off_q,
                "queue_delay_ecn_ms": on_q,
                "queue_delay_delta_ms": on_q - off_q,
                "queue_delay_delta_pct": pct_delta(on_q, off_q),
                "drop_delta": on_drop - off_drop,
                "mark_delta": on_mark - off_mark,
            }
        )
    return out


def rtt_sensitivity(single_rows: list[dict[str, str]]) -> list[dict[str, object]]:
    grouped: dict[tuple[str, str, str], list[dict[str, str]]] = defaultdict(list)
    for row in single_rows:
        grouped[(row["first_tcp"], row["queue"], row["ecn"])].append(row)

    out: list[dict[str, object]] = []
    for (tcp, queue, ecn), group in sorted(grouped.items()):
        if len({row["base_rtt"] for row in group}) < 2:
            continue
        tputs = [number(row, "throughput_mean_mbps_mean") for row in group]
        qdelays = [number(row, "queue_delay_mean_ms_mean") for row in group]
        marks = [number(row, "mark_count_mean") for row in group]
        tput_max = max(tputs)
        # Guard the divisor: if every aggregate row is zero (e.g., the flow
        # never started across all RTTs) we report NaN rather than crashing.
        # The metric is throughput *dispersion* across RTT, not a regression
        # slope — i.e. spread, not signed sensitivity. Documented as such in
        # the paper to avoid implying a monotone response.
        if tput_max > 0:
            throughput_range_pct_of_max = 100.0 * (tput_max - min(tputs)) / tput_max
        else:
            throughput_range_pct_of_max = math.nan
        out.append(
            {
                "tcp": tcp,
                "queue": queue,
                "ecn": ecn,
                "rtt_values": ",".join(sorted(row["base_rtt"] for row in group)),
                "throughput_min_mbps": min(tputs),
                "throughput_max_mbps": tput_max,
                "throughput_range_mbps": tput_max - min(tputs),
                "throughput_range_pct_of_max": throughput_range_pct_of_max,
                "queue_delay_min_ms": min(qdelays),
                "queue_delay_max_ms": max(qdelays),
                "queue_delay_range_ms": max(qdelays) - min(qdelays),
                "mark_min": min(marks),
                "mark_max": max(marks),
                "mark_range": max(marks) - min(marks),
            }
        )
    return out


def ranking_stability(single_rows: list[dict[str, str]]) -> list[dict[str, object]]:
    grouped: dict[tuple[str, str, str], list[dict[str, str]]] = defaultdict(list)
    for row in single_rows:
        grouped[(row["queue"], row["base_rtt"], row["ecn"])].append(row)

    out: list[dict[str, object]] = []
    for (queue, rtt, ecn), group in sorted(grouped.items()):
        by_tput = sorted(group, key=lambda row: number(row, "throughput_mean_mbps_mean"), reverse=True)
        by_delay = sorted(group, key=lambda row: number(row, "queue_delay_mean_ms_mean"))
        out.append(
            {
                "queue": queue,
                "base_rtt": rtt,
                "ecn": ecn,
                "throughput_order": ">".join(row["first_tcp"] for row in by_tput),
                "top_throughput_tcp": by_tput[0]["first_tcp"],
                "top_throughput_mbps": number(by_tput[0], "throughput_mean_mbps_mean"),
                "delay_order": "<".join(row["first_tcp"] for row in by_delay),
                "lowest_delay_tcp": by_delay[0]["first_tcp"],
                "lowest_delay_ms": number(by_delay[0], "queue_delay_mean_ms_mean"),
            }
        )
    return out


def pareto_frontier(single_rows: list[dict[str, str]]) -> list[dict[str, object]]:
    out: list[dict[str, object]] = []
    grouped: dict[tuple[str, str], list[dict[str, str]]] = defaultdict(list)
    for row in single_rows:
        grouped[(row["base_rtt"], row["ecn"])].append(row)

    for (rtt, ecn), group in sorted(grouped.items()):
        for row in group:
            tput = number(row, "throughput_mean_mbps_mean")
            qdelay = number(row, "queue_delay_mean_ms_mean")
            dominated = False
            for other in group:
                other_tput = number(other, "throughput_mean_mbps_mean")
                other_qdelay = number(other, "queue_delay_mean_ms_mean")
                if (
                    other_tput >= tput
                    and other_qdelay <= qdelay
                    and (other_tput > tput or other_qdelay < qdelay)
                ):
                    dominated = True
                    break
            if not dominated:
                out.append(
                    {
                        "base_rtt": rtt,
                        "ecn": ecn,
                        "tcp": row["first_tcp"],
                        "queue": row["queue"],
                        "throughput_mbps": tput,
                        "queue_delay_ms": qdelay,
                        "drop_count": number(row, "drop_count_mean"),
                        "mark_count": number(row, "mark_count_mean"),
                    }
                )
    return out


def mixed_flow_share(mixed_rows: list[dict[str, str]]) -> list[dict[str, object]]:
    out: list[dict[str, object]] = []
    for row in sorted(mixed_rows, key=lambda r: (r["first_tcp"], r["second_tcp"], r["queue"], r["base_rtt"])):
        first = number(row, "throughput_mean_mbps_mean")
        second = number(row, "second_throughput_mean_mbps_mean")
        total = number(row, "total_throughput_mean_mbps_mean")
        first_share = first / total if not math.isnan(total) and total else math.nan
        second_share = second / total if not math.isnan(total) and total else math.nan
        out.append(
            {
                "first_tcp": row["first_tcp"],
                "second_tcp": row["second_tcp"],
                "pair": f"{row['first_tcp']}/{row['second_tcp']}",
                "queue": row["queue"],
                "base_rtt": row["base_rtt"],
                "first_throughput_mbps": first,
                "second_throughput_mbps": second,
                "total_throughput_mbps": total,
                "first_share": first_share,
                "second_share": second_share,
                "dominant_flow": row["first_tcp"] if first >= second else row["second_tcp"],
                "share_gap": abs(first_share - second_share),
                "jain_fairness": number(row, "jain_fairness_mean"),
                "queue_delay_ms": number(row, "queue_delay_mean_ms_mean"),
                "mark_count": number(row, "mark_count_mean"),
            }
        )
    return out


def fq_vs_codel_mixed(mixed_rows: list[dict[str, str]]) -> list[dict[str, object]]:
    grouped: dict[tuple[str, str, str], dict[str, dict[str, str]]] = defaultdict(dict)
    for row in mixed_rows:
        key = (row["first_tcp"], row["second_tcp"], row["base_rtt"])
        grouped[key][row["queue"]] = row

    out: list[dict[str, object]] = []
    for (first, second, rtt), by_queue in sorted(grouped.items()):
        if "codel" not in by_queue or "fq" not in by_queue:
            continue
        codel = by_queue["codel"]
        fq = by_queue["fq"]
        out.append(
            {
                "pair": f"{first}/{second}",
                "base_rtt": rtt,
                "jain_codel": number(codel, "jain_fairness_mean"),
                "jain_fq": number(fq, "jain_fairness_mean"),
                "jain_delta_fq_minus_codel": number(fq, "jain_fairness_mean")
                - number(codel, "jain_fairness_mean"),
                "queue_delay_codel_ms": number(codel, "queue_delay_mean_ms_mean"),
                "queue_delay_fq_ms": number(fq, "queue_delay_mean_ms_mean"),
                "queue_delay_delta_fq_minus_codel_ms": number(fq, "queue_delay_mean_ms_mean")
                - number(codel, "queue_delay_mean_ms_mean"),
                "total_throughput_codel_mbps": number(codel, "total_throughput_mean_mbps_mean"),
                "total_throughput_fq_mbps": number(fq, "total_throughput_mean_mbps_mean"),
                "total_throughput_delta_fq_minus_codel_mbps": number(
                    fq, "total_throughput_mean_mbps_mean"
                )
                - number(codel, "total_throughput_mean_mbps_mean"),
            }
        )
    return out


def audit_campaign(results_dir: Path, aggregate_path: Path, name: str) -> dict[str, object]:
    run_dirs = [path for path in sorted(results_dir.iterdir()) if path.is_dir()]
    metadata = []
    trace_counts = []
    missing_metadata = 0
    failed = 0
    for run_dir in run_dirs:
        metadata_path = run_dir / "metadata.json"
        if not metadata_path.exists():
            missing_metadata += 1
            continue
        data = json.loads(metadata_path.read_text(encoding="utf-8"))
        metadata.append(data)
        if data.get("returncode") != 0:
            failed += 1
        trace_counts.append(len(data.get("trace_files", [])))

    aggregate_rows = len(read_rows(aggregate_path)) if aggregate_path.exists() else 0
    return {
        "campaign": name,
        "results_dir": str(results_dir),
        "run_dirs": len(run_dirs),
        "metadata_files": len(metadata),
        "missing_metadata": missing_metadata,
        "successful_runs": len(metadata) - failed,
        "failed_runs": failed,
        "aggregate_rows": aggregate_rows,
        "min_trace_files": min(trace_counts) if trace_counts else 0,
        "max_trace_files": max(trace_counts) if trace_counts else 0,
    }


def aggregate_for_warmup(results_dir: Path, warmup: float) -> list[dict[str, object]]:
    rows = []
    for run_dir in sorted(results_dir.iterdir()):
        if run_dir.is_dir():
            row = analyze_tcp_aqm.summarize_run(run_dir, warmup)
            if row is not None:
                rows.append(row)
    return analyze_tcp_aqm.aggregate_rows(rows)


def warmup_sensitivity(
    single_results_dir: Path,
    mixed_results_dir: Path,
    single_warmups: list[float],
    mixed_warmups: list[float],
) -> tuple[list[dict[str, object]], list[dict[str, object]]]:
    rows: list[dict[str, object]] = []
    for campaign, results_dir, warmups in [
        ("single-flow", single_results_dir, single_warmups),
        ("mixed-flow", mixed_results_dir, mixed_warmups),
    ]:
        for warmup in warmups:
            for row in aggregate_for_warmup(results_dir, warmup):
                if campaign == "single-flow":
                    key = f"{row.get('first_tcp')}/{row.get('queue')}/{row.get('base_rtt')}/ecn{row.get('ecn')}"
                else:
                    key = (
                        f"{row.get('first_tcp')}-{row.get('second_tcp')}/"
                        f"{row.get('queue')}/{row.get('base_rtt')}"
                    )
                rows.append(
                    {
                        "campaign": campaign,
                        "warmup": warmup,
                        "config_key": key,
                        "first_tcp": row.get("first_tcp", ""),
                        "second_tcp": row.get("second_tcp", ""),
                        "queue": row.get("queue", ""),
                        "base_rtt": row.get("base_rtt", ""),
                        "ecn": row.get("ecn", ""),
                        "throughput_mbps": number(row, "throughput_mean_mbps_mean"),
                        "second_throughput_mbps": number(row, "second_throughput_mean_mbps_mean"),
                        "total_throughput_mbps": number(row, "total_throughput_mean_mbps_mean"),
                        "jain_fairness": number(row, "jain_fairness_mean"),
                        "queue_delay_ms": number(row, "queue_delay_mean_ms_mean"),
                    }
                )

    grouped: dict[tuple[str, str], list[dict[str, object]]] = defaultdict(list)
    for row in rows:
        grouped[(str(row["campaign"]), str(row["config_key"]))].append(row)

    delta_rows: list[dict[str, object]] = []
    for (campaign, key), group in sorted(grouped.items()):
        tputs = [number(row, "throughput_mbps") for row in group]
        total_tputs = [number(row, "total_throughput_mbps") for row in group]
        fairness = [number(row, "jain_fairness") for row in group]
        qdelay = [number(row, "queue_delay_ms") for row in group]
        tput_basis = [value for value in (total_tputs if campaign == "mixed-flow" else tputs) if not math.isnan(value)]
        fairness_clean = [value for value in fairness if not math.isnan(value)]
        qdelay_clean = [value for value in qdelay if not math.isnan(value)]
        delta_rows.append(
            {
                "campaign": campaign,
                "config_key": key,
                "warmups": ",".join(str(row["warmup"]) for row in group),
                "throughput_range_mbps": max(tput_basis) - min(tput_basis) if tput_basis else math.nan,
                "throughput_range_pct_of_max": (
                    100.0 * (max(tput_basis) - min(tput_basis)) / max(tput_basis)
                    if tput_basis and max(tput_basis)
                    else math.nan
                ),
                "fairness_range": max(fairness_clean) - min(fairness_clean) if fairness_clean else math.nan,
                "queue_delay_range_ms": max(qdelay_clean) - min(qdelay_clean) if qdelay_clean else math.nan,
            }
        )
    return rows, delta_rows


def trace_values(run_dir: Path, suffix: str, start: float, end: float) -> list[float]:
    path = analyze_tcp_aqm.trace_path(run_dir, suffix)
    return [value for time, value in analyze_tcp_aqm.parse_two_column(path, start) if time < end]


def stabilization_audit(results_dir: Path, campaign: str, warmup: float) -> list[dict[str, object]]:
    out: list[dict[str, object]] = []
    for run_dir in sorted(results_dir.iterdir()):
        if not run_dir.is_dir():
            continue
        metadata = analyze_tcp_aqm.read_metadata(run_dir)
        if not metadata:
            continue
        cfg = metadata.get("config", {})
        stop_time = parse_seconds(str(cfg.get("stop_time", "40s")))
        midpoint = warmup + (stop_time - warmup) * 0.5
        final_start = warmup + (stop_time - warmup) * 0.75
        for flow, suffix in [
            ("first", "first-tcp-throughput.dat"),
            ("second", "second-tcp-throughput.dat"),
        ]:
            if flow == "second" and not cfg.get("second_tcp", ""):
                continue
            previous = trace_values(run_dir, suffix, midpoint, final_start)
            final = trace_values(run_dir, suffix, final_start, stop_time)
            previous_mean = analyze_tcp_aqm.mean(previous)
            final_mean = analyze_tcp_aqm.mean(final)
            delta = pct_delta(final_mean, previous_mean)
            # Separate "unstable" from "no comparable data". Previously NaN
            # deltas were folded into the unstable bucket, which both overstated
            # instability and made the figure's NaN sort order arbitrary.
            if math.isnan(delta):
                stability_flag_gt_5pct = False
                missing_data = True
            else:
                stability_flag_gt_5pct = abs(delta) > 5
                missing_data = False
            out.append(
                {
                    "campaign": campaign,
                    "run_id": metadata.get("run_id", run_dir.name),
                    "flow": flow,
                    "first_tcp": cfg.get("first_tcp", ""),
                    "second_tcp": cfg.get("second_tcp", ""),
                    "queue": cfg.get("queue", ""),
                    "base_rtt": cfg.get("base_rtt", ""),
                    "ecn": int(bool(cfg.get("ecn", False))),
                    "previous_window_mean_mbps": previous_mean,
                    "final_window_mean_mbps": final_mean,
                    "final_vs_previous_delta_pct": delta,
                    "stability_flag_gt_5pct": stability_flag_gt_5pct,
                    "missing_data": missing_data,
                }
            )
    return out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--single-aggregate", type=Path, default=None)
    parser.add_argument("--mixed-aggregate", type=Path, default=None)
    parser.add_argument("--single-results-dir", type=Path, default=None)
    parser.add_argument("--mixed-results-dir", type=Path, default=None)
    parser.add_argument("--output-dir", type=Path, default=None)
    parser.add_argument("--skip-warmup", action="store_true")
    args = parser.parse_args()

    repo = repo_root_from_script()
    root = repo / "paper" / "icns3-2026-tcp-aqm"
    single_aggregate = args.single_aggregate or root / "results" / "summary-aggregate.csv"
    mixed_aggregate = args.mixed_aggregate or root / "results-mixed" / "summary-aggregate.csv"
    single_results_dir = args.single_results_dir or root / "results"
    mixed_results_dir = args.mixed_results_dir or root / "results-mixed"
    output_dir = args.output_dir or root / "evaluation"
    output_dir.mkdir(parents=True, exist_ok=True)

    single_rows = read_rows(single_aggregate)
    mixed_rows = read_rows(mixed_aggregate)

    outputs = {
        "ecn-impact.csv": ecn_impact(single_rows),
        "single-flow-rtt-sensitivity.csv": rtt_sensitivity(single_rows),
        "ranking-stability.csv": ranking_stability(single_rows),
        "pareto-frontier.csv": pareto_frontier(single_rows),
        "mixed-flow-share.csv": mixed_flow_share(mixed_rows),
        "mixed-fq-vs-codel.csv": fq_vs_codel_mixed(mixed_rows),
        "campaign-audit.csv": [
            audit_campaign(single_results_dir, single_aggregate, "single-flow"),
            audit_campaign(mixed_results_dir, mixed_aggregate, "mixed-flow"),
        ],
        "stabilization-audit.csv": stabilization_audit(single_results_dir, "single-flow", 10.0)
        + stabilization_audit(mixed_results_dir, "mixed-flow", 20.0),
    }

    if not args.skip_warmup:
        warmup_rows, warmup_delta_rows = warmup_sensitivity(
            single_results_dir,
            mixed_results_dir,
            [5.0, 10.0, 20.0, 30.0],
            [15.0, 20.0, 25.0, 30.0],
        )
        outputs["warmup-sensitivity.csv"] = warmup_rows
        outputs["warmup-sensitivity-delta.csv"] = warmup_delta_rows

    for name, rows in outputs.items():
        write_rows(output_dir / name, rows)
        print(f"wrote {len(rows)} rows to {output_dir / name}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
