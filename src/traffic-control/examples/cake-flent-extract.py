#!/usr/bin/env python3
# Copyright (c) 2026
# SPDX-License-Identifier: GPL-2.0-only
"""
Turn a Flent rrul capture (.flent.gz) into a reference JSON for
cake-validation-compare.py.

It reads the aggregate TCP throughput (down/up) and the ICMP ping series from
the Flent results and emits the {scenario, linux_cake, source} JSON the
comparison script expects. No third-party dependencies.

Usage:
  python3 cake-flent-extract.py linux-cake.flent.gz \
      --bandwidth 10 --rtt 40 --nflows 4 --out my-linux-cake.json

Inspect the available data series first if the defaults don't match your Flent
version:
  python3 cake-flent-extract.py linux-cake.flent.gz --list
"""

import argparse
import gzip
import json
import math
import shutil
import subprocess
import sys


def load(path):
    with gzip.open(path, "rt") as f:
        return json.load(f)


def series(results, *names):
    """Return the first matching series as a list of non-null floats."""
    for n in names:
        if n in results:
            return [float(v) for v in results[n] if v is not None]
    return []


def percentile(values, p):
    if not values:
        return 0.0
    s = sorted(values)
    idx = max(0, math.ceil(p / 100.0 * len(s)) - 1)
    return s[min(idx, len(s) - 1)]


def mean(values):
    return sum(values) / len(values) if values else 0.0


def aggregate_throughput(results, direction):
    """Aggregate TCP throughput for a direction, the way Flent's summary does it:
    the sum of each individual flow's own average (rrul has 4 DSCP-marked flows
    per direction, e.g. 'TCP upload BE/BK/CS5/EF'). Averaging the pre-summed
    '... sum' series instead is wrong, because the per-timestep sums are sampling-
    misaligned and undercount."""
    prefix = f"TCP {direction} "
    exclude = {f"TCP {direction} sum", f"TCP {direction} avg"}
    total = 0.0
    found = False
    for key, vals in results.items():
        if key.startswith(prefix) and key not in exclude:
            nn = [float(x) for x in vals if x is not None]
            if nn:
                total += sum(nn) / len(nn)
                found = True
    if found:
        return total
    return mean(series(results, f"TCP {direction} sum"))  # fallback


def flent_summary_throughput(path):
    """Authoritative down/up Mbps from Flent's own summary (None if flent absent)."""
    if not shutil.which("flent"):
        return None, None
    try:
        out = subprocess.run(["flent", "--format=summary", path],
                             capture_output=True, text=True, timeout=120).stdout
    except Exception:
        return None, None
    down = up = None
    for line in out.splitlines():
        if ":" not in line:
            continue
        name, rest = line.split(":", 1)
        toks = rest.split()
        if not toks:
            continue
        try:
            val = float(toks[0])
        except ValueError:
            continue
        if name.strip() == "TCP download sum":
            down = val
        elif name.strip() == "TCP upload sum":
            up = val
    return down, up


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("flentfile")
    ap.add_argument("--bandwidth", type=float, help="shaper rate in Mbps (scenario)")
    ap.add_argument("--rtt", type=float, help="base RTT in ms (scenario)")
    ap.add_argument("--nflows", type=int, default=4)
    ap.add_argument("--out", default=None, help="write JSON here (default: stdout)")
    ap.add_argument("--list", action="store_true", help="list data series and exit")
    args = ap.parse_args()

    data = load(args.flentfile)
    results = data.get("results", data)

    if args.list:
        print("\n".join(sorted(results.keys())))
        return

    if args.bandwidth is None or args.rtt is None:
        sys.exit("--bandwidth and --rtt are required (use --list to inspect series)")

    ping = series(results, "Ping (ms) ICMP", "Ping (ms) avg", "Ping (ms) UDP BE")
    # Prefer Flent's own summary throughput (authoritative); fall back to summing
    # the per-flow averages if flent isn't available on this host.
    down_mbps, up_mbps = flent_summary_throughput(args.flentfile)
    if down_mbps is None:
        down_mbps = aggregate_throughput(results, "download")
    if up_mbps is None:
        up_mbps = aggregate_throughput(results, "upload")

    if not ping or down_mbps == 0 or up_mbps == 0:
        sys.stderr.write(
            "WARNING: some expected series were not found; run with --list to see "
            "the real names in this file, then adjust.\n"
        )

    ref = {
        "scenario": {
            "bandwidth_mbps": args.bandwidth,
            "rtt_ms": args.rtt,
            "nflows": args.nflows,
        },
        "linux_cake": {
            "down_mbps": round(down_mbps, 3),
            "up_mbps": round(up_mbps, 3),
            "rtt_p50_ms": round(percentile(ping, 50), 2),
            "rtt_p90_ms": round(percentile(ping, 90), 2),
            "rtt_p99_ms": round(percentile(ping, 99), 2),
        },
        "source": f"flent rrul, {args.flentfile}",
    }

    text = json.dumps(ref, indent=2)
    if args.out:
        with open(args.out, "w") as f:
            f.write(text + "\n")
        print(f"wrote {args.out}")
    else:
        print(text)


if __name__ == "__main__":
    main()
