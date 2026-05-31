#!/usr/bin/env python3
"""Create simple SVG figures from the TCP/AQM summary CSV without dependencies."""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path


COLORS = {
    "cubic": "#2f6fbb",
    "reno": "#408b5a",
    "dctcp": "#b75f2a",
}


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[3]


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def number(row: dict[str, str], key: str) -> float:
    try:
        value = float(row[key])
    except (KeyError, ValueError):
        return math.nan
    return value


def label(row: dict[str, str]) -> str:
    ecn = "ECN" if row.get("ecn") == "1" else "no ECN"
    return f"{row.get('first_tcp')} {row.get('queue')} {row.get('base_rtt')} {ecn}"


def svg_escape(text: str) -> str:
    return (
        text.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def write_bar_chart(
    rows: list[dict[str, str]],
    metric: str,
    title: str,
    y_label: str,
    output: Path,
) -> None:
    values = [number(row, metric) for row in rows]
    clean_values = [value for value in values if not math.isnan(value)]
    if not clean_values:
        return

    margin_left = 72
    margin_right = 28
    margin_top = 48
    margin_bottom = 118
    bar_width = 26
    gap = 12
    width = margin_left + margin_right + len(rows) * (bar_width + gap)
    height = 360
    plot_height = height - margin_top - margin_bottom
    plot_width = width - margin_left - margin_right
    max_value = max(clean_values) * 1.12
    max_value = max_value if max_value > 0 else 1

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        "<style>",
        "text{font-family:Arial,Helvetica,sans-serif;fill:#222}",
        ".title{font-size:16px;font-weight:700}",
        ".axis{stroke:#222;stroke-width:1}",
        ".grid{stroke:#ddd;stroke-width:1}",
        ".tick{font-size:10px;fill:#555}",
        ".label{font-size:9px;fill:#333}",
        "</style>",
        f'<text class="title" x="{width / 2:.1f}" y="24" text-anchor="middle">{svg_escape(title)}</text>',
        f'<text class="tick" transform="translate(16 {margin_top + plot_height / 2:.1f}) rotate(-90)" text-anchor="middle">{svg_escape(y_label)}</text>',
    ]

    for tick in range(0, 6):
        value = max_value * tick / 5
        y = margin_top + plot_height - (value / max_value) * plot_height
        parts.append(f'<line class="grid" x1="{margin_left}" y1="{y:.1f}" x2="{margin_left + plot_width}" y2="{y:.1f}"/>')
        parts.append(f'<text class="tick" x="{margin_left - 8}" y="{y + 3:.1f}" text-anchor="end">{value:.1f}</text>')

    parts.append(f'<line class="axis" x1="{margin_left}" y1="{margin_top}" x2="{margin_left}" y2="{margin_top + plot_height}"/>')
    parts.append(f'<line class="axis" x1="{margin_left}" y1="{margin_top + plot_height}" x2="{margin_left + plot_width}" y2="{margin_top + plot_height}"/>')

    for index, row in enumerate(rows):
        value = number(row, metric)
        if math.isnan(value):
            continue
        x = margin_left + index * (bar_width + gap) + gap / 2
        bar_height = (value / max_value) * plot_height
        y = margin_top + plot_height - bar_height
        color = COLORS.get(row.get("first_tcp", ""), "#555")
        parts.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{bar_width}" height="{bar_height:.1f}" fill="{color}"/>')
        parts.append(f'<text class="tick" x="{x + bar_width / 2:.1f}" y="{y - 4:.1f}" text-anchor="middle">{value:.1f}</text>')

        words = label(row).split()
        lx = x + bar_width / 2
        ly = margin_top + plot_height + 14
        parts.append(f'<text class="label" transform="translate({lx:.1f} {ly:.1f}) rotate(62)" text-anchor="start">')
        parts.append(svg_escape(" ".join(words)))
        parts.append("</text>")

    parts.append("</svg>")
    output.write_text("\n".join(parts) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--summary", type=Path, default=None)
    parser.add_argument("--figures-dir", type=Path, default=None)
    parser.add_argument("--include-50ms", action="store_true")
    args = parser.parse_args()

    repo = repo_root_from_script()
    results_root = repo / "contrib" / "tcp-aqm-config" / "results"
    summary = args.summary or results_root / "single-flow" / "summary-aggregate.csv"
    figures_dir = args.figures_dir or results_root / "figures" / "inspection"
    figures_dir.mkdir(parents=True, exist_ok=True)

    rows = read_rows(summary)
    metric_suffix = "_mean" if rows and "throughput_mean_mbps_mean" in rows[0] else ""
    rtts = {"10ms", "50ms", "80ms"} if args.include_50ms else {"10ms", "80ms"}
    rows = [
        row
        for row in rows
        if row.get("ecn") == "1" and row.get("base_rtt") in rtts
    ]
    rows.sort(key=lambda row: (row.get("base_rtt", ""), row.get("first_tcp", ""), row.get("queue", ""), row.get("ecn", "")))

    write_bar_chart(
        rows,
        f"throughput_mean_mbps{metric_suffix}",
        "Mean TCP Throughput in Full Campaign",
        "Throughput (Mbit/s)",
        figures_dir / "throughput-full.svg",
    )
    write_bar_chart(
        rows,
        f"queue_delay_mean_ms{metric_suffix}",
        "Mean Bottleneck Queueing Delay in Full Campaign",
        "Queue delay (ms)",
        figures_dir / "queue-delay-full.svg",
    )
    print(f"wrote figures to {figures_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
