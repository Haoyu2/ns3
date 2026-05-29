# CAKE examples and validation harness

This file documents the CAKE-related programs and data files in this directory.
The rest of this directory's examples (`red-tests.cc`, `pie-example.cc`,
`codel-vs-pfifo-*.cc`, `fqcodel-l4s-example.cc`, etc.) predate this work and
are not described here.

For the user-facing description of the CAKE model itself, see
[`../doc/cake.rst`](../doc/cake.rst). For the model source, see
[`../model/`](../model/). For unit tests, see [`../test/`](../test/).

## Standalone demonstration

### `cake-bufferbloat-example.cc`
Single-bottleneck demo with CAKE acting as the shaper (not just as the AQM):
`sender → access (1 Gb/s, 1 ms) → router → bottleneck (100 Mb/s link, 10 ms)
→ receiver`, with CAKE installed on the router's bottleneck device at a 10 Mb/s
shaper rate and the TCP ACK identifier wired up via `MakeTcpAckIdentifier()`.
N long-lived Cubic flows saturate the bottleneck and FlowMonitor reports
per-flow throughput and one-way delay. Run as
`./ns3 run "cake-bufferbloat-example --simTime=30"`. This is the experiment
behind the "Shaping and bufferbloat control" table in the paper.

## AQM comparison campaign

### `cake-aqm-comparison.cc`
Parametrised single-bottleneck benchmark used to compare CAKE against the
other ns-3 AQMs (`FqCoDel`, `FqCobalt`, `Pie`, `PfifoFast`). Same topology and
workload for every qdisc; all see the same link-rate bottleneck so the
comparison isolates AQM / flow-isolation behaviour (CAKE runs in unlimited
mode here — the integrated shaper is exercised by the bufferbloat example
above). Args: `--qdisc {Cake|FqCoDel|FqCobalt|Pie|PfifoFast} --bandwidth --rtt
--nFlows --simTime --seed`. Prints one machine-readable line:
`RESULT,qdisc,bw,rtt,nFlows,seed,aggMbps,jain,probeMeanMs,probeJitterMs,drops`.

### `cake-aqm-campaign.py`
Python sweep driver for the comparison program: iterates qdisc × bandwidth ×
RTT × seed, runs the built binary per combination, writes a tidy CSV, and
prints a mean ± stdev summary table. Run as
`python3 cake-aqm-campaign.py [--quick]` from the ns-3 source root. Used to
generate `paper/aqm-sweep.csv` (paper Figure 2).

## Linux `sch_cake` validation harness

### `cake-rrul-validation.cc`
rrul-like ns-3 scenario that mirrors Flent's *rrul* test: bidirectional bulk
TCP over a CAKE-shaped bottleneck with a UDP echo RTT probe. Used by
`cake-validation-compare.py` to validate ns-3 against Linux.
Args of note:
* `--bandwidth` / `--upRate` — per-direction CAKE rate (upRate = 0 means
  symmetric).
* `--nFlows` / `--downFlows` / `--upFlows` — number of TCP flows per
  direction (defaults preserve the symmetric rrul behaviour).
* `--ackFilter` — toggle CAKE's ACK filter (default on).
* `--rttout <file>` — dump per-sample RTTs (one per line) for offline CDF
  generation.

### `cake-validation-compare.py`
Runs the rrul scenario for a reference JSON's parameters, loads the
captured Linux numbers, prints per-metric relative error and a PASS/FAIL
verdict against a tolerance (default 20%), and exits non-zero on failure
(suitable as a reproducibility gate in CI). Usage:
```
./ns3 build cake-rrul-validation
python3 cake-validation-compare.py \
    --reference cake-validation-linux-10mbit-40ms.json
```

### `cake-linux-testbed.sh`
Network-namespace testbed for the **Linux** side of the validation. Creates
`client`, `router`, `server` namespaces with veth pairs, applies `tc cake` on
the router's two egress interfaces and `netem` at the endpoints, runs
Flent's *rrul* test, and saves a `.flent.gz`. Supports asymmetric shaping
(`--up-rate`) and toggling the CAKE ACK filter (`--ack-filter on|off`).
Requires Linux ≥ 4.19, `iproute2` with cake support, `flent`, `netperf`,
`fping`. Run as root.

### `cake-flent-extract.py`
Turns a captured `.flent.gz` into a small reference JSON consumed by
`cake-validation-compare.py`. Prefers Flent's own `--format=summary` values
for throughput (authoritative); computes RTT percentiles from the raw ping
series.

## Reference data (Linux numbers)

`cake-validation-linux-*.json` — Linux `sch_cake` references captured on a
network-namespace testbed, one per operating point:

* `1mbit-40ms`, `5mbit-40ms`, `10mbit-40ms`, `20mbit-100ms`, `50mbit-80ms`,
  `100mbit-20ms`, `100mbit-80ms` — the seven symmetric rrul points in the
  paper's validation table.
* `asym-50-1mbit-40ms-ackon.json` / `-ackoff.json` — asymmetric (50 Mb/s
  down / 1 Mb/s up) with CAKE's ACK filter on and off, used in the paper's
  ACK-filtering experiment.

Schema:
```json
{
  "scenario":  { "bandwidth_mbps": <n>, "rtt_ms": <n>, "nflows": <n> },
  "linux_cake": {
    "down_mbps": <n>, "up_mbps": <n>,
    "rtt_p50_ms": <n>, "rtt_p90_ms": <n>, "rtt_p99_ms": <n>
  },
  "source": "<provenance string>"
}
```

`cake-validation-reference.example.json` is a template with synthetic
placeholder numbers, intended as a starting point for new captures.
