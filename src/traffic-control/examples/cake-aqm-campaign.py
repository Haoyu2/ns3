#!/usr/bin/env python3
# Copyright (c) 2026
# SPDX-License-Identifier: GPL-2.0-only
"""
Sweep driver for the CAKE vs AQM comparison.

Runs cake-aqm-comparison across a grid of queue discs, bottleneck rates, base
RTTs and RNG seeds, collects the single RESULT line each run prints, writes one
tidy CSV, and prints a mean-over-seeds summary table.

Run from the ns-3 source root after building the program, e.g.:

    ./ns3 build cake-aqm-comparison
    python3 src/traffic-control/examples/cake-aqm-campaign.py \
        --qdiscs Cake FqCoDel FqCobalt Pie PfifoFast \
        --bandwidths 10 50 --rtts 20 80 --seeds 1 2 3 --sim-time 20

Use --quick for a tiny grid to validate the pipeline.
"""

import argparse
import csv
import itertools
import statistics
import subprocess
import sys
from pathlib import Path

# Order matters: header of the RESULT line printed by cake-aqm-comparison.cc.
FIELDS = [
    "qdisc",
    "bwMbps",
    "rttMs",
    "nFlows",
    "seed",
    "aggMbps",
    "jain",
    "probeMeanMs",
    "probeJitterMs",
    "drops",
]

BIN_GLOBS = [
    "build/src/traffic-control/examples/ns3.47-cake-aqm-comparison-*",
    "build/src/traffic-control/examples/ns3*-cake-aqm-comparison-*",
]


def find_binary(explicit):
    if explicit:
        p = Path(explicit)
        if not p.exists():
            sys.exit(f"binary not found: {explicit}")
        return p
    for pattern in BIN_GLOBS:
        matches = sorted(Path(".").glob(pattern))
        if matches:
            return matches[0]
    sys.exit(
        "Could not find the cake-aqm-comparison binary. Build it first with\n"
        "  ./ns3 build cake-aqm-comparison\n"
        "or pass --binary <path>."
    )


def run_one(binary, qdisc, bw, rtt, nflows, sim_time, seed):
    """Run one scenario and return the parsed RESULT row as a dict (or None)."""
    cmd = [
        str(binary),
        f"--qdisc={qdisc}",
        f"--bandwidth={bw}",
        f"--rtt={rtt}",
        f"--nFlows={nflows}",
        f"--simTime={sim_time}",
        f"--seed={seed}",
    ]
    out = subprocess.run(cmd, capture_output=True, text=True)
    for line in out.stdout.splitlines():
        if line.startswith("RESULT,"):
            values = line[len("RESULT,") :].split(",")
            if len(values) == len(FIELDS):
                return dict(zip(FIELDS, values))
    sys.stderr.write(f"  WARNING: no RESULT for {cmd}\n{out.stderr}\n")
    return None


def summarize(rows):
    """Mean +/- stdev of key metrics over seeds, keyed by (qdisc, bw, rtt)."""
    groups = {}
    for r in rows:
        key = (r["qdisc"], r["bwMbps"], r["rttMs"])
        groups.setdefault(key, []).append(r)

    print(
        f"\n{'qdisc':<10} {'bw':>5} {'rtt':>5} "
        f"{'thrpt(Mbps)':>14} {'jain':>8} {'probeDelay(ms)':>18} {'drops':>8}"
    )
    print("-" * 72)

    def fmt(vals):
        m = statistics.mean(vals)
        s = statistics.stdev(vals) if len(vals) > 1 else 0.0
        return f"{m:.2f}+/-{s:.2f}"

    for key in sorted(groups, key=lambda k: (float(k[1]), float(k[2]), k[0])):
        g = groups[key]
        thr = [float(r["aggMbps"]) for r in g]
        jain = [float(r["jain"]) for r in g]
        delay = [float(r["probeMeanMs"]) for r in g]
        drops = [float(r["drops"]) for r in g]
        print(
            f"{key[0]:<10} {key[1]:>5} {key[2]:>5} "
            f"{fmt(thr):>14} {fmt(jain):>8} {fmt(delay):>18} {fmt(drops):>8}"
        )


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--qdiscs", nargs="+",
                    default=["Cake", "FqCoDel", "FqCobalt", "Pie", "PfifoFast"])
    ap.add_argument("--bandwidths", nargs="+", type=float, default=[10, 50])
    ap.add_argument("--rtts", nargs="+", type=float, default=[20, 80])
    ap.add_argument("--seeds", nargs="+", type=int, default=[1, 2, 3])
    ap.add_argument("--nflows", type=int, default=4)
    ap.add_argument("--sim-time", type=float, default=20.0)
    ap.add_argument("--binary", default=None, help="path to the built program")
    ap.add_argument("--out", default="cake-aqm-results.csv")
    ap.add_argument("--quick", action="store_true",
                    help="tiny grid (one bw, one rtt, two seeds) to validate the pipeline")
    args = ap.parse_args()

    if args.quick:
        args.bandwidths = [10]
        args.rtts = [40]
        args.seeds = [1, 2]
        args.sim_time = min(args.sim_time, 10.0)

    binary = find_binary(args.binary)
    combos = list(itertools.product(args.qdiscs, args.bandwidths, args.rtts, args.seeds))
    print(f"Using binary: {binary}\nRunning {len(combos)} simulations...")

    rows = []
    for i, (qdisc, bw, rtt, seed) in enumerate(combos, 1):
        print(f"  [{i}/{len(combos)}] {qdisc} bw={bw} rtt={rtt} seed={seed}")
        row = run_one(binary, qdisc, bw, rtt, args.nflows, args.sim_time, seed)
        if row:
            rows.append(row)

    if not rows:
        sys.exit("No results collected.")

    with open(args.out, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)
    print(f"\nWrote {len(rows)} rows to {args.out}")

    summarize(rows)


if __name__ == "__main__":
    main()
