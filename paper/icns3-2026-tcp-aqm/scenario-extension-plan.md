# tcp-aqm-benchmark Extension Plan

## Phase 1: Generic TCP Selection

Status: implemented in `contrib/tcp-aqm-config/examples/tcp-aqm-benchmark.cc`.

The benchmark example now accepts both short aliases and ns-3 TypeId names for TCP congestion control:

- Short aliases: `reno`, `newreno`, `cubic`, `dctcp`, `bbr`, `vegas`, `veno`, `yeah`, `bic`, `westwoodplus`, and others.
- Full TypeIds: `ns3::TcpCubic`, `ns3::TcpDctcp`, `ns3::TcpBbr`, etc.

The existing validation cases still work, and DCTCP-specific trace hooks are now based on the resolved TypeId instead of string equality.

## Phase 2: Keep the Main Paper Matrix Tight

Primary ICNS3 matrix:

- TCP: `reno`, `cubic`, `dctcp`
- Queue: `codel`, `fq`, `pie`, `red`
- RTT: `10ms`, `50ms`, `80ms`
- ECN: off/on, excluding DCTCP without ECN from the main comparison
- Seeds: at least 3 runs, preferably 5 if runtime allows
- Stop time: at least `40s`
- Warmup: at least `10s`

This is the safest main result because it stays closest to the original validation scenario.

## Phase 3: Optional BBR Extension

Run BBR as a compact secondary matrix, not the main claim unless we have enough time to analyze it carefully:

```bash
python3 paper/icns3-2026-tcp-aqm/scripts/run_tcp_aqm_sweep.py \
  --mode single-flow \
  --tcp-types bbr \
  --queue-types codel,fq,pie,red \
  --base-rtts 10ms,80ms \
  --ecns 0,1 \
  --runs 3 \
  --stop-time 40s
```

Use BBR to show that the workflow can include additional TCP TypeIds, but be careful with interpretation because BBR is pacing/model based and is largely loss/ECN agnostic.

## Phase 4: Scenario Modes Worth Adding If Time Permits

The current example already supports a second TCP flow with `--secondTcpType`. Use that before adding a new topology.

1. **Single-flow baseline:** current default, best for clean throughput/delay/mark/drop comparisons.
2. **Competing-flow fairness:** use `--secondTcpType` to compare CUBIC vs Reno, CUBIC vs DCTCP, and BBR vs CUBIC.
3. **Bufferbloat sensitivity:** vary `--linkRate`, `--baseRtt`, and queue-disc choice; only add explicit queue-size parameters if the existing traces show the current model is too constrained.

Avoid adding wireless, LTE/5G, or mobility for this submission. They would make the paper look broader but weaker.

## Runner Examples

Conservative full matrix:

```bash
python3 paper/icns3-2026-tcp-aqm/scripts/run_tcp_aqm_sweep.py \
  --mode single-flow \
  --runs 3 \
  --stop-time 40s
```

Custom TypeId matrix:

```bash
python3 paper/icns3-2026-tcp-aqm/scripts/run_tcp_aqm_sweep.py \
  --mode single-flow \
  --tcp-types ns3::TcpCubic,ns3::TcpBbr \
  --queue-types codel,fq \
  --base-rtts 10ms,80ms \
  --ecns 0,1 \
  --runs 2 \
  --stop-time 40s
```

Competing-flow matrix:

```bash
python3 paper/icns3-2026-tcp-aqm/scripts/run_tcp_aqm_sweep.py \
  --mode competition \
  --pairs cubic:reno,cubic:dctcp,bbr:cubic \
  --runs 3 \
  --stop-time 50s
```
