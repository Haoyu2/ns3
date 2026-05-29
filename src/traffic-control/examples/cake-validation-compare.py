#!/usr/bin/env python3
# Copyright (c) 2026
# SPDX-License-Identifier: GPL-2.0-only
"""
Compare ns-3 CAKE against a captured Linux sch_cake reference.

This lines up the rrul-like ns-3 scenario (cake-rrul-validation) with numbers
measured on a real Linux box running `tc qdisc ... cake` under Flent's rrul
test. It runs (or reads) the ns-3 result, loads a reference JSON, and prints a
side-by-side table with the relative error per metric, flagging PASS/FAIL
against a tolerance. The process exits non-zero if any metric fails, so it can
gate a reproducibility check in CI.

Capturing the Linux reference (summary; full commands in doc/cake.rst):

    # shape + emulate the path on the egress interface
    tc qdisc replace dev <iface> root cake bandwidth 10Mbit
    # (use netem on a separate node/veth to add the base RTT)
    flent rrul -H <server> -l 60 -o linux-cake.flent.gz
    flent --format=summary linux-cake.flent.gz   # read off throughput + ping percentiles

Then fill those numbers into a reference JSON (see
cake-validation-reference.example.json) and run:

    python3 src/traffic-control/examples/cake-validation-compare.py \
        --reference my-linux-cake.json

Run from the ns-3 source root after `./ns3 build cake-rrul-validation`.
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path

# Columns of the RESULT line printed by cake-rrul-validation.cc.
NS3_FIELDS = [
    "bwMbps", "rttMs", "nFlows", "seed",
    "downMbps", "upMbps", "rttP50", "rttP90", "rttP99", "rttMax", "ackDrops",
]

# Reference metric key -> ns-3 RESULT field.
METRICS = [
    ("down_mbps", "downMbps", "higher-better"),
    ("up_mbps", "upMbps", "higher-better"),
    ("rtt_p50_ms", "rttP50", "lower-better"),
    ("rtt_p90_ms", "rttP90", "lower-better"),
    ("rtt_p99_ms", "rttP99", "lower-better"),
]

BIN_GLOBS = [
    "build/src/traffic-control/examples/ns3.47-cake-rrul-validation-*",
    "build/src/traffic-control/examples/ns3*-cake-rrul-validation-*",
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
    sys.exit("Build it first: ./ns3 build cake-rrul-validation  (or pass --binary)")


def run_ns3(binary, scenario, sim_time, seed):
    cmd = [
        str(binary),
        f"--bandwidth={scenario['bandwidth_mbps']}",
        f"--rtt={scenario['rtt_ms']}",
        f"--nFlows={scenario.get('nflows', 4)}",
        f"--simTime={sim_time}",
        f"--seed={seed}",
    ]
    out = subprocess.run(cmd, capture_output=True, text=True)
    for line in out.stdout.splitlines():
        if line.startswith("RESULT,"):
            values = line[len("RESULT,") :].split(",")
            return dict(zip(NS3_FIELDS, (float(v) for v in values)))
    sys.exit(f"ns-3 produced no RESULT line.\ncmd: {cmd}\nstderr:\n{out.stderr}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--reference", required=True, help="reference JSON with Linux/Flent numbers")
    ap.add_argument("--binary", default=None, help="ns-3 cake-rrul-validation binary")
    ap.add_argument("--ns3-result", default=None,
                    help="a saved RESULT,... line (skip running ns-3)")
    ap.add_argument("--tolerance", type=float, default=0.20,
                    help="max allowed relative error per metric (default 0.20)")
    ap.add_argument("--sim-time", type=float, default=30.0)
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()

    ref = json.loads(Path(args.reference).read_text())
    scenario = ref["scenario"]
    linux = ref["linux_cake"]

    if args.ns3_result:
        values = args.ns3_result.replace("RESULT,", "").split(",")
        ns3 = dict(zip(NS3_FIELDS, (float(v) for v in values)))
    else:
        ns3 = run_ns3(find_binary(args.binary), scenario, args.sim_time, args.seed)

    print(f"\nScenario: {scenario['bandwidth_mbps']} Mbps, {scenario['rtt_ms']} ms RTT, "
          f"{scenario.get('nflows', 4)} flows/direction")
    print(f"Linux reference: {ref.get('source', 'unspecified')}")
    print(f"Tolerance: +/-{args.tolerance * 100:.0f}%\n")
    print(f"{'metric':<14}{'ns-3':>12}{'linux':>12}{'rel.err':>10}{'verdict':>9}")
    print("-" * 57)

    all_pass = True
    for ref_key, ns3_key, _ in METRICS:
        if ref_key not in linux:
            continue
        a = ns3[ns3_key]
        b = float(linux[ref_key])
        rel = abs(a - b) / b if b else float("inf")
        ok = rel <= args.tolerance
        all_pass = all_pass and ok
        print(f"{ref_key:<14}{a:>12.3f}{b:>12.3f}{rel * 100:>9.1f}%{('PASS' if ok else 'FAIL'):>9}")

    print()
    if all_pass:
        print("VALIDATION PASSED: ns-3 CAKE matches Linux within tolerance.")
        return 0
    print("VALIDATION FAILED: one or more metrics outside tolerance.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
