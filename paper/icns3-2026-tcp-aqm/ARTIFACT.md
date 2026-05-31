# Artifact Manifest

This manifest describes the ns-3-side artifact for the ICNS3 2026 TCP/AQM
paper. The Overleaf-ready paper source is maintained separately in
`paper/icns3-2026-summer/`.

## Code Components

- `contrib/tcp-aqm-config/examples/tcp-aqm-benchmark.cc`
  - Paper-specific benchmark scenario derived from the topology and tracing
    structure of `examples/tcp/tcp-validation.cc`.
  - Supports command-line parameters and `--configFile`.
  - Supports single-flow and two-flow TCP/AQM experiments.

- `contrib/tcp-aqm-config/`
  - C++ configuration module using `yaml-cpp` and `fmt`.
  - Defines strongly typed experiment and topology configuration objects.
  - Defines typed one-parameter sweep targets and selected analysis metrics.
  - Provides YAML and JSON examples for single-bottleneck and two-bottleneck
    templates.
  - Includes `tcp-aqm-config-validate`.
  - `contrib/tcp-aqm-config/results/` is the canonical home of all committed
    test data (per-campaign summary CSVs, derived evaluation tables, and
    reference figures). See its `README.md` for layout and regeneration.

- `paper/icns3-2026-tcp-aqm/scripts/`
  - `run_tcp_aqm_sweep.py`: expands campaign matrices, runs ns-3, stores
    metadata and raw traces.
  - `analyze_tcp_aqm.py`: creates per-run and aggregate CSV summaries.
  - `plot_summary_svg.py`: creates dependency-free inspection SVGs.
  - `analyze_core_evaluation.py`: derives ECN impact, RTT sensitivity,
    ranking stability, Pareto frontier, mixed-flow sharing, warmup sensitivity,
    stability audit, and campaign audit tables.
  - `plot_core_evaluation_svg.py`: creates dependency-free SVG figures from
    the derived core evaluation tables.

## Preserved Results

The artifact preserves summary CSVs in git and ignores raw per-run directories.
All summary paths are relative to `contrib/tcp-aqm-config/results/`. Raw
traces can be regenerated from the commands below.

| Campaign | Per-run rows | Aggregate rows | Summary path |
| --- | ---: | ---: | --- |
| Single-flow full campaign | 180 | 60 | `single-flow/summary-aggregate.csv` |
| Topology smoke | 2 | 2 | `smoke/topology/summary-aggregate.csv` |
| Mixed-flow smoke | 2 | 2 | `smoke/mixed-flow/summary-aggregate.csv` |
| Mixed-flow replicated campaign | 36 | 12 | `mixed-flow/summary-aggregate.csv` |
| Config-authored RTT sweep smoke | 3 | 3 | `smoke/config-sweep/summary-selected-aggregate.csv` |

Additional derived evaluation artifacts are stored in
`contrib/tcp-aqm-config/results/evaluation/*.csv`.
Paper-facing core figures are stored in
`contrib/tcp-aqm-config/results/figures/*.svg`.

## Reproduction Commands

Build the benchmark and validator:

```bash
/opt/homebrew/bin/cmake --build cmake-build-debug --target tcp-aqm-config tcp-aqm-config-validate tcp-aqm-benchmark -j 4
```

Validate example config files:

```bash
build/contrib/tcp-aqm-config/examples/ns3.47-tcp-aqm-config-validate-debug \
  --configFile=contrib/tcp-aqm-config/configs/single-bottleneck.yaml
build/contrib/tcp-aqm-config/examples/ns3.47-tcp-aqm-config-validate-debug \
  --configFile=contrib/tcp-aqm-config/configs/two-bottleneck.json
```

Run and analyze the replicated mixed-flow campaign:

```bash
python3 paper/icns3-2026-tcp-aqm/scripts/run_tcp_aqm_sweep.py \
  --mode mixed-flow --topologies single-bottleneck \
  --pairs cubic:reno,cubic:dctcp,reno:dctcp \
  --queue-types codel,fq --base-rtts 10ms,80ms --ecns 1 \
  --runs 3 --stop-time 40s --overwrite \
  --results-dir contrib/tcp-aqm-config/results/mixed-flow

python3 paper/icns3-2026-tcp-aqm/scripts/analyze_tcp_aqm.py \
  --results-dir contrib/tcp-aqm-config/results/mixed-flow \
  --warmup 20
```

Run and analyze the single-flow campaign (default `--results-dir` is
`contrib/tcp-aqm-config/results/single-flow`):

```bash
python3 paper/icns3-2026-tcp-aqm/scripts/run_tcp_aqm_sweep.py \
  --mode single-flow --runs 3 --stop-time 40s --overwrite

python3 paper/icns3-2026-tcp-aqm/scripts/analyze_tcp_aqm.py --warmup 10
python3 paper/icns3-2026-tcp-aqm/scripts/plot_summary_svg.py
```

Run and analyze a config-authored one-parameter sweep:

```bash
python3 paper/icns3-2026-tcp-aqm/scripts/run_tcp_aqm_sweep.py \
  --config-file contrib/tcp-aqm-config/configs/single-bottleneck.json \
  --results-dir contrib/tcp-aqm-config/results/smoke/config-sweep \
  --overwrite

python3 paper/icns3-2026-tcp-aqm/scripts/analyze_tcp_aqm.py \
  --results-dir contrib/tcp-aqm-config/results/smoke/config-sweep \
  --warmup 10
```

Derive the core evaluation tables and figures from the preserved summaries and
raw traces:

```bash
python3 paper/icns3-2026-tcp-aqm/scripts/analyze_core_evaluation.py
python3 paper/icns3-2026-tcp-aqm/scripts/plot_core_evaluation_svg.py
```

## Dependency Notes

The config module requires `yaml-cpp` and `fmt`.

```bash
contrib/tcp-aqm-config/scripts/install-deps-macos.sh
contrib/tcp-aqm-config/scripts/install-deps-ubuntu.sh
```

The commands above use `--runner direct` because this local workspace can hit
ccache permission issues when invoking `./ns3 run` inside the sandbox. On a
regular ns-3 checkout, the default runner can be used after configuring and
building ns-3.

## Limitations

- This is not a new TCP or AQM model.
- This is not the first TCP or AQM evaluation suite for ns-3.
- Runtime metadata should not be used for paper claims until regenerated in an
  uninterrupted timing pass.
- The warmup and late-window audits show that several 40 s configurations are
  not steady enough for final performance claims; those results should motivate
  a longer final campaign.
- The two-bottleneck topology is currently used as a smoke/check path; the
  paper-ready mixed-flow campaign uses the single-bottleneck topology.
- The shipped single-flow campaign was run with `firstStartJitter=0s`, so the
  three `RngRun` values produce numerically identical traces and the recorded
  aggregate 95% confidence intervals are structurally zero. Future single-flow
  campaigns should set a small `firstStartJitter` (now supported in both the
  C++ benchmark and the Python runner) to produce meaningful CIs.
- Earlier runs in `results/` were produced before the runner started recording
  per-file SHA-256 hashes and the env snapshot. Those legacy run directories
  pin only the git commit and `git status --short`; reruns will include the
  expanded metadata.
