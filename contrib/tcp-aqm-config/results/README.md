# `tcp-aqm-config` testing results

This directory holds the canonical test data for the `tcp-aqm-config` contrib
module: the campaign summary CSVs, the derived evaluation tables, and the
SVG figures used in the ICNS3 2026 paper and its archived artifact.

Configurations live one level up in [`../configs/`](../configs/). The Python
runner, analyzer, and plotter that produced everything here live in
[`paper/icns3-2026-tcp-aqm/scripts/`](../../../paper/icns3-2026-tcp-aqm/scripts/).

## Layout

```
results/
├── single-flow/              # 60-config single-flow campaign (3 RngRun seeds)
│   ├── summary.csv             # per-run rows (180)
│   └── summary-aggregate.csv   # per-config rows with CI95 (60)
├── mixed-flow/               # 12-config mixed-flow campaign (3 seeds)
│   ├── summary.csv             # per-run rows (36)
│   └── summary-aggregate.csv   # per-config rows with CI95 (12)
├── smoke/                    # short sanity-check campaigns
│   ├── config-sweep/           # config-file driven sweep (RTT)
│   ├── mixed-flow/             # mixed-flow smoke
│   └── topology/               # two-bottleneck topology smoke
├── evaluation/               # derived analysis tables (analyze_core_evaluation.py)
│   ├── campaign-audit.csv
│   ├── ecn-impact.csv
│   ├── mixed-flow-share.csv
│   ├── mixed-fq-vs-codel.csv
│   ├── pareto-frontier.csv
│   ├── ranking-stability.csv
│   ├── single-flow-rtt-sensitivity.csv
│   ├── stabilization-audit.csv
│   ├── warmup-sensitivity-delta.csv
│   └── warmup-sensitivity.csv
└── figures/                  # SVG figures (plot_core_evaluation_svg.py)
    ├── ecn-queue-delay-delta.svg
    ├── mixed-flow-share.svg
    ├── mixed-fq-fairness-delta.svg
    ├── rtt-throughput-sensitivity.svg
    ├── single-throughput-delay-scatter.svg
    ├── stabilization-audit.svg
    ├── warmup-sensitivity.svg
    └── inspection/             # quick-look SVGs from plot_summary_svg.py
        ├── queue-delay-full.svg
        ├── queue-delay-smoke.svg
        ├── throughput-full.svg
        └── throughput-smoke.svg
```

Raw per-run trace directories (`<campaign>/<run-id>/tcp-aqm-benchmark-*.dat`,
`metadata.json`, `stdout.txt`, etc.) are intentionally **not** committed (they
total roughly 1 GB). They can be regenerated from the commands below; only the
summary CSVs and derived artifacts are tracked.

## Regenerate from scratch

The full pipeline reproduces every committed CSV and figure here. All commands
are run from the repo root.

### 1. Build the contrib module

```bash
./ns3 configure --enable-examples
./ns3 build tcp-aqm-config tcp-aqm-benchmark tcp-aqm-config-validate
```

### 2. Run the campaigns

Single-flow main campaign (180 runs, ≈30 min on a laptop):

```bash
python3 paper/icns3-2026-tcp-aqm/scripts/run_tcp_aqm_sweep.py \
  --mode single-flow --runs 3 --stop-time 40s --overwrite \
  --results-dir contrib/tcp-aqm-config/results/single-flow
```

Mixed-flow replicated campaign (36 runs):

```bash
python3 paper/icns3-2026-tcp-aqm/scripts/run_tcp_aqm_sweep.py \
  --mode mixed-flow --topologies single-bottleneck \
  --pairs cubic:reno,cubic:dctcp,reno:dctcp \
  --queue-types codel,fq --base-rtts 10ms,80ms --ecns 1 \
  --runs 3 --stop-time 40s --overwrite \
  --results-dir contrib/tcp-aqm-config/results/mixed-flow
```

Smoke campaigns (small, fast):

```bash
python3 paper/icns3-2026-tcp-aqm/scripts/run_tcp_aqm_sweep.py \
  --mode topology --topologies single-bottleneck,two-bottleneck \
  --runs 1 --stop-time 15s --overwrite \
  --results-dir contrib/tcp-aqm-config/results/smoke/topology

python3 paper/icns3-2026-tcp-aqm/scripts/run_tcp_aqm_sweep.py \
  --mode mixed-flow --runs 1 --stop-time 15s --overwrite \
  --results-dir contrib/tcp-aqm-config/results/smoke/mixed-flow

python3 paper/icns3-2026-tcp-aqm/scripts/run_tcp_aqm_sweep.py \
  --config-file contrib/tcp-aqm-config/configs/single-bottleneck.json \
  --results-dir contrib/tcp-aqm-config/results/smoke/config-sweep \
  --overwrite
```

### 3. Build the summary CSVs

```bash
python3 paper/icns3-2026-tcp-aqm/scripts/analyze_tcp_aqm.py \
  --results-dir contrib/tcp-aqm-config/results/single-flow --warmup 10
python3 paper/icns3-2026-tcp-aqm/scripts/analyze_tcp_aqm.py \
  --results-dir contrib/tcp-aqm-config/results/mixed-flow --warmup 20
python3 paper/icns3-2026-tcp-aqm/scripts/analyze_tcp_aqm.py \
  --results-dir contrib/tcp-aqm-config/results/smoke/topology --warmup 5
python3 paper/icns3-2026-tcp-aqm/scripts/analyze_tcp_aqm.py \
  --results-dir contrib/tcp-aqm-config/results/smoke/mixed-flow --warmup 5
python3 paper/icns3-2026-tcp-aqm/scripts/analyze_tcp_aqm.py \
  --results-dir contrib/tcp-aqm-config/results/smoke/config-sweep --warmup 5
```

### 4. Derive the core evaluation tables and figures

```bash
python3 paper/icns3-2026-tcp-aqm/scripts/analyze_core_evaluation.py
python3 paper/icns3-2026-tcp-aqm/scripts/plot_core_evaluation_svg.py
python3 paper/icns3-2026-tcp-aqm/scripts/plot_summary_svg.py
```

The analyzers and plotters default to this `contrib/tcp-aqm-config/results/`
tree, so no `--*-dir` flags are needed in the common case.

## What each file represents

| File | What it captures |
| --- | --- |
| `single-flow/summary.csv` | One row per single-flow run, with raw metrics, sweep metadata, and the recorded git commit / source SHA. |
| `single-flow/summary-aggregate.csv` | One row per (TCP, queue, RTT, ECN) cell, with the mean and 95 % confidence interval over the seeds in the campaign. |
| `mixed-flow/*.csv` | Same shape as single-flow, plus second-flow throughput, total throughput, and Jain fairness columns. |
| `smoke/*/summary*.csv` | Short campaigns kept for sanity-checking the module after edits; the smoke campaigns share the column schema with the main campaigns. |
| `evaluation/ecn-impact.csv` | ECN-on vs ECN-off delta in throughput, queue delay, drops, and marks per (TCP, queue, RTT) cell. |
| `evaluation/single-flow-rtt-sensitivity.csv` | Throughput dispersion across the three base RTT values for each (TCP, queue, ECN) cell. |
| `evaluation/ranking-stability.csv` | Throughput / delay rankings per (queue, RTT, ECN) operating point. |
| `evaluation/pareto-frontier.csv` | Non-dominated (TCP, queue) cells per (RTT, ECN) point. |
| `evaluation/mixed-flow-share.csv` | First-flow / second-flow throughput share per mixed-flow cell. |
| `evaluation/mixed-fq-vs-codel.csv` | Fairness and delay delta between FQ-CoDel and CoDel for each pair. |
| `evaluation/warmup-sensitivity.csv` / `warmup-sensitivity-delta.csv` | Throughput at multiple warmup thresholds, plus per-config range. |
| `evaluation/stabilization-audit.csv` | Final-quarter vs preceding-quarter throughput delta per run. |
| `evaluation/campaign-audit.csv` | Counts of successful / failed runs and trace files per campaign. |
| `figures/*.svg` | Paper-facing figures rendered from the evaluation tables. |
| `figures/inspection/*.svg` | Quick-look SVGs straight from the aggregate summary tables. |

## Pinning provenance

Every run directory's `metadata.json` records, in addition to the configured
parameters: the ns-3 commit hash, the working-tree status, the SHA-256 hash of
each pinned benchmark and contrib module source file, a snapshot of ns-3-relevant
environment variables (`NS_LOG`, `NS_GLOBAL_VALUE`, `LD_LIBRARY_PATH`, …), the
exact command line, the return code, and the wall-clock time. The runner prints
a warning when any pinned source is dirty in git so reviewers can tell the
recorded commit apart from the actual running code.
