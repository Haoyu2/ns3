# ns-3 CAKE — Comprehensive Queue Management Discipline

This branch (`ns3-summer-2026`) adds the **first model of CAKE** to ns-3.
CAKE (Common Applications Kept Enhanced) is the state-of-the-art queue
discipline behind OpenWrt's Smart Queue Management, widely deployed on home
gateways. It integrates rate shaping, per-flow active queue management,
DiffServ prioritisation, ACK filtering, and host-fair triple isolation into a
single qdisc. Until now ns-3 had every other modern AQM but not CAKE; this
work closes that gap and validates the model against Linux `sch_cake` using
Flent's *rrul* test.

The model is implemented in the `traffic-control` module by reusing the
existing COBALT AQM. Two header-aware mechanisms (ACK filtering and host
fairness) are wired up through **injected callbacks**, so the model never
references types in the `internet` module — a small pattern that respects
ns-3's module layering and that may be useful when porting other
header-aware Linux qdiscs.

> The upstream ns-3 project README (build prerequisites, supported
> platforms, manual pointers) is preserved verbatim as
> [`README-ns3.md`](README-ns3.md).

## What lives where

```
src/traffic-control/
├── model/cake-queue-disc.{h,cc}            # the CAKE model
├── model/README.md                         # developer notes on the model
├── test/cake-queue-disc-test-suite.cc      # 8 unit tests
├── test/README.md                          # per-test description
├── examples/cake-*.{cc,py,sh,json}         # examples + validation harness
├── examples/README.md                      # per-script description
└── doc/cake.rst                            # Sphinx user docs

src/internet/helper/
└── cake-ack-identifier.{h,cc}              # internet-side header helpers:
                                            # MakeTcpAckIdentifier()
                                            # MakeIpv4HostClassifier()

paper/
├── cake-icns3.tex                          # ICNS3 paper source
├── cake-icns3.pdf                          # (gitignored; rebuild as below)
├── cake-slides.tex                         # presentation slides
├── references.bib
├── *.dat                                   # figure data (validation, CDFs,
                                            # AQM latency-vs-rate)
└── aqm-sweep.csv                           # raw AQM campaign output
```

## Build and run

The model only needs the `traffic-control`, `internet`, `point-to-point`,
`applications` and `flow-monitor` modules. A reduced configure keeps the
build fast:

```sh
./ns3 configure --enable-tests --enable-examples \
    --enable-modules=traffic-control,internet,point-to-point,applications,flow-monitor
./ns3 build
```

(`--enable-modules=...` is optional; the configuration above just keeps the
compile small. A standard `./ns3 configure --enable-tests --enable-examples`
also works.)

Run the unit tests for CAKE:

```sh
./test.py -s cake-queue-disc
```

All eight cases (single-flow FIFO, two-flow DRR, deficit shaper pacing,
unlimited-mode bypass, DiffServ tin priority, ACK filtering, host fairness,
per-tin rate caps) should pass in well under a second.

Run the bufferbloat-control demonstration:

```sh
./ns3 run "cake-bufferbloat-example --simTime=30"
```

Run the AQM comparison campaign that produces the paper's Figure 2:

```sh
python3 src/traffic-control/examples/cake-aqm-campaign.py --quick
```

## Validation against Linux `sch_cake`

The `examples/` directory ships a reproducible validation harness:

* `cake-linux-testbed.sh` — bring up a network-namespace testbed and run
  Flent's *rrul* test against `tc qdisc … cake`. Linux only; requires
  `flent`, `netperf`, `fping` and a kernel ≥ 4.19.
* `cake-flent-extract.py` — turn the resulting `.flent.gz` into a reference
  JSON.
* `cake-validation-compare.py` — run the ns-3 rrul scenario for the
  reference's parameters, print a per-metric relative-error table, and exit
  non-zero on failure (suitable as a reproducibility gate).

The seven committed `cake-validation-linux-*.json` files are the Linux
references from the paper's validation table; two more (`asym-*-ackon/off`)
are the asymmetric ACK-filtering references. Reproduce a comparison locally:

```sh
./ns3 build cake-rrul-validation
python3 src/traffic-control/examples/cake-validation-compare.py \
    --reference src/traffic-control/examples/cake-validation-linux-10mbit-40ms.json
```

Full step-by-step capture instructions are in the
[`examples/README.md`](src/traffic-control/examples/README.md) and in
[`src/traffic-control/doc/cake.rst`](src/traffic-control/doc/cake.rst).

## Docs and Results

The paper is LaTeX (acmart, pgfplots). Build from `paper/`:

```sh
cd paper
pdflatex cake-icns3 && bibtex cake-icns3 && pdflatex cake-icns3 && pdflatex cake-icns3
pdflatex cake-slides && pdflatex cake-slides
```

The figures pull their data directly from the checked-in `paper/*.dat`
files, so the figures in the paper are guaranteed to match the figures in
the slides.

## Per-folder documentation

For more detail on each part of the work see the folder-level READMEs:

* [`src/traffic-control/model/README.md`](src/traffic-control/model/README.md)
  — class hierarchy, mechanism walkthrough, attribute table.
* [`src/traffic-control/test/README.md`](src/traffic-control/test/README.md)
  — what each of the eight unit tests covers.
* [`src/traffic-control/examples/README.md`](src/traffic-control/examples/README.md)
  — every script and reference JSON described.

## Authors

* **Haoyu Wang** — University of Massachusetts Boston —
  haoyu.wang001@umb.edu
* **Bo Sheng** — University of Massachusetts Boston — bo.sheng@umb.edu
* **Xiaoqian Zhang** — University of Nebraska Omaha —
  xiaoqianzhang@unomaha.edu

Comments, bug reports, and merge-request feedback welcome.
