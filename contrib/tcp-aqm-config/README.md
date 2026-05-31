# TCP/AQM Config contrib module

This module provides C++ configuration objects for the ICNS3 2026 TCP/AQM
benchmark artifact. It reads YAML or JSON files through `yaml-cpp`, uses `fmt`
for normalized configuration output, validates the experiment/topology sections,
and exposes typed configuration objects to ns-3 examples.

## Directory layout

| Path | What it contains |
| --- | --- |
| [`configs/`](configs/) | YAML / JSON example configurations: single-bottleneck and two-bottleneck templates, each with a `sweep:` block. |
| [`model/`](model/) | C++ configuration classes (`TcpAqmExperimentConfig`, `TcpAqmTopologyConfig`, sweep + analysis enums). |
| [`examples/`](examples/) | `tcp-aqm-benchmark` (derived from `examples/tcp/tcp-validation.cc`) and the `tcp-aqm-config-validate` config-checker. |
| [`scripts/`](scripts/) | Dependency installers for macOS and Ubuntu. |
| [`results/`](results/) | Committed test artifacts: per-campaign summary CSVs, derived evaluation tables, and reference figures. See [`results/README.md`](results/README.md) for layout and regeneration commands. Raw per-run trace directories are gitignored. |

The public API uses ns-3 value types for scalar fields and enums for controlled
choices:

- `Time` for RTT, stop time, CE threshold, flow start times, start jitter, and link delays
- `DataRate` for access and bottleneck rates
- `QueueSize` for device queue limits
- `TcpAqmTcpType` for TCP congestion-control choices
- `TcpAqmQueueDiscType` for bottleneck queue-disc choices
- `TcpAqmTopologyKind` for topology templates
- `TcpAqmSweepParameter` for one-factor sweep targets
- `TcpAqmAnalysisMetric` for selected analysis outputs

## One-parameter sweeps

Each config may include a `sweep` section that varies exactly one parameter
while keeping the rest of the experiment fixed.  This supports artifact-first
one-factor-at-a-time sensitivity studies without editing the benchmark source or
the runner script.

Example:

```yaml
sweep:
  enabled: true
  name: rtt-sensitivity
  parameter: experiment.baseRtt
  values: [10ms, 50ms, 80ms]

analysis:
  metrics:
    - throughput_mean_mbps
    - queue_delay_p95_ms
    - mark_count
```

Supported sweep parameters include experiment fields such as
`experiment.baseRtt`, `experiment.queueType`, `experiment.queueUseEcn`,
`experiment.firstStartJitter`, and `experiment.rngRun`; topology fields such as
`topology.accessRate`; and indexed bottleneck fields such as
`topology.bottlenecks[0].rate` or `topology.bottlenecks[1].queueType`.

`experiment.firstStartJitter` is useful for single-flow campaigns. Without a
stochastic input the simulation is deterministic, so a multi-seed run produces
identical traces and the corresponding 95% confidence intervals are
structurally zero. Setting a small jitter (sampled from `RngRun`-seeded RNG)
gives the seeds real variance without changing topology or queue settings.

The paper runner expands these configs directly:

```bash
python3 paper/icns3-2026-tcp-aqm/scripts/run_tcp_aqm_sweep.py \
  --config-file contrib/tcp-aqm-config/configs/single-bottleneck.json \
  --results-dir paper/icns3-2026-tcp-aqm/results-config-sweep \
  --runner direct --overwrite
python3 paper/icns3-2026-tcp-aqm/scripts/analyze_tcp_aqm.py \
  --results-dir paper/icns3-2026-tcp-aqm/results-config-sweep --warmup 10
```

When `analysis.metrics` is present, the analyzer still writes the complete
summary files and also writes compact selected-metric CSVs for the declared
metrics.

The Python runner reads JSON with the standard library. YAML sweep files can be
used by the runner when PyYAML is installed; the C++ ns-3 examples and validator
read both YAML and JSON through `yaml-cpp`.

## Dependency

macOS:

```bash
contrib/tcp-aqm-config/scripts/install-deps-macos.sh
```

Ubuntu:

```bash
contrib/tcp-aqm-config/scripts/install-deps-ubuntu.sh
```

## Validation example

```bash
./ns3 run "tcp-aqm-config-validate --configFile=contrib/tcp-aqm-config/configs/single-bottleneck.yaml"
./ns3 run "tcp-aqm-config-validate --configFile=contrib/tcp-aqm-config/configs/two-bottleneck.json"
```

The example prints the normalized configuration, including derived bottleneck
delays, sweep values, and requested analysis metrics when a config file includes
them.
