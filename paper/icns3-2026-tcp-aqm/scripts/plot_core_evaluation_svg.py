#!/usr/bin/env python3
"""Create publication-style SVG figures for the TCP/AQM core evaluation.

The plotter stays dependency-free so reviewers can regenerate figures from a
plain Python installation. It reads the derived CSV files produced by
analyze_core_evaluation.py and writes SVG figures with axes, units, legends,
and stable sizing.

Design goals
------------
- A single shared style block (typography, colours, grid, padding) is reused
  by every figure so they look consistent in the paper.
- Colours come from a colourblind-safe palette and have stable role-based
  meanings (TCP family, queue family, signed delta).
- Reusable primitives draw axes, ticks, legends, and bar/scatter glyphs so
  every chart shares the same proportions and visual weight.
- Horizontal bar charts share margins and row heights so a reader can compare
  bars across figures without re-calibrating.
"""

from __future__ import annotations

import argparse
import csv
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable


# ---------------------------------------------------------------------------
# Palette and typography
# ---------------------------------------------------------------------------

# Okabe-Ito-derived palette: colourblind safe and prints reasonably.
TCP_COLORS = {
    "cubic": "#0072B2",      # blue
    "reno": "#009E73",       # bluish green
    "dctcp": "#D55E00",      # vermillion
}

QUEUE_COLORS = {
    "codel": "#4C6FA1",      # slate blue
    "fq":    "#7B5EA7",      # muted purple
    "pie":   "#C97D2A",      # amber
    "red":   "#B44B5C",      # rose
}

POSITIVE = "#D55E00"   # vermillion — "ECN/FQ made it worse" direction
NEGATIVE = "#009E73"   # bluish green — "better" direction
NEUTRAL  = "#6B7280"   # slate

TEXT_DARK  = "#1F2933"
TEXT_MID   = "#52606D"
GRID       = "#E4E7EB"
AXIS       = "#3E4C59"

# Common dimensions. Every horizontal bar chart uses the same width and
# margins so figures line up cleanly in the paper.
BAR_WIDTH         = 880
BAR_ROW_HEIGHT    = 22
BAR_MARGIN_LEFT   = 240
BAR_MARGIN_RIGHT  = 120
BAR_MARGIN_TOP    = 64
BAR_MARGIN_BOTTOM = 56


# ---------------------------------------------------------------------------
# Tiny SVG helpers
# ---------------------------------------------------------------------------


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[3]


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def number(row: dict[str, object], key: str, default: float = math.nan) -> float:
    try:
        return float(row.get(key, default))
    except (TypeError, ValueError):
        return default


def svg_escape(text: object) -> str:
    return (
        str(text)
        .replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def fmt_value(value: float, digits: int = 2) -> str:
    if math.isnan(value):
        return "n/a"
    if abs(value) >= 100:
        return f"{value:.0f}"
    if abs(value) >= 10:
        return f"{value:.1f}"
    return f"{value:.{digits}f}"


def nice_domain(values: Iterable[float], signed: bool) -> tuple[float, float]:
    clean = [value for value in values if not math.isnan(value)]
    if not clean:
        return (0.0, 1.0)
    if signed:
        bound = max(abs(min(clean)), abs(max(clean)))
        if bound == 0:
            bound = 1.0
        return (-bound * 1.12, bound * 1.12)
    upper = max(clean)
    if upper <= 0:
        upper = 1.0
    return (0.0, upper * 1.12)


def axis_ticks(min_value: float, max_value: float, count: int = 5) -> list[float]:
    if max_value == min_value:
        return [min_value]
    return [min_value + (max_value - min_value) * idx / count for idx in range(count + 1)]


def signed_color(value: float) -> str:
    if math.isnan(value) or value == 0:
        return NEUTRAL
    return POSITIVE if value > 0 else NEGATIVE


def color_for_tcp(name: str) -> str:
    return TCP_COLORS.get(name, NEUTRAL)


def color_for_queue(name: str) -> str:
    return QUEUE_COLORS.get(name, NEUTRAL)


def svg_header(width: int, height: int) -> list[str]:
    """Open the SVG element and emit the shared style block."""
    return [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}">',
        "<style>",
        "  text { font-family: 'Helvetica Neue', Helvetica, Arial, sans-serif; "
        f"fill: {TEXT_DARK}; }}",
        "  .title       { font-size: 15px; font-weight: 600; }",
        f"  .subtitle    {{ font-size: 11px; fill: {TEXT_MID}; }}",
        "  .axis-label  { font-size: 11px; }",
        f"  .tick        {{ font-size: 10px; fill: {TEXT_MID}; }}",
        "  .row-label   { font-size: 11px; }",
        f"  .value       {{ font-size: 10px; fill: {TEXT_DARK}; }}",
        f"  .extra       {{ font-size: 10px; fill: {TEXT_MID}; }}",
        "  .legend      { font-size: 10px; }",
        f"  .grid        {{ stroke: {GRID}; stroke-width: 1; shape-rendering: crispEdges; }}",
        f"  .axis-line   {{ stroke: {AXIS}; stroke-width: 1; shape-rendering: crispEdges; }}",
        f"  .zero-line   {{ stroke: {TEXT_DARK}; stroke-width: 1.2; "
        "shape-rendering: crispEdges; }",
        "  .bar         { stroke: none; }",
        "</style>",
        f'<rect x="0" y="0" width="{width}" height="{height}" fill="#FFFFFF"/>',
    ]


def add_title(parts: list[str], width: int, title: str, subtitle: str | None = None) -> None:
    parts.append(
        f'<text class="title" x="{width / 2:.1f}" y="26" text-anchor="middle">'
        f"{svg_escape(title)}</text>"
    )
    if subtitle:
        parts.append(
            f'<text class="subtitle" x="{width / 2:.1f}" y="44" text-anchor="middle">'
            f"{svg_escape(subtitle)}</text>"
        )


def estimate_legend_gap(entries: list[tuple[str, str]]) -> float:
    """Pick a column gap wide enough for the longest label in this legend.

    Glyph widths at 10 px Helvetica are roughly ~6 px each; a swatch plus
    padding adds another ~22 px. We round up to keep some breathing room.
    """
    glyph_px = 6.2
    swatch_pad_px = 24.0
    min_gap = 60.0
    longest = max((len(label) for label, _ in entries), default=0)
    return max(min_gap, swatch_pad_px + longest * glyph_px + 12.0)


def draw_legend(
    parts: list[str],
    x: float,
    y: float,
    entries: list[tuple[str, str]],
    *,
    swatch: str = "rect",
    column_gap: float | None = None,
) -> None:
    """Draw a single-row legend at (x, y) with coloured swatches.

    column_gap defaults to a width derived from the longest label so labels
    never overlap the next swatch. Pass an explicit value to override.
    """
    if column_gap is None:
        column_gap = estimate_legend_gap(entries)
    cursor = x
    for label, color in entries:
        if swatch == "circle":
            parts.append(
                f'<circle cx="{cursor + 5:.1f}" cy="{y - 4:.1f}" r="5" fill="{color}"/>'
            )
        else:
            parts.append(
                f'<rect x="{cursor:.1f}" y="{y - 10:.1f}" width="12" height="12" '
                f'fill="{color}"/>'
            )
        parts.append(
            f'<text class="legend" x="{cursor + 18:.1f}" y="{y:.1f}">{svg_escape(label)}</text>'
        )
        cursor += column_gap


# ---------------------------------------------------------------------------
# Bar-chart primitive — every horizontal bar chart in the paper uses this.
# ---------------------------------------------------------------------------


@dataclass
class BarChart:
    rows: list[dict[str, str]]
    value_key: str
    label_fn: Callable[[dict[str, str]], str]
    title: str
    x_label: str
    output: Path
    signed: bool = False
    color_fn: Callable[[dict[str, str]], str] | None = None
    extra_label_fn: Callable[[dict[str, str]], str] | None = None
    max_rows: int | None = None
    legend: list[tuple[str, str]] | None = None
    legend_swatch: str = "rect"
    subtitle: str | None = None


@dataclass
class BarGroup:
    """One labelled section of a grouped bar chart.

    A group has its own top-N truncation and its own header row, so a single
    chart can show "top 12 of 60 from campaign A" and "top 6 of 12 from
    campaign B" side by side at consistent heights.
    """

    label: str
    rows: list[dict[str, str]]
    max_rows: int | None = None
    total_override: int | None = None

    def displayed_rows(self) -> tuple[list[dict[str, str]], bool, int]:
        total = self.total_override if self.total_override is not None else len(self.rows)
        if self.max_rows is not None and len(self.rows) > self.max_rows:
            return self.rows[: self.max_rows], True, total
        return self.rows, False, total


def write_horizontal_bar_chart(chart: BarChart) -> None:
    rows = chart.rows
    total_rows = len(rows)
    truncated = chart.max_rows is not None and total_rows > chart.max_rows
    if truncated:
        rows = rows[: chart.max_rows]

    values = [number(row, chart.value_key) for row in rows]
    clean_values = [value for value in values if not math.isnan(value)]
    if not clean_values:
        return

    min_value, max_value = nice_domain(clean_values, chart.signed)

    legend_pad = 24 if chart.legend else 0
    truncation_pad = 14 if truncated else 0
    width = BAR_WIDTH
    row_height = BAR_ROW_HEIGHT
    margin_left = BAR_MARGIN_LEFT
    margin_right = BAR_MARGIN_RIGHT
    margin_top = BAR_MARGIN_TOP + legend_pad
    margin_bottom = BAR_MARGIN_BOTTOM + truncation_pad
    height = margin_top + margin_bottom + row_height * len(rows)
    plot_width = width - margin_left - margin_right
    zero = 0.0 if chart.signed else min_value

    def xpos(value: float) -> float:
        return margin_left + (value - min_value) / (max_value - min_value) * plot_width

    parts = svg_header(width, height)
    add_title(parts, width, chart.title, subtitle=chart.subtitle)

    if chart.legend:
        legend_x = margin_left
        legend_y = margin_top - 18
        draw_legend(parts, legend_x, legend_y, chart.legend, swatch=chart.legend_swatch)

    baseline_y = margin_top + row_height * len(rows)

    # Grid + ticks
    for tick in axis_ticks(min_value, max_value):
        x = xpos(tick)
        parts.append(
            f'<line class="grid" x1="{x:.1f}" y1="{margin_top - 4:.1f}" '
            f'x2="{x:.1f}" y2="{baseline_y:.1f}"/>'
        )
        parts.append(
            f'<text class="tick" x="{x:.1f}" y="{baseline_y + 14:.1f}" '
            f'text-anchor="middle">{fmt_value(tick)}</text>'
        )

    # Zero line for signed charts so the polarity is obvious.
    if chart.signed:
        zero_x = xpos(0)
        parts.append(
            f'<line class="zero-line" x1="{zero_x:.1f}" y1="{margin_top - 4:.1f}" '
            f'x2="{zero_x:.1f}" y2="{baseline_y:.1f}"/>'
        )

    # Bottom axis and x-axis label
    parts.append(
        f'<line class="axis-line" x1="{margin_left}" y1="{baseline_y:.1f}" '
        f'x2="{margin_left + plot_width}" y2="{baseline_y:.1f}"/>'
    )
    parts.append(
        f'<text class="axis-label tick" x="{margin_left + plot_width / 2:.1f}" '
        f'y="{height - 22:.1f}" text-anchor="middle">{svg_escape(chart.x_label)}</text>'
    )

    for idx, row in enumerate(rows):
        value = number(row, chart.value_key)
        if math.isnan(value):
            continue
        y = margin_top + idx * row_height
        text_y = y + 15
        parts.append(
            f'<text class="row-label" x="{margin_left - 10:.1f}" y="{text_y:.1f}" '
            f'text-anchor="end">{svg_escape(chart.label_fn(row))}</text>'
        )
        x0 = xpos(min(value, zero))
        x1 = xpos(max(value, zero))
        bar_width = max(1.0, x1 - x0)
        color = chart.color_fn(row) if chart.color_fn else signed_color(value)
        parts.append(
            f'<rect class="bar" x="{x0:.1f}" y="{y + 4:.1f}" width="{bar_width:.1f}" '
            f'height="{row_height - 8}" fill="{color}"/>'
        )
        value_anchor = "start" if value >= zero else "end"
        value_x = x1 + 6 if value >= zero else x0 - 6
        parts.append(
            f'<text class="value" x="{value_x:.1f}" y="{text_y:.1f}" '
            f'text-anchor="{value_anchor}">{fmt_value(value)}</text>'
        )
        if chart.extra_label_fn:
            parts.append(
                f'<text class="extra" x="{width - 18:.1f}" y="{text_y:.1f}" '
                f'text-anchor="end">{svg_escape(chart.extra_label_fn(row))}</text>'
            )

    if truncated:
        parts.append(
            f'<text class="extra" x="{width / 2:.1f}" y="{height - 6:.1f}" '
            f'text-anchor="middle">'
            f"Showing top {len(rows)} of {total_rows} rows by magnitude."
            "</text>"
        )

    parts.append("</svg>")
    chart.output.write_text("\n".join(parts) + "\n", encoding="utf-8")


# ---------------------------------------------------------------------------
# Grouped bar chart — used by per-campaign audit figures so both legend
# entries always have visible bars and the chart height stays stable across
# reruns regardless of how the underlying ranking shifts.
# ---------------------------------------------------------------------------


GROUP_HEADER_HEIGHT = 22
GROUP_GAP = 10


def write_grouped_horizontal_bar_chart(
    groups: list[BarGroup],
    *,
    value_key: str,
    label_fn: Callable[[dict[str, str]], str],
    title: str,
    x_label: str,
    output: Path,
    signed: bool = False,
    color_fn: Callable[[dict[str, str]], str] | None = None,
    extra_label_fn: Callable[[dict[str, str]], str] | None = None,
    legend: list[tuple[str, str]] | None = None,
    legend_swatch: str = "rect",
    subtitle: str | None = None,
) -> None:
    # Pre-compute display rows and per-group metadata.
    sections: list[tuple[BarGroup, list[dict[str, str]], bool, int]] = []
    all_values: list[float] = []
    for group in groups:
        display_rows, truncated, total = group.displayed_rows()
        if not display_rows:
            continue
        sections.append((group, display_rows, truncated, total))
        all_values.extend(number(row, value_key) for row in display_rows)

    clean = [value for value in all_values if not math.isnan(value)]
    if not clean:
        return

    min_value, max_value = nice_domain(clean, signed)

    total_data_rows = sum(len(rows) for _, rows, _, _ in sections)
    total_headers = len(sections)
    total_gaps = max(0, len(sections) - 1)

    legend_pad = 24 if legend else 0
    width = BAR_WIDTH
    row_height = BAR_ROW_HEIGHT
    margin_left = BAR_MARGIN_LEFT
    margin_right = BAR_MARGIN_RIGHT
    margin_top = BAR_MARGIN_TOP + legend_pad
    margin_bottom = BAR_MARGIN_BOTTOM
    body_height = (
        total_data_rows * row_height
        + total_headers * GROUP_HEADER_HEIGHT
        + total_gaps * GROUP_GAP
    )
    height = margin_top + margin_bottom + body_height
    plot_width = width - margin_left - margin_right
    zero = 0.0 if signed else min_value

    def xpos(value: float) -> float:
        return margin_left + (value - min_value) / (max_value - min_value) * plot_width

    parts = svg_header(width, height)
    add_title(parts, width, title, subtitle=subtitle)

    if legend:
        draw_legend(parts, margin_left, margin_top - 18, legend, swatch=legend_swatch)

    baseline_y = margin_top + body_height

    # Grid + ticks span the whole plot body so both groups share an axis.
    for tick in axis_ticks(min_value, max_value):
        x = xpos(tick)
        parts.append(
            f'<line class="grid" x1="{x:.1f}" y1="{margin_top - 4:.1f}" '
            f'x2="{x:.1f}" y2="{baseline_y:.1f}"/>'
        )
        parts.append(
            f'<text class="tick" x="{x:.1f}" y="{baseline_y + 14:.1f}" '
            f'text-anchor="middle">{fmt_value(tick)}</text>'
        )

    if signed:
        zero_x = xpos(0)
        parts.append(
            f'<line class="zero-line" x1="{zero_x:.1f}" y1="{margin_top - 4:.1f}" '
            f'x2="{zero_x:.1f}" y2="{baseline_y:.1f}"/>'
        )

    parts.append(
        f'<line class="axis-line" x1="{margin_left}" y1="{baseline_y:.1f}" '
        f'x2="{margin_left + plot_width}" y2="{baseline_y:.1f}"/>'
    )
    parts.append(
        f'<text class="axis-label tick" x="{margin_left + plot_width / 2:.1f}" '
        f'y="{height - 22:.1f}" text-anchor="middle">{svg_escape(x_label)}</text>'
    )

    cursor_y = margin_top
    for section_idx, (group, display_rows, truncated, total) in enumerate(sections):
        if section_idx > 0:
            cursor_y += GROUP_GAP

        # Group header: bold label on the left, count summary on the right.
        header_y = cursor_y
        header_text_y = header_y + 14
        rule_y = header_y + GROUP_HEADER_HEIGHT - 4
        count_text = (
            f"top {len(display_rows)} of {total}" if truncated else f"all {total}"
        )
        parts.append(
            f'<text class="row-label" style="font-weight:600" '
            f'x="{margin_left - 10:.1f}" y="{header_text_y:.1f}" text-anchor="end">'
            f"{svg_escape(group.label)}</text>"
        )
        parts.append(
            f'<text class="extra" x="{margin_left + 4:.1f}" y="{header_text_y:.1f}">'
            f"{svg_escape(count_text)}</text>"
        )
        parts.append(
            f'<line class="grid" x1="{margin_left:.1f}" y1="{rule_y:.1f}" '
            f'x2="{margin_left + plot_width:.1f}" y2="{rule_y:.1f}"/>'
        )
        cursor_y += GROUP_HEADER_HEIGHT

        for row in display_rows:
            value = number(row, value_key)
            if math.isnan(value):
                cursor_y += row_height
                continue
            text_y = cursor_y + 15
            parts.append(
                f'<text class="row-label" x="{margin_left - 10:.1f}" y="{text_y:.1f}" '
                f'text-anchor="end">{svg_escape(label_fn(row))}</text>'
            )
            x0 = xpos(min(value, zero))
            x1 = xpos(max(value, zero))
            bar_width = max(1.0, x1 - x0)
            color = color_fn(row) if color_fn else signed_color(value)
            parts.append(
                f'<rect class="bar" x="{x0:.1f}" y="{cursor_y + 4:.1f}" '
                f'width="{bar_width:.1f}" height="{row_height - 8}" fill="{color}"/>'
            )
            value_anchor = "start" if value >= zero else "end"
            value_x = x1 + 6 if value >= zero else x0 - 6
            parts.append(
                f'<text class="value" x="{value_x:.1f}" y="{text_y:.1f}" '
                f'text-anchor="{value_anchor}">{fmt_value(value)}</text>'
            )
            if extra_label_fn:
                parts.append(
                    f'<text class="extra" x="{width - 18:.1f}" y="{text_y:.1f}" '
                    f'text-anchor="end">{svg_escape(extra_label_fn(row))}</text>'
                )
            cursor_y += row_height

    parts.append("</svg>")
    output.write_text("\n".join(parts) + "\n", encoding="utf-8")


# ---------------------------------------------------------------------------
# Specialty charts
# ---------------------------------------------------------------------------


def write_mixed_share(rows: list[dict[str, str]], output: Path) -> None:
    rows = sorted(rows, key=lambda row: (row["pair"], row["base_rtt"], row["queue"]))
    width = 920
    row_height = 26
    margin_left = 200
    margin_right = 210
    margin_top = 84
    margin_bottom = 56
    height = margin_top + margin_bottom + row_height * len(rows)
    plot_width = width - margin_left - margin_right
    midline_x = margin_left + plot_width / 2

    parts = svg_header(width, height)
    add_title(
        parts,
        width,
        "Mixed-Flow Throughput Share",
        "Stacked share of bottleneck goodput. Right column reports total throughput and Jain fairness.",
    )

    # Legend just under the title
    legend_entries = [(name, color) for name, color in TCP_COLORS.items()]
    draw_legend(parts, margin_left, margin_top - 16, legend_entries)

    # Equal-share marker
    parts.append(
        f'<line class="grid" x1="{midline_x:.1f}" y1="{margin_top - 4:.1f}" '
        f'x2="{midline_x:.1f}" y2="{margin_top + row_height * len(rows):.1f}"/>'
    )
    parts.append(
        f'<text class="extra" x="{midline_x:.1f}" y="{height - 22:.1f}" '
        f'text-anchor="middle">equal share (50%)</text>'
    )

    for idx, row in enumerate(rows):
        y = margin_top + idx * row_height
        text_y = y + 17
        label = f"{row['pair']} • {row['queue']} • {row['base_rtt']}"
        first_share = number(row, "first_share")
        second_share = number(row, "second_share")
        if math.isnan(first_share) or math.isnan(second_share):
            continue
        first_width = plot_width * first_share
        second_width = plot_width * second_share
        first_color = color_for_tcp(row["first_tcp"])
        second_color = color_for_tcp(row["second_tcp"])
        parts.append(
            f'<text class="row-label" x="{margin_left - 10:.1f}" y="{text_y:.1f}" '
            f'text-anchor="end">{svg_escape(label)}</text>'
        )
        parts.append(
            f'<rect class="bar" x="{margin_left:.1f}" y="{y + 5:.1f}" '
            f'width="{first_width:.1f}" height="{row_height - 10}" fill="{first_color}"/>'
        )
        parts.append(
            f'<rect class="bar" x="{margin_left + first_width:.1f}" y="{y + 5:.1f}" '
            f'width="{second_width:.1f}" height="{row_height - 10}" fill="{second_color}"/>'
        )
        if first_width > 36:
            parts.append(
                f'<text class="value" x="{margin_left + first_width / 2:.1f}" '
                f'y="{text_y:.1f}" text-anchor="middle" style="fill:#FFFFFF;font-weight:600;">'
                f"{first_share * 100:.0f}%</text>"
            )
        if second_width > 36:
            parts.append(
                f'<text class="value" x="{margin_left + first_width + second_width / 2:.1f}" '
                f'y="{text_y:.1f}" text-anchor="middle" style="fill:#FFFFFF;font-weight:600;">'
                f"{second_share * 100:.0f}%</text>"
            )
        detail = (
            f"total {fmt_value(number(row, 'total_throughput_mbps'))} Mb/s, "
            f"Jain {fmt_value(number(row, 'jain_fairness'), 3)}"
        )
        parts.append(
            f'<text class="extra" x="{margin_left + plot_width + 12:.1f}" '
            f'y="{text_y:.1f}">{svg_escape(detail)}</text>'
        )

    parts.append("</svg>")
    output.write_text("\n".join(parts) + "\n", encoding="utf-8")


def write_throughput_delay_scatter(
    single_rows: list[dict[str, str]],
    frontier_rows: list[dict[str, str]],
    output: Path,
) -> None:
    rows = [row for row in single_rows if row.get("ecn") == "1"]
    width = 880
    height = 500
    margin_left = 78
    margin_right = 28
    margin_top = 86
    margin_bottom = 64
    plot_width = width - margin_left - margin_right
    plot_height = height - margin_top - margin_bottom
    x_values = [number(row, "throughput_mean_mbps_mean") for row in rows]
    y_values = [number(row, "queue_delay_mean_ms_mean") for row in rows]
    x_min = min(x_values) * 0.92
    x_max = max(x_values) * 1.04
    y_min = 0.0
    y_max = max(y_values) * 1.18
    frontier_keys = {
        (row["tcp"], row["queue"], row["base_rtt"], row["ecn"])
        for row in frontier_rows
        if row.get("ecn") == "1"
    }

    def xpos(value: float) -> float:
        return margin_left + (value - x_min) / (x_max - x_min) * plot_width

    def ypos(value: float) -> float:
        return margin_top + plot_height - (value - y_min) / (y_max - y_min) * plot_height

    parts = svg_header(width, height)
    add_title(
        parts,
        width,
        "Single-Flow Throughput vs. Queueing Delay",
        "ECN-enabled configurations. Larger markers are non-dominated within their RTT group. "
        "X-axis is truncated — spread between points is visually exaggerated.",
    )

    # Legends (TCP family on the left, queue stroke on the right)
    tcp_entries = [(name, color) for name, color in TCP_COLORS.items()]
    queue_entries = [(name, color) for name, color in QUEUE_COLORS.items()]
    draw_legend(parts, margin_left + 6, margin_top - 12, tcp_entries, swatch="circle")
    draw_legend(parts, margin_left + 6 + 290, margin_top - 12, queue_entries, swatch="rect")

    # Grid + tick labels
    for tick in axis_ticks(x_min, x_max):
        x = xpos(tick)
        parts.append(
            f'<line class="grid" x1="{x:.1f}" y1="{margin_top:.1f}" '
            f'x2="{x:.1f}" y2="{margin_top + plot_height:.1f}"/>'
        )
        parts.append(
            f'<text class="tick" x="{x:.1f}" y="{height - 42:.1f}" '
            f'text-anchor="middle">{fmt_value(tick)}</text>'
        )
    for tick in axis_ticks(y_min, y_max):
        y = ypos(tick)
        parts.append(
            f'<line class="grid" x1="{margin_left:.1f}" y1="{y:.1f}" '
            f'x2="{margin_left + plot_width:.1f}" y2="{y:.1f}"/>'
        )
        parts.append(
            f'<text class="tick" x="{margin_left - 8:.1f}" y="{y + 3:.1f}" '
            f'text-anchor="end">{fmt_value(tick)}</text>'
        )

    parts.append(
        f'<line class="axis-line" x1="{margin_left}" y1="{margin_top + plot_height:.1f}" '
        f'x2="{margin_left + plot_width:.1f}" y2="{margin_top + plot_height:.1f}"/>'
    )
    parts.append(
        f'<line class="axis-line" x1="{margin_left}" y1="{margin_top:.1f}" '
        f'x2="{margin_left}" y2="{margin_top + plot_height:.1f}"/>'
    )
    parts.append(
        f'<text class="axis-label tick" x="{margin_left + plot_width / 2:.1f}" '
        f'y="{height - 20:.1f}" text-anchor="middle">Throughput (Mbit/s)</text>'
    )
    parts.append(
        f'<text class="axis-label tick" transform="translate(22 {margin_top + plot_height / 2:.1f}) '
        f'rotate(-90)" text-anchor="middle">Mean queueing delay (ms)</text>'
    )

    for row in rows:
        tcp = row["first_tcp"]
        key = (tcp, row["queue"], row["base_rtt"], row["ecn"])
        frontier = key in frontier_keys
        radius = 6.5 if frontier else 4.0
        x = xpos(number(row, "throughput_mean_mbps_mean"))
        y = ypos(number(row, "queue_delay_mean_ms_mean"))
        fill = color_for_tcp(tcp)
        stroke = color_for_queue(row["queue"])
        parts.append(
            f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{radius}" fill="{fill}" '
            f'stroke="{stroke}" stroke-width="1.6" opacity="0.92"/>'
        )
        if frontier:
            label = f"{tcp}/{row['queue']}/{row['base_rtt']}"
            parts.append(
                f'<text class="extra" x="{x + 9:.1f}" y="{y - 7:.1f}">{svg_escape(label)}</text>'
            )

    parts.append("</svg>")
    output.write_text("\n".join(parts) + "\n", encoding="utf-8")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--evaluation-dir", type=Path, default=None)
    parser.add_argument("--single-aggregate", type=Path, default=None)
    parser.add_argument("--figures-dir", type=Path, default=None)
    args = parser.parse_args()

    repo = repo_root_from_script()
    root = repo / "paper" / "icns3-2026-tcp-aqm"
    evaluation_dir = args.evaluation_dir or root / "evaluation"
    single_aggregate = args.single_aggregate or root / "results" / "summary-aggregate.csv"
    figures_dir = args.figures_dir or root / "figures" / "core-eval"
    figures_dir.mkdir(parents=True, exist_ok=True)

    # ECN — queue-delay delta
    ecn_rows = read_rows(evaluation_dir / "ecn-impact.csv")
    ecn_rows = sorted(
        ecn_rows, key=lambda row: abs(number(row, "queue_delay_delta_ms")), reverse=True
    )
    write_horizontal_bar_chart(
        BarChart(
            rows=ecn_rows,
            value_key="queue_delay_delta_ms",
            label_fn=lambda row: f"{row['tcp']} • {row['queue']} • {row['base_rtt']}",
            title="Queue Delay Change When Enabling ECN",
            x_label="ECN minus non-ECN mean queue delay (ms)",
            output=figures_dir / "ecn-queue-delay-delta.svg",
            signed=True,
            legend=[("ECN improved delay (lower)", NEGATIVE),
                    ("ECN worsened delay (higher)", POSITIVE)],
            extra_label_fn=lambda row: f"Δthroughput {fmt_value(number(row, 'throughput_delta_pct'))}%",
        )
    )

    # RTT throughput dispersion
    rtt_rows = [
        row
        for row in read_rows(evaluation_dir / "single-flow-rtt-sensitivity.csv")
        if row.get("ecn") == "1"
    ]
    rtt_rows = sorted(
        rtt_rows, key=lambda row: number(row, "throughput_range_pct_of_max"), reverse=True
    )
    write_horizontal_bar_chart(
        BarChart(
            rows=rtt_rows,
            value_key="throughput_range_pct_of_max",
            label_fn=lambda row: f"{row['tcp']} • {row['queue']}",
            title="Throughput Dispersion Across Base RTT (Single-Flow, ECN On)",
            x_label="Range across 10/50/80 ms RTT (% of max)",
            output=figures_dir / "rtt-throughput-sensitivity.svg",
            color_fn=lambda row: color_for_tcp(row["tcp"]),
            legend=[(name, color) for name, color in TCP_COLORS.items()],
            extra_label_fn=lambda row: f"q-delay span {fmt_value(number(row, 'queue_delay_range_ms'))} ms",
        )
    )

    # Mixed-flow share (specialty chart)
    mixed_rows = read_rows(evaluation_dir / "mixed-flow-share.csv")
    write_mixed_share(mixed_rows, figures_dir / "mixed-flow-share.svg")

    # FQ-CoDel vs CoDel fairness delta
    fq_rows = sorted(
        read_rows(evaluation_dir / "mixed-fq-vs-codel.csv"),
        key=lambda row: number(row, "jain_delta_fq_minus_codel"),
        reverse=True,
    )
    write_horizontal_bar_chart(
        BarChart(
            rows=fq_rows,
            value_key="jain_delta_fq_minus_codel",
            label_fn=lambda row: f"{row['pair']} • {row['base_rtt']}",
            title="FQ-CoDel Fairness Change vs. CoDel (Mixed Flows)",
            x_label="Jain fairness delta (FQ-CoDel minus CoDel)",
            output=figures_dir / "mixed-fq-fairness-delta.svg",
            signed=True,
            legend=[("FQ-CoDel fairer", NEGATIVE), ("FQ-CoDel less fair", POSITIVE)],
            extra_label_fn=lambda row: f"Δdelay {fmt_value(number(row, 'queue_delay_delta_fq_minus_codel_ms'))} ms",
        )
    )

    # Single-throughput vs delay scatter (specialty chart)
    single_rows = read_rows(single_aggregate)
    frontier_rows = read_rows(evaluation_dir / "pareto-frontier.csv")
    write_throughput_delay_scatter(
        single_rows, frontier_rows, figures_dir / "single-throughput-delay-scatter.svg"
    )

    # Warmup sensitivity — shown as two grouped sections so the mixed-flow
    # campaign is always represented even when single-flow dominates the
    # global ranking.
    warmup_delta_path = evaluation_dir / "warmup-sensitivity-delta.csv"
    if warmup_delta_path.exists():
        warmup_rows = read_rows(warmup_delta_path)
        single_rows_warmup = sorted(
            [row for row in warmup_rows if row["campaign"] == "single-flow"],
            key=lambda row: number(row, "throughput_range_pct_of_max"),
            reverse=True,
        )
        mixed_rows_warmup = sorted(
            [row for row in warmup_rows if row["campaign"] == "mixed-flow"],
            key=lambda row: number(row, "throughput_range_pct_of_max"),
            reverse=True,
        )

        def warmup_color(row: dict[str, str]) -> str:
            return color_for_queue("fq") if row["campaign"] == "mixed-flow" else color_for_queue("codel")

        write_grouped_horizontal_bar_chart(
            [
                BarGroup(label="single-flow", rows=single_rows_warmup, max_rows=12),
                BarGroup(label="mixed-flow", rows=mixed_rows_warmup, max_rows=6),
            ],
            value_key="throughput_range_pct_of_max",
            label_fn=lambda row: row["config_key"],
            title="Warmup-Sensitivity Audit",
            x_label="Throughput range across tested warmups (% of max)",
            output=figures_dir / "warmup-sensitivity.svg",
            color_fn=warmup_color,
            legend=[
                ("single-flow campaign", color_for_queue("codel")),
                ("mixed-flow campaign", color_for_queue("fq")),
            ],
            extra_label_fn=lambda row: f"q-delay span {fmt_value(number(row, 'queue_delay_range_ms'))} ms",
        )

    # Stability audit — same grouped layout as the warmup chart so the two
    # audit figures read as siblings.
    stability_path = evaluation_dir / "stabilization-audit.csv"
    if stability_path.exists():
        stability_rows = read_rows(stability_path)
        for row in stability_rows:
            value = number(row, "final_vs_previous_delta_pct")
            row["abs_delta"] = "" if math.isnan(value) else str(abs(value))

        def _stability_sort_key(row: dict[str, str]) -> tuple[int, float]:
            value = number(row, "abs_delta")
            if math.isnan(value):
                return (1, 0.0)
            return (0, -value)

        single_stab = sorted(
            [row for row in stability_rows if row["campaign"] == "single-flow"],
            key=_stability_sort_key,
        )
        mixed_stab = sorted(
            [row for row in stability_rows if row["campaign"] == "mixed-flow"],
            key=_stability_sort_key,
        )

        def stab_color(row: dict[str, str]) -> str:
            value = number(row, "abs_delta")
            if math.isnan(value):
                return NEUTRAL
            return POSITIVE if value > 5 else color_for_queue("codel")

        def stab_label(row: dict[str, str]) -> str:
            pair = row["first_tcp"]
            if row.get("second_tcp"):
                pair += "/" + row["second_tcp"]
            return f"{pair} • {row['queue']} • {row['base_rtt']} • {row['flow']}"

        write_grouped_horizontal_bar_chart(
            [
                BarGroup(label="single-flow", rows=single_stab, max_rows=12),
                BarGroup(label="mixed-flow", rows=mixed_stab, max_rows=6),
            ],
            value_key="abs_delta",
            label_fn=stab_label,
            title="Late-Window Stability Audit",
            x_label="|final-quarter vs preceding-quarter throughput delta| (%)",
            output=figures_dir / "stabilization-audit.svg",
            color_fn=stab_color,
            legend=[
                ("stable (≤5%)", color_for_queue("codel")),
                ("unstable (>5%)", POSITIVE),
            ],
            extra_label_fn=lambda row: f"signed {fmt_value(number(row, 'final_vs_previous_delta_pct'))}%",
        )

    print(f"wrote core evaluation figures to {figures_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
