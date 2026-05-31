# Next Phase

## Current Status

The conservative single-flow campaign is complete and audited.

- Per-run rows: 180
- Aggregate rows: 60
- Return codes: 180 successful, 0 failed
- Stop time: 40 s for every run
- Warmup used for analysis: 10 s
- Replication: every aggregate row has 3 random-run values
- Missing configurations: 0

The Overleaf repository contains:

- `data/full-summary.csv`
- `data/full-summary-aggregate.csv`
- `fig/tcp-aqm/throughput-full.svg`
- `fig/tcp-aqm/queue-delay-full.svg`

The ns-3 workspace now also contains a reusable C++ contrib module:

- `contrib/tcp-aqm-config/`

It provides `TcpAqmExperimentConfig` and `TcpAqmTopologyConfig`, reads YAML/JSON through `yaml-cpp`, includes single-bottleneck and two-bottleneck example configs, and has a `tcp-aqm-config-validate` example.

The config-driven topology smoke path is also complete.

- Per-run rows: 2
- Aggregate rows: 2
- Topologies: `single-bottleneck`, `two-bottleneck`
- TCP / queue / RTT: CUBIC, CoDel, 10 ms
- ECN: enabled
- Stop time / warmup: 8 s / 4 s
- Overleaf data: `data/topology-smoke-summary.csv`, `data/topology-smoke-summary-aggregate.csv`

Do not make wall-clock runtime claims from this campaign. The network metrics are valid, but some runtime metadata came from an interrupted/resumed run sequence and should be regenerated in a clean timing pass if runtime overhead becomes part of the paper.

The mixed-flow smoke path is now complete.

- Per-run rows: 2
- Aggregate rows: 2
- Topologies: `single-bottleneck`, `two-bottleneck`
- Flow mix: CUBIC versus Reno
- Queue / RTT / ECN: CoDel, 10 ms, enabled
- Stop time / warmup: 24 s / 16 s
- Second-flow timing: base start at 10 s plus up to 5 s of `rngRun`-driven jitter
- Overleaf data: `data/mixed-smoke-summary.csv`, `data/mixed-smoke-summary-aggregate.csv`

Do not present this as a final result. It is a smoke test showing that the config path, second-flow traces, total throughput, and Jain fairness columns work. In this local run, the two-bottleneck smoke case took much longer than the single-bottleneck case, so use a conservative staged matrix for the paper campaign.

The replicated single-bottleneck mixed-flow campaign is complete.

- Per-run rows: 36
- Aggregate rows: 12
- Return codes: 36 successful, 0 failed
- Topology: `single-bottleneck`
- Flow mixes: CUBIC/Reno, CUBIC/DCTCP, Reno/DCTCP
- Queue disciplines: CoDel, FQ-CoDel
- Base RTT: 10 ms, 80 ms
- ECN: enabled
- Stop time / warmup: 40 s / 20 s
- Replication: every aggregate row has 3 random-run values
- Overleaf data: `data/mixed-summary.csv`, `data/mixed-summary-aggregate.csv`

Current mixed-flow findings:

- Total throughput stays near capacity at 10 ms RTT for all pairs.
- CUBIC/Reno with FQ-CoDel at 10 ms is nearly equal-share by Jain fairness.
- DCTCP-containing pairs have much lower queueing delay but lower fairness at 10 ms because DCTCP receives most of the bottleneck share.
- At 80 ms, fairness improves for all pairs, while total throughput is lower for DCTCP-containing pairs than for CUBIC/Reno.
- Do not make runtime claims from this campaign. One run had an unusually high local elapsed time even though all simulation outputs completed successfully.

## Phase 3: Mixed-Flow Sensitivity Completed

Goal: add actual workload variation while preserving the same artifact structure.

Completed scenario extension:

- The mixed-flow mode now uses config files.
- The single-bottleneck campaign compares TCP flow mixes sharing the bottleneck path.
- The second flow start time is randomized by `rngRun`.
- The same artifact structure is preserved across single-flow, topology-smoke, mixed-smoke, and mixed-flow campaigns.

Why this matters:

- The completed single-flow campaign has near-zero confidence intervals because the topology is deterministic.
- A mixed-flow workload lets confidence intervals represent real workload sensitivity.
- This creates a stronger ICNS3 contribution: the paper shows not just how to run a benchmark, but how reproducibility bookkeeping exposes when a benchmark lacks statistical diversity.

## Phase 4: Paper Tightening

- Introduction and contribution claim have been updated to include mixed-flow results.
- Related work has been strengthened against prior TCP/AQM evaluation suites.
- A LaTeX/TikZ workflow diagram has replaced the placeholder figure.
- The mixed-flow results table and prose have been promoted from smoke-test wording to replicated-campaign wording.
- Add artifact availability notes once the final code/result bundle location is chosen.

## Reproduce Current Full Campaign

Run from the ns-3 workspace root:

```bash
python3 paper/icns3-2026-tcp-aqm/scripts/run_tcp_aqm_sweep.py \
  --mode single-flow \
  --runs 3 \
  --stop-time 40s \
  --overwrite

python3 paper/icns3-2026-tcp-aqm/scripts/analyze_tcp_aqm.py --warmup 10
python3 paper/icns3-2026-tcp-aqm/scripts/plot_summary_svg.py
```

Copy the updated summaries into this Overleaf repo:

```bash
cp ../icns3-2026-tcp-aqm/results/summary.csv data/full-summary.csv
cp ../icns3-2026-tcp-aqm/results/summary-aggregate.csv data/full-summary-aggregate.csv
cp ../icns3-2026-tcp-aqm/figures/throughput-full.svg fig/tcp-aqm/throughput-full.svg
cp ../icns3-2026-tcp-aqm/figures/queue-delay-full.svg fig/tcp-aqm/queue-delay-full.svg
```

## Validate Config Module

```bash
./ns3 run "tcp-aqm-config-validate --configFile=contrib/tcp-aqm-config/configs/single-bottleneck.yaml"
./ns3 run "tcp-aqm-config-validate --configFile=contrib/tcp-aqm-config/configs/two-bottleneck.json"
```

## Reproduce Topology Smoke

The `direct` runner uses the already-built benchmark binary and avoids local ccache sandbox issues. Use the default `ns3` runner outside this sandbox if it works on your machine.

```bash
python3 paper/icns3-2026-tcp-aqm/scripts/run_tcp_aqm_sweep.py \
  --mode topology \
  --tcp-types cubic \
  --queue-types codel \
  --base-rtts 10ms \
  --ecns 1 \
  --runs 1 \
  --stop-time 8s \
  --results-dir paper/icns3-2026-tcp-aqm/results-topology-smoke \
  --overwrite \
  --runner direct

python3 paper/icns3-2026-tcp-aqm/scripts/analyze_tcp_aqm.py \
  --results-dir paper/icns3-2026-tcp-aqm/results-topology-smoke \
  --warmup 4
```

Recommended topology campaign for paper-ready results:

```bash
python3 paper/icns3-2026-tcp-aqm/scripts/run_tcp_aqm_sweep.py \
  --mode topology \
  --tcp-types reno,cubic,dctcp \
  --queue-types codel,fq \
  --base-rtts 10ms,80ms \
  --ecns 1 \
  --runs 3 \
  --stop-time 40s \
  --results-dir paper/icns3-2026-tcp-aqm/results-topology \
  --overwrite
```

## Reproduce Mixed-Flow Smoke

```bash
python3 paper/icns3-2026-tcp-aqm/scripts/run_tcp_aqm_sweep.py \
  --mode mixed-flow \
  --pairs cubic:reno \
  --queue-types codel \
  --base-rtts 10ms \
  --ecns 1 \
  --runs 1 \
  --stop-time 24s \
  --results-dir paper/icns3-2026-tcp-aqm/results-mixed-smoke \
  --overwrite \
  --runner direct

python3 paper/icns3-2026-tcp-aqm/scripts/analyze_tcp_aqm.py \
  --results-dir paper/icns3-2026-tcp-aqm/results-mixed-smoke \
  --warmup 16
```

Reproduce replicated mixed-flow campaign:

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

Optional narrow two-bottleneck add-on, only if runtime is acceptable:

```bash
python3 paper/icns3-2026-tcp-aqm/scripts/run_tcp_aqm_sweep.py \
  --mode mixed-flow \
  --topologies two-bottleneck \
  --pairs cubic:reno \
  --queue-types codel \
  --base-rtts 10ms \
  --ecns 1 \
  --runs 3 \
  --stop-time 40s \
  --results-dir paper/icns3-2026-tcp-aqm/results-mixed-two-bottleneck \
  --overwrite
```

## Commit to Overleaf/GitHub

```bash
./commit-and-push-overleaf.sh "Update ICNS3 draft"
```
