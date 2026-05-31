# Related Work and Positioning Notes

## Closest Prior ns-3 Work

| Work | What it contributes | How our paper should position against it |
| --- | --- | --- |
| Casoni and Patriciello, "Next-generation TCP for ns-3 simulator," SIMPAT 2016 | Documents the TCP module redesign that made ns-3 more suitable for TCP-centered research, including congestion-control separation, testing support, and an example TCP/CoDel case study. | Use this as architecture background. Our paper relies on the modern TCP architecture rather than modifying TCP internals. |
| Imputato and Avallone, "Design and Implementation of the Traffic Control Module in ns-3," WNS3 2016 | Introduces the Linux-like Traffic Control layer and queue-disc base abstraction used by modern ns-3 AQM studies. | Use this to ground the queue-disc side of the paper. Our workflow compares models exposed by this layer rather than proposing a new queue-disc architecture. |
| Shravya, Murali, and Tahiliani, "Implementation and Evaluation of PIE Algorithm in ns-3," WNS3 2016 | Implements and evaluates the PIE AQM model in ns-3, validating behavior against the ns-2 PIE model. | Cite as model-level prior work for one queue discipline in our matrix. We should treat PIE as an existing ns-3 model, not a contribution of this paper. |
| Nguyen et al., "An Implementation of Scalable, Vegas, Veno, and YeAH Congestion Control Algorithms in ns-3," WNS3 2016 | Adds and validates additional TCP congestion-control models in ns-3. | Cite as evidence that ns-3 has a long history of TCP model work; our benchmark should eventually accept more TCP TypeIds, but the paper can begin with Reno, CUBIC, and DCTCP. |
| Jain, Mittal, and Tahiliani, "Design and Implementation of TCP BBR in ns-3," WNS3 2018 | Adds TCP BBR in ns-3 and validates it against Linux BBR using NeST. | This is a useful optional extension path. We can mention BBR as future matrix expansion unless we add it to `tcp-aqm-benchmark.cc` and rerun results. |
| Mishra, Vankar, and Tahiliani, "TCP Evaluation Suite for ns-3," WNS3 2016 | Implements an ICCRG-inspired TCP evaluation suite in ns-3, automating setup, topology creation, traffic generation, execution, and result collection for TCP congestion-control comparisons. It includes single and multiple bottleneck topologies, bottleneck bandwidth and RTT variation, and long-flow-count variation. | We should not claim that automation for TCP evaluation is new. Our narrower contribution is a current-ns-3, artifact-first TCP/AQM benchmark that derives from the in-tree `tcp-validation.cc` validation scenario while keeping the artifact implementation separate in `tcp-aqm-benchmark.cc`. |
| Nagori et al., "Common TCP Evaluation Suite for ns-3: Design, Implementation and Open Issues," WNS3 2017 | Extends the TCP evaluation direction with Tmix-based realistic synthetic TCP traffic, connection-vector handling, architecture, and validation against an ns-2 implementation. | We should describe this as complementary. It targets realistic TCP traffic generation and cross-simulator validation; our paper targets lightweight, reproducible benchmarking of current in-tree TCP/AQM models and makes no claim to replace Tmix or full ICCRG-style evaluation. |
| Deepak, Shravya, and Tahiliani, "Design and Implementation of AQM Evaluation Suite for ns-3," WNS3 2017 | Implements an RFC 7928-aligned AQM evaluation framework for ns-3, automating topology, traffic generation, execution, result collection, and visualization. | This is the closest AQM prior work. Our paper must avoid claiming a first AQM suite. The gap we can own is the intersection of TCP congestion control and AQM behavior in modern ns-3, using a validated in-tree TCP scenario with explicit ECN, RTT, queue-disc, seed, and trace provenance. |
| ns-3 App Store / GSoC 2025 AQM Evaluation Suite upgrade | Modernizes the AQM Evaluation Suite for ns-3.45, CMake, newer AQM/ECN functionality, and App Store packaging. | This makes the "new AQM framework" claim even weaker. It strengthens our need to focus on a smaller, auditable benchmark artifact and on lessons from deriving a separate benchmark from `tcp-validation.cc` rather than building another broad suite. |
| WNS3 2021 NeST lightning talk on validating congestion control mechanisms and queue disciplines | Signals continued community interest in validating TCP and queue-disc behavior, possibly against external/emulated Linux behavior. | We can cite it as adjacent validation work, but our current contribution should be reproducible ns-3 experiment methodology and baseline results rather than external-system validation. |

## Positioning We Can Defend

The safest novelty claim is:

> Prior ns-3 work provides broad TCP and AQM evaluation suites. This paper instead asks how far an in-tree, validated TCP example can be turned into a lightweight, reproducible benchmark for modern TCP/AQM interaction studies, and what reporting practices are needed for conclusions to survive changes in RTT, ECN, queue discipline, flow mix, and random seed.

Concrete differences to emphasize:

1. **Validated in-tree anchor:** Start from `examples/tcp/tcp-validation.cc`, preserving the original file while deriving a separate benchmark scenario with the same validated topology.
2. **TCP/AQM interaction matrix:** Cross TCP variants, queue disciplines, ECN settings, RTTs, and optional competing flows in one common workflow.
3. **Artifact-first reproducibility:** Store exact commands, git metadata, raw traces, stdout/stderr, random-run values, and summary tables for each run.
4. **Scenario-sensitivity results:** Report which conclusions change when RTT, ECN, or flow mix changes, instead of presenting a single universal ranking.
5. **Current ns-3 practical guidance:** Document what modern ns-3 users must report when comparing TCP and queue-disc behavior.

## Avoid These Claims

- "First TCP evaluation suite for ns-3."
- "First AQM evaluation suite for ns-3."
- "Complete RFC 7928 implementation."
- "Universal ranking of CoDel, FQ-CoDel, PIE, and RED."
- "Validation against real deployments" unless we actually add an external validation component.

## Useful Source Links

- TCP Evaluation Suite for ns-3, WNS3 2016: https://eudl.eu/doi/10.1145/2915371.2915388
- Next-generation TCP for ns-3 simulator, SIMPAT 2016: https://iris.unimore.it/handle/11380/1098855
- Traffic Control Module in ns-3, WNS3 2016: https://www.iris.unina.it/handle/11588/656821
- PIE Algorithm in ns-3, WNS3 2016: https://eudl.eu/doi/10.1145/2915371.2915385
- Scalable/Vegas/Veno/YeAH in ns-3, WNS3 2016: https://eudl.eu/doi/10.1145/2915371.2915386
- TCP BBR in ns-3, WNS3 2018: https://dblp.org/rec/conf/wns3/JainMT18.html
- Common TCP Evaluation Suite for ns-3, WNS3 2017: https://idr.nitk.ac.in/items/a625ccf6-8db0-45bb-88e4-607997a98f96
- AQM Evaluation Suite paper, WNS3 2017: https://eudl.eu/doi/10.1145/3067665.3067674
- AQM Evaluation Suite project page: https://aqm-eval-suite.github.io/
- ns-3 App Store AQM Evaluation Suite: https://apps.nsnam.org/app/aqm-evaluation-suite/
- GSoC 2025 AQM Evaluation Suite upgrade: https://www.nsnam.org/wiki/GSOC2025AqmEvaluation
- WNS3 2021 program with NeST validation lightning talk: https://www.nsnam.org/research/wns3/wns3-2021/program/
