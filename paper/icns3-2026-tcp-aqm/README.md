# ICNS3 2026 TCP/AQM Paper Workspace

Working title:

**Reproducible TCP and AQM Benchmarking in ns-3: A Baseline Study of Congestion Control, ECN, and Queue Discipline Interactions**

## Thesis

The paper should be centered on ns-3 methodology, not only on networking results. The strongest near-term contribution is a reproducible benchmark suite that uses ns-3's existing TCP and traffic-control models to expose how conclusions about TCP congestion control depend on queue discipline, RTT, ECN support, and flow mix.

## Why This Fits ICNS3

ICNS3 papers are strongest when they improve or scrutinize ns-3 itself. This direction contributes:

1. A reproducible TCP/AQM experiment harness for ns-3.
2. A documented baseline matrix over TCP variants, AQM queue discs, RTTs, ECN, and seeds.
3. A case study showing which conclusions are stable and which depend on scenario assumptions.
4. An artifact structure that stores command lines, ns-3 version metadata, raw traces, and summaries.

## Positioning Against Prior Work

This should be framed as a complement to existing ns-3 TCP and AQM evaluation suites, not as the first such suite. Prior WNS3 work already automated ICCRG-style TCP evaluation, Tmix-based common TCP evaluation, and RFC 7928-style AQM evaluation. The defensible gap is a smaller, current-ns-3 benchmark artifact derived from the in-tree `tcp-validation.cc` scenario and implemented separately as `contrib/tcp-aqm-config/examples/tcp-aqm-benchmark.cc`. The artifact makes TCP/AQM interaction results auditable across RTT, ECN, queue discipline, flow mix, and random seed.

See `related-work.md` and `references.bib` for the working literature map.

## Current Scope

Use `contrib/tcp-aqm-config/examples/tcp-aqm-benchmark.cc` as the experiment scenario. It is derived from `examples/tcp/tcp-validation.cc`, which already models a validated dumbbell topology with:

- TCP Linux Reno, CUBIC, and DCTCP.
- CoDel, FQ-CoDel, PIE, and RED bottleneck queues.
- Optional ECN marking at the queue.
- Throughput, RTT, congestion window, queue delay, drop, and mark traces.

This avoids spending the first week writing a simulator from scratch while keeping upstream `tcp-validation.cc` unchanged. Paper-specific generalization stays in the separate benchmark scenario and in `contrib/tcp-aqm-config/`.

Current extension status:

- `tcp-aqm-benchmark.cc` accepts short TCP aliases and full ns-3 TCP TypeIds.
- The benchmark accepts YAML/JSON config files through the C++ `tcp-aqm-config` contrib module.
- Config files can select single-bottleneck and two-bottleneck topology templates.
- Config files can declare a one-parameter sweep plus the metrics to emphasize in analysis.
- Mixed-flow runs support typed flow start times and `rngRun`-driven second-flow start jitter.
- Single-flow runs support `rngRun`-driven first-flow start jitter (`--firstStartJitter`) so multi-seed campaigns produce non-trivial confidence intervals without changing topology or queue settings.
- The runner accepts custom TCP, queue, RTT, ECN, topology, and competing-flow matrices.
- The runner pins each run with per-file SHA-256 hashes of the benchmark and contrib module sources, plus a snapshot of ns-3-relevant environment variables (`NS_LOG`, `NS_GLOBAL_VALUE`, `LD_LIBRARY_PATH`, etc.). A warning is printed when the pinned sources are dirty in git so reviewers can tell the recorded commit apart from the actual running code.
- `examples/tcp/tcp-validation.cc` remains unchanged.

See `scenario-extension-plan.md` for the scoped path from the validated baseline to optional BBR and competing-flow experiments.

The active Overleaf/GitHub paper repo is `../icns3-2026-summer/`. Make new LaTeX paper edits there. The local `tex/` directory is now a backup/staging copy.

## Research Questions

RQ1. Across CoDel, FQ-CoDel, PIE, and RED, how do TCP CUBIC, Linux Reno, and DCTCP trade throughput, queueing delay, drops, and ECN marks?

RQ2. How sensitive are these conclusions to base RTT and ECN configuration?

RQ3. How does a competing flow mix change throughput sharing and queueing delay?

RQ4. What metadata and artifact structure is needed to make a TCP/AQM ns-3 study reproducible by another user?

## Paper Contributions

1. **Benchmark harness:** A scripted workflow that expands a TCP/AQM matrix, runs ns-3, stores per-run metadata, and preserves raw traces.
2. **C++ configuration module:** A `yaml-cpp`/`fmt` contrib module with strongly typed experiment and topology objects.
3. **Config-authored sensitivity sweeps:** A one-factor sweep declaration inside the same YAML/JSON config file as the experiment and topology, with requested analysis metrics.
4. **Analysis pipeline:** Summary tables for throughput, fairness, RTT, queue delay, congestion window, drops, and marks.
5. **Baseline results:** Reproducible single-flow and mixed-flow comparisons of ns-3 TCP/AQM behavior across RTT, ECN, queue discipline, and flow mix.
6. **Reproducibility checklist:** Lessons for future ns-3 TCP/AQM papers, including exact command lines, commit hash, seeds, and trace provenance.

## Reproduce Current Results

Single-flow campaign used for the main paper table:

```bash
python3 paper/icns3-2026-tcp-aqm/scripts/run_tcp_aqm_sweep.py --mode single-flow --runs 3 --stop-time 40s --overwrite --runner direct
python3 paper/icns3-2026-tcp-aqm/scripts/analyze_tcp_aqm.py --warmup 10
python3 paper/icns3-2026-tcp-aqm/scripts/plot_summary_svg.py
```

Replicated mixed-flow campaign:

```bash
python3 paper/icns3-2026-tcp-aqm/scripts/run_tcp_aqm_sweep.py \
  --mode mixed-flow \
  --topologies single-bottleneck \
  --pairs cubic:reno,cubic:dctcp,reno:dctcp \
  --queue-types codel,fq \
  --base-rtts 10ms,80ms \
  --ecns 1 \
  --runs 3 \
  --stop-time 40s \
  --results-dir paper/icns3-2026-tcp-aqm/results-mixed \
  --overwrite \
  --runner direct

python3 paper/icns3-2026-tcp-aqm/scripts/analyze_tcp_aqm.py \
  --results-dir paper/icns3-2026-tcp-aqm/results-mixed \
  --warmup 20
```

Core evaluation tables and scientific SVG figures:

```bash
python3 paper/icns3-2026-tcp-aqm/scripts/analyze_core_evaluation.py
python3 paper/icns3-2026-tcp-aqm/scripts/plot_core_evaluation_svg.py
```

Config-authored one-parameter sweep:

```bash
python3 paper/icns3-2026-tcp-aqm/scripts/run_tcp_aqm_sweep.py \
  --config-file contrib/tcp-aqm-config/configs/single-bottleneck.json \
  --results-dir paper/icns3-2026-tcp-aqm/results-config-sweep \
  --runner direct \
  --overwrite

python3 paper/icns3-2026-tcp-aqm/scripts/analyze_tcp_aqm.py \
  --results-dir paper/icns3-2026-tcp-aqm/results-config-sweep \
  --warmup 10
```

Config-module validation:

```bash
./ns3 run "tcp-aqm-config-validate --configFile=contrib/tcp-aqm-config/configs/single-bottleneck.yaml"
./ns3 run "tcp-aqm-config-validate --configFile=contrib/tcp-aqm-config/configs/two-bottleneck.json"
```

Use `--runner direct` when the already-built benchmark binary should be invoked directly. This avoids local rebuild/ccache issues in this workspace. On a normal ns-3 setup, the default `./ns3 run` runner should also work.

## Result Artifacts

- Single-flow summaries: `results/summary.csv`, `results/summary-aggregate.csv`
- Mixed-flow summaries: `results-mixed/summary.csv`, `results-mixed/summary-aggregate.csv`
- Config sweep summaries: `results-config-sweep*/summary*.csv`
- Topology smoke summaries: `results-topology-smoke/summary.csv`, `results-topology-smoke/summary-aggregate.csv`
- Mixed-flow smoke summaries: `results-mixed-smoke/summary.csv`, `results-mixed-smoke/summary-aggregate.csv`
- Inspection figures: `figures/throughput-full.svg`, `figures/queue-delay-full.svg`
- Core evaluation derived tables: `evaluation/*.csv`
- Core evaluation figures: `figures/core-eval/*.svg`

Raw per-run directories are intentionally ignored by git; the summary CSVs and paper-facing artifacts are preserved.
