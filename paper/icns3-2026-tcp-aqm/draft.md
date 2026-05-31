# Reproducible TCP and AQM Benchmarking in ns-3: A Baseline Study of Congestion Control, ECN, and Queue Discipline Interactions

## Abstract

Comparisons of TCP congestion control and active queue management in ns-3 are highly sensitive to topology, RTT, ECN configuration, queue discipline defaults, and random seeds. Yet many simulation studies report only a small number of manually executed scenarios, making it difficult to determine whether observed behavior is robust or an artifact of a particular setup. This paper presents a reproducible ns-3 benchmark workflow for TCP/AQM studies and demonstrates it through a baseline comparison of TCP Linux Reno, CUBIC, and DCTCP with CoDel, FQ-CoDel, PIE, and RED. The workflow expands an explicit experiment matrix, executes ns-3 runs with controlled random-run values, preserves command lines and environment metadata, stores raw trace outputs, and generates summary tables for throughput, RTT, queueing delay, congestion window, packet drops, and ECN marks. Using a separate benchmark example derived from ns-3's existing `tcp-validation` dumbbell topology, the study examines how conclusions change across base RTT and ECN settings. The artifact is designed to be rerun, audited, and extended by ns-3 users, providing both a reusable methodology and baseline results for future congestion-control and AQM evaluations.

## 1. Introduction

ns-3 is widely used to evaluate transport protocols and queue-management mechanisms, but TCP/AQM simulation results can be difficult to compare across papers. A conclusion such as "queue discipline X improves delay" or "congestion control Y better utilizes the bottleneck" depends on details that are easy to omit: the base RTT, queue-disc defaults, ECN configuration, traffic start time, random-run value, trace-processing method, and warmup interval. When those details are not preserved as runnable artifacts, reproducing or extending a study becomes unnecessarily fragile.

This paper focuses on TCP/AQM evaluation as an ns-3 methodology problem. Rather than proposing a new congestion-control algorithm, we contribute a reproducible benchmark workflow and use it to generate baseline results over ns-3's existing TCP and traffic-control models. The workflow records exact command lines, ns-3 commit metadata, run parameters, raw traces, and derived summary statistics. It is intentionally lightweight: it derives a separate benchmark example from the in-tree `tcp-validation` scenario so that researchers can reuse a known topology without changing the original validation example.

The case study compares TCP Linux Reno, CUBIC, and DCTCP across CoDel, FQ-CoDel, PIE, and RED. We vary base RTT and ECN support and report throughput, RTT, bottleneck queueing delay, congestion window behavior, drops, and ECN marks. The goal is not to declare a universal winner among queue disciplines. Instead, the goal is to show which conclusions are stable, which depend on experimental assumptions, and how ns-3 users can package TCP/AQM studies so that others can rerun them.

The paper makes four contributions:

1. A reproducible TCP/AQM benchmark harness for ns-3.
2. A traceable artifact structure that preserves command lines, random-run values, raw traces, and metadata.
3. A baseline comparison of TCP congestion-control and AQM interactions under multiple RTT and ECN settings.
4. Practical reporting recommendations for future ns-3 TCP/AQM studies.

## 2. Background and Motivation

The ns-3 TCP model includes multiple congestion-control algorithms, including Linux Reno, CUBIC, DCTCP, and BBR. This capability builds on a substantial redesign of ns-3 TCP architecture and later model contributions for both loss-based and model-based congestion control [Casoni2016NextGenerationTcp, Nguyen2016TcpVariants, Jain2018Bbr]. The traffic-control module introduces a Linux-like queue-disc layer in ns-3, enabling studies of CoDel, FQ-CoDel, PIE, RED, and related variants [Imputato2016TrafficControl, Shravya2016Pie]. These models make ns-3 a natural platform for transport and queue-management studies, but they also introduce a large configuration space.

TCP/AQM comparisons are particularly sensitive to hidden assumptions. ECN-enabled queues change whether congestion is signaled by marking or dropping. RTT changes the time required for a flow to reach steady state. Queue-disc defaults affect target delay, marking thresholds, and drop behavior. Some queue disciplines include randomness, so single-run results may overstate a conclusion. These sensitivities are not flaws in ns-3; they are reasons to make experiment design explicit and reproducible.

## 3. Related Work and Positioning

The ns-3 community has already produced substantial evaluation infrastructure for transport and queue-management studies. Mishra, Vankar, and Tahiliani introduced a TCP Evaluation Suite for ns-3 based on ICCRG evaluation guidance, automating simulation setup, topology creation, traffic generation, execution, and result collection for several TCP extensions and scenarios, including bandwidth, RTT, bottleneck, and long-flow-count variation [Mishra2016TcpEval]. Nagori et al. later presented a Common TCP Evaluation Suite for ns-3 that integrates Tmix realistic TCP traffic and validates its behavior against an ns-2 implementation, while also reporting open issues in the integration [Nagori2017CommonTcpEval].

AQM evaluation has also been addressed directly. Deepak, Shravya, and Tahiliani presented an AQM Evaluation Suite for ns-3 aligned with RFC 7928, automating topology construction, traffic generation, execution, result collection, and graphical output for AQM studies [Deepak2017AqmEval, Kuhn2016Rfc7928]. That work is especially close to this paper because it targets reproducible AQM comparisons rather than individual protocol design. More recently, the AQM Evaluation Suite has been modernized for ns-3.45, CMake, newer AQM and ECN functionality, and ns-3 App Store packaging [Ns3AppStore2025AqmEval, Lin2025GSoCAqmEval].

These prior systems mean that this paper should not be framed as the first TCP or AQM evaluation suite for ns-3. Instead, the contribution is deliberately narrower: we study how an in-tree, validated TCP example in current ns-3 can be turned into a lightweight benchmark artifact for TCP/AQM interaction studies. The benchmark uses `examples/tcp/tcp-validation.cc` as a validated anchor but implements the experiment artifact in `contrib/tcp-aqm-config/examples/tcp-aqm-benchmark.cc`, preserving raw traces and exact command provenance while examining whether TCP/AQM conclusions remain stable as RTT, ECN, queue discipline, flow mix, and random-run values change. In this sense, the work complements broad TCP and AQM evaluation suites by focusing on a small, auditable workflow that a user can rerun directly inside a current ns-3 checkout.

## 4. Benchmark Design

The benchmark uses the dumbbell topology from `examples/tcp/tcp-validation.cc`, implemented separately in `contrib/tcp-aqm-config/examples/tcp-aqm-benchmark.cc`. Servers connect through a WAN router to a bottleneck link, then through a LAN router to clients. The bottleneck link is configured with a selected queue discipline. TCP data flows traverse the bottleneck in the downstream direction, and a ping flow measures RTT. The benchmark example allows TCP variants to be selected using either short aliases or ns-3 TypeId names, while the original validation example remains unchanged.

The primary experiment matrix varies:

- TCP variant: Linux Reno, CUBIC, DCTCP.
- Queue discipline: CoDel, FQ-CoDel, PIE, RED.
- Base RTT: 10 ms, 50 ms, 80 ms.
- ECN: disabled or enabled at the bottleneck queue.
- Random run: one or more `RngRun` values.

The same harness can run additional TCP TypeIds, such as BBR, but those results should be reported as an extension unless the campaign includes enough repetitions and discussion to interpret model-based pacing behavior fairly.

The primary metrics are mean throughput, ping RTT, bottleneck queueing delay, congestion window, packet drops, and ECN marks after a warmup interval. Raw traces are retained so that additional metrics can be computed without rerunning simulations.

## 5. Workflow Implementation

The workflow consists of the `tcp-aqm-benchmark.cc` example plus two scripts. The C++ example resolves TCP aliases and TypeId names, configures the selected TCP variants per node, and keeps DCTCP-specific alpha tracing attached only when the resolved TypeId is DCTCP. The runner expands a named experiment matrix and invokes `./ns3 run` for each configuration. Each run receives an explicit `RngRun` value and stores:

- `metadata.json`, containing parameters, ns-3 commit, branch state, return code, and wall-clock time.
- `command.txt`, containing the exact ns-3 command.
- `stdout.txt` and `stderr.txt`.
- Raw `tcp-aqm-benchmark` trace files.

The analyzer reads each run directory and writes a campaign-level `summary.csv`. The summary includes throughput, RTT, queueing delay, congestion window, drop count, and mark count. A dependency-free SVG plotter creates quick figures from the summary table.

## 6. Initial Smoke Results

The smoke matrix contains 12 runs: CUBIC and DCTCP over CoDel and FQ-CoDel at 10 ms and 80 ms RTT, with ECN enabled where meaningful. The purpose of this matrix is to validate the workflow and expose obvious parameter issues before running the full campaign.

Early observations from the smoke matrix should be treated as sanity checks, not final claims. At 10 ms RTT, DCTCP with ECN reaches near-line-rate throughput with much lower mean queueing delay than CUBIC in the same short run. At 80 ms RTT, DCTCP requires a longer stop time before the throughput summary is meaningful, which directly motivates careful warmup and stop-time reporting in the full study.

Figure placeholders:

- `figures/throughput-smoke.svg`: mean throughput for the smoke matrix.
- `figures/queue-delay-smoke.svg`: mean bottleneck queueing delay for the smoke matrix.

## 7. Planned Full Evaluation

The full single-flow matrix should run Linux Reno, CUBIC, and DCTCP over CoDel, FQ-CoDel, PIE, and RED with base RTTs of 10 ms, 50 ms, and 80 ms. ECN-disabled DCTCP runs should be excluded from the main comparison or reported only as a warning case. Each configuration should be repeated across at least three random-run values; five is better if runtime permits.

The paper should also include a small competing-flow matrix, such as CUBIC vs Reno and CUBIC vs DCTCP, because AQM behavior is often more interesting when fairness and flow isolation are visible. This section can remain compact if space is tight.

## 8. Discussion

The discussion should focus on reproducibility lessons:

- Stop time and warmup must be reported, especially at larger RTTs.
- ECN configuration must be explicit at both TCP and queue-disc layers.
- Raw queue traces are important because throughput alone hides queueing-delay behavior.
- The exact ns-3 commit matters because TCP and queue-disc implementations continue to evolve.
- Seeded multi-run campaigns should be preferred over single-run claims when queue disciplines use randomness.

## 9. Conclusion

This paper presents a reproducible ns-3 workflow for TCP/AQM evaluation and demonstrates it with baseline results across TCP variants, queue disciplines, RTTs, and ECN settings. The contribution is both methodological and practical: a runnable artifact for ns-3 users and a set of baseline observations that future congestion-control and queue-management studies can compare against.
