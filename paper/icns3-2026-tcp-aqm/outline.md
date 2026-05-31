# Paper Outline

## 1. Introduction

Network simulation papers often compare congestion-control or queue-management mechanisms using a small number of manually chosen scenarios. In ns-3, TCP and traffic-control models are rich enough to support more systematic studies, but the scripts, seeds, command lines, and post-processing steps are often scattered across a repository. This makes it hard to distinguish robust conclusions from scenario-specific artifacts.

This paper presents a reproducible ns-3 benchmark workflow for TCP and active queue management studies. The workflow is demonstrated through a baseline comparison of TCP Linux Reno, CUBIC, and DCTCP across CoDel, FQ-CoDel, PIE, and RED under multiple RTT and ECN settings.

Contributions:

1. A reproducible ns-3 TCP/AQM experiment harness.
2. A traceable artifact layout that preserves command lines, seeds, metadata, and raw traces.
3. A baseline TCP/AQM comparison that reports throughput, RTT, queue delay, drops, marks, and congestion window behavior.
4. Practical recommendations for reporting TCP/AQM results in future ns-3 studies.

## 2. Background and Motivation

Discuss:

- ns-3 TCP congestion-control models.
- ns-3 traffic-control layer and queue discs.
- Why TCP/AQM comparisons are sensitive to RTT, ECN, queue settings, and seeds.
- Why a reproducible benchmark suite is useful to the ns-3 community.

## 3. Related Work and Positioning

Key prior work:

- TCP Evaluation Suite for ns-3, WNS3 2016: ICCRG-style TCP evaluation automation.
- Common TCP Evaluation Suite for ns-3, WNS3 2017: Tmix realistic traffic and ns-2 validation.
- AQM Evaluation Suite for ns-3, WNS3 2017: RFC 7928-aligned AQM evaluation automation.
- AQM Evaluation Suite upgrade, GSoC/ns-3 App Store 2025: modern CMake/ns-3.45 packaging and ECN/AQM updates.
- NeST validation work, WNS3 2021 lightning talk: adjacent validation of congestion control and queue disciplines.

Positioning:

- Do not claim a first TCP or AQM suite.
- Claim a lightweight, current-ns-3 benchmark artifact derived from the in-tree `tcp-validation.cc` validation example and implemented separately as `tcp-aqm-benchmark.cc`.
- Emphasize TCP/AQM interaction, scenario sensitivity, raw trace preservation, command provenance, and reproducible reporting.

## 4. Benchmark Design

Scenario:

- Dumbbell topology from `examples/tcp/tcp-validation.cc`, implemented separately in `contrib/tcp-aqm-config/examples/tcp-aqm-benchmark.cc`.
- Extended TCP selection accepts aliases and TypeIds, while preserving validation cases.
- One or two TCP data flows over a bottleneck link.
- Optional ping flow to measure RTT.
- Bottleneck queue selected from CoDel, FQ-CoDel, PIE, RED.
- ECN enabled or disabled at the bottleneck queue.

Factors:

- TCP variant: Linux Reno, CUBIC, DCTCP.
- Optional extension: BBR or any supported `TcpCongestionOps` TypeId.
- Queue discipline: CoDel, FQ-CoDel, PIE, RED.
- Base RTT: 10 ms, 50 ms, 80 ms.
- ECN: off/on.
- Random run: 1..N.

Metrics:

- Mean TCP throughput after warmup.
- Mean and tail ping RTT.
- Mean and tail queueing delay at bottleneck.
- Drop count.
- ECN mark count.
- Mean congestion window.

## 5. Workflow Implementation

Describe:

- Generic TCP TypeId resolution in `tcp-aqm-benchmark.cc`.
- Runner expands the matrix and invokes `./ns3 run`.
- Each run receives `RngRun`.
- Each run stores `metadata.json`, command line, stdout/stderr, and raw trace files.
- Analyzer emits `summary.csv` and optional follow-on plot inputs.

Important reproducibility choices:

- Use ns-3 commit hash and version in metadata.
- Store full command line for every run.
- Do not overwrite old campaigns by default.
- Keep raw traces, not only aggregate CSV.

## 6. Results

Expected figures:

1. Throughput by queue discipline and TCP variant.
2. RTT or queueing delay by queue discipline.
3. Drop and mark counts by queue discipline and ECN setting.
4. Sensitivity to base RTT.
5. Optional fairness plot for two-flow runs.

Early result claims to test, not assume:

- FQ-CoDel may reduce queueing delay and improve flow isolation relative to single-queue CoDel.
- DCTCP should only be meaningful when ECN marking is enabled.
- RED and PIE results may be more sensitive to random seed and parameter defaults.
- CUBIC with ECN may maintain throughput while reducing loss-driven backoff.

## 7. Discussion

Focus on what this teaches ns-3 users:

- Which defaults are safe for baseline comparisons.
- Which settings must be reported.
- How many seeds are needed before claims stabilize.
- How queue-disc trace outputs can be used in reproducibility artifacts.

## 8. Artifact and Reproducibility

Include:

- Repository structure.
- Commands to reproduce smoke and full results.
- ns-3 version and commit.
- Hardware/compiler notes.
- Output files and summary-table schema.

## 9. Conclusion

Summarize that the main value is not a universal ranking of TCP/AQM combinations, but a repeatable ns-3 workflow and baseline results that make future TCP/AQM studies easier to compare.
