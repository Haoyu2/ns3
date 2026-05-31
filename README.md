# ICNS3 2026 — Reproducible TCP/AQM Benchmarking in ns-3

This branch (`icns3-summer-2026-benchmark`) adds the code, data, and paper
sources for the ICNS3 2026 submission *From a Validation Example to a
Reproducible TCP/AQM Benchmark in ns-3*.

The upstream ns-3 source tree is intact. See [README-ns3.md](README-ns3.md) for
the original ns-3 project README.

---

## What this branch adds

| Path | What it is |
| --- | --- |
| [`contrib/tcp-aqm-config/`](contrib/tcp-aqm-config/) | New ns-3 contrib module: typed C++ experiment / topology configuration objects (YAML + JSON), a `tcp-aqm-benchmark` example derived from the in-tree `examples/tcp/tcp-validation.cc`, a `tcp-aqm-config-validate` config-checker, the Python harness under [`scripts/`](contrib/tcp-aqm-config/scripts/), and the [`results/`](contrib/tcp-aqm-config/results/) folder with all committed test data. |
| [`paper/icns3-2026-summer/`](paper/icns3-2026-summer/) | ICNS3 2026 paper sources (LaTeX, figures, references, rendered `main.pdf`). Originally an Overleaf-synced repo; the inner `.git` is preserved at `paper/icns3-2026-summer/.git-uno-overleaf-backup/` (gitignored) so the Overleaf workflow can be restored by renaming that directory back to `.git`. |
| [`README-ns3.md`](README-ns3.md) | The original upstream ns-3 README, renamed so the new top-level README is the guided tour. |

`examples/tcp/tcp-validation.cc` is **unchanged** — the benchmark inherits its
dumbbell topology but ships in the contrib module so the upstream validation
example stays intact.

---

## Quick start

Build the new contrib module and its two executables:

```bash
./ns3 configure
./ns3 build tcp-aqm-config tcp-aqm-benchmark tcp-aqm-config-validate
```

Validate one of the example configurations:

```bash
./ns3 run "tcp-aqm-config-validate \
  --configFile=contrib/tcp-aqm-config/configs/single-bottleneck.yaml"
```

Run a small single-flow smoke campaign and analyze it:

```bash
python3 contrib/tcp-aqm-config/scripts/run_tcp_aqm_sweep.py \
  --mode smoke --runs 1 --stop-time 10s --runner ns3 --overwrite \
  --results-dir contrib/tcp-aqm-config/results/smoke/single-flow
python3 contrib/tcp-aqm-config/scripts/analyze_tcp_aqm.py \
  --results-dir contrib/tcp-aqm-config/results/smoke/single-flow --warmup 5
```

The dependencies for the contrib module (`yaml-cpp`, `fmt`) install with:

```bash
# macOS
contrib/tcp-aqm-config/scripts/install-deps-macos.sh
# Ubuntu
contrib/tcp-aqm-config/scripts/install-deps-ubuntu.sh
```

Rebuild the paper PDF after editing LaTeX or regenerating figures:

```bash
cd paper/icns3-2026-summer && ./scripts/compile-pdf.sh --figures
```

---

## Guided tour

### `contrib/tcp-aqm-config/`

| File | Purpose |
| --- | --- |
| [`model/tcp-aqm-experiment-config.{h,cc}`](contrib/tcp-aqm-config/model/) | Typed C++ experiment configuration (TCP variant, queue discipline, RTT, ECN, jitter, sweep, analysis metrics). Parses YAML/JSON via `yaml-cpp`, validates, and pretty-prints with `fmt`. |
| [`model/tcp-aqm-topology-config.{h,cc}`](contrib/tcp-aqm-config/model/) | Typed topology configuration (single-bottleneck / two-bottleneck templates, link rates, queue sizes, derived per-bottleneck delays). |
| [`examples/tcp-aqm-benchmark.cc`](contrib/tcp-aqm-config/examples/tcp-aqm-benchmark.cc) | Paper-specific benchmark scenario derived from `examples/tcp/tcp-validation.cc`. Accepts both short aliases (`cubic`, `dctcp`) and full TypeIds (`ns3::TcpBbr`). Supports `--configFile=...` for YAML/JSON-driven runs, including `firstStartJitter` for stochastic single-flow campaigns. |
| [`examples/tcp-aqm-config-validate.cc`](contrib/tcp-aqm-config/examples/tcp-aqm-config-validate.cc) | Loads a config file and prints the normalized form. Useful for catching mistakes before launching a campaign. |
| [`configs/`](contrib/tcp-aqm-config/configs/) | YAML and JSON examples for the single-bottleneck and two-bottleneck templates, each including a one-parameter sweep block. |
| [`results/`](contrib/tcp-aqm-config/results/) | Canonical home for committed test data: per-campaign summary CSVs (`single-flow/`, `mixed-flow/`, `smoke/{config-sweep,mixed-flow,topology}/`), derived `evaluation/*.csv` tables, and `figures/*.svg`. See [`results/README.md`](contrib/tcp-aqm-config/results/README.md) for layout and end-to-end regeneration. |
| [`scripts/install-deps-{macos,ubuntu}.sh`](contrib/tcp-aqm-config/scripts/) | Installs `yaml-cpp` and `fmt`. |
| [`scripts/run_tcp_aqm_sweep.py`](contrib/tcp-aqm-config/scripts/run_tcp_aqm_sweep.py) | Expands a campaign matrix or a config-file sweep, runs `tcp-aqm-benchmark` once per configuration, and writes per-run `metadata.json` (parameters, ns-3 commit, per-file SHA-256, env snapshot, command line, wall-clock, return code), `command.txt`, `stdout.txt`/`stderr.txt`, and raw trace files. Default `--results-dir` is `contrib/tcp-aqm-config/results/single-flow`. Warns when the benchmark or contrib sources are dirty in git. |
| [`scripts/analyze_tcp_aqm.py`](contrib/tcp-aqm-config/scripts/analyze_tcp_aqm.py) | Reads run directories and emits per-run + aggregate `summary.csv` / `summary-aggregate.csv`. Computes throughput, ping RTT, queueing delay, congestion window, drops, marks, and Jain fairness (two-flow); clamps the warmup threshold to the flow start time. |
| [`scripts/analyze_core_evaluation.py`](contrib/tcp-aqm-config/scripts/analyze_core_evaluation.py) | Derives the core evaluation tables used by the paper: ECN impact, RTT throughput dispersion, ranking stability, throughput / delay Pareto frontier, mixed-flow share, FQ-CoDel vs. CoDel fairness, warmup sensitivity, and late-window stability. Writes to `contrib/tcp-aqm-config/results/evaluation/`. |
| [`scripts/plot_core_evaluation_svg.py`](contrib/tcp-aqm-config/scripts/plot_core_evaluation_svg.py) | Renders the paper-facing SVG figures (no third-party plotting dependency). Unified style: colourblind-safe palette, consistent margins, grouped per-campaign bar charts for the audit figures. Writes to `contrib/tcp-aqm-config/results/figures/`. |
| [`scripts/plot_summary_svg.py`](contrib/tcp-aqm-config/scripts/plot_summary_svg.py) | Quick-look inspection SVGs from `summary-aggregate.csv`. Writes to `contrib/tcp-aqm-config/results/figures/inspection/`. |
| [`CMakeLists.txt`](contrib/tcp-aqm-config/CMakeLists.txt) | Builds the contrib library and both example executables. |

### `paper/icns3-2026-summer/` — LaTeX paper sources

| File / dir | Purpose |
| --- | --- |
| [`main.tex`](paper/icns3-2026-summer/main.tex) | Top-level paper source (ACM `sigconf`, non-review mode). |
| [`sections/00-abstract.tex`](paper/icns3-2026-summer/sections/00-abstract.tex) through [`09-conclusion.tex`](paper/icns3-2026-summer/sections/09-conclusion.tex) | Body sections. |
| [`references.bib`](paper/icns3-2026-summer/references.bib) | Bibliography for the LaTeX paper. |
| [`fig/tcp-aqm/core-eval/`](paper/icns3-2026-summer/fig/tcp-aqm/core-eval/) | PDF + SVG figures referenced by the paper. PDFs are produced from the SVGs with `rsvg-convert` (see `scripts/compile-pdf.sh --figures`). |
| [`data/`](paper/icns3-2026-summer/data/) | CSV snapshots the paper references for archival, mirroring the artifact's `evaluation/`. |
| [`scripts/compile-pdf.sh`](paper/icns3-2026-summer/scripts/compile-pdf.sh) | Optional figure regeneration + `latexmk` driver. |
| [`scripts/setup-latex-deps.sh`](paper/icns3-2026-summer/scripts/setup-latex-deps.sh) | Installs `latexmk`, `rsvg-convert`, and the TeX Live ACM bundle. |
| [`main.pdf`](paper/icns3-2026-summer/main.pdf) | Rendered paper. Committed for review convenience even though the inner `.gitignore` would skip it locally. |
| [`IEEEtranBST/`](paper/icns3-2026-summer/IEEEtranBST/) | Vendored BibTeX style files used by the build. |
| [`.git-uno-overleaf-backup/`](paper/icns3-2026-summer/) (gitignored) | The detached inner repo's `.git` directory. Rename it back to `.git` to restore the Overleaf/UNO GitHub sync. |

---

## What was tested

### C++ contrib module

- `tcp-aqm-config`, `tcp-aqm-benchmark`, and `tcp-aqm-config-validate` all build cleanly on macOS with the bundled `cmake`/`ninja` setup.
- `tcp-aqm-config-validate` loads each shipped YAML/JSON example, fills derived bottleneck delays, validates, and prints the normalized configuration including the new `firstStartJitter` field.
- A directed smoke run (`cubic` / `codel` / `10ms` / `ECN on` / `firstStartJitter=1s` / `stopTime=10s`) executes end-to-end through the runner, emits the expected eight trace files, and records the new `source_sha256` (six pinned files) and `env` snapshot in `metadata.json`.
- Upstream validation cases inherited from `examples/tcp/tcp-validation.cc` (`dctcp-10ms`, `dctcp-80ms`, `cubic-50ms-no-ecn`, `cubic-50ms-ecn`) remain runnable via `--validate=...` against the new example.

### Python harness

- Five scripts pass `python3 -c "import ast; ast.parse(...)"` syntax checks.
- The runner's dirty-tree warning fires correctly when contrib sources are modified or untracked.
- The analyzer's warmup clamp pushes `--warmup 2` up to the flow start time (5 s by default), confirming pre-flow zero samples no longer deflate mean throughput.
- The grouped-bar renderer outputs both the single-flow group (top 12 of 60) and the mixed-flow group (top 6 of 12) for the warmup-sensitivity and stability-audit figures.

### Paper build

- `latexmk` produces a 7-page PDF cleanly with no errors and only minor (≤8 pt) hyphenation overfull boxes.
- All headline numbers in §6 (51.6 % / 32 % / 31 % RTT dispersion; 95.8 % / 88.3 % mixed-flow share; 0.733 → 0.9998 FQ-CoDel fairness; 32-of-72 warmup flag; 97-of-252 stability flag) reproduce exactly from the shipped CSVs in `paper/icns3-2026-summer/data/`.

---

## How to restore the Overleaf / UNO GitHub sync for the paper

The inner repo's `.git` was moved to `paper/icns3-2026-summer/.git-uno-overleaf-backup/` so the parent branch can track the paper sources as plain files. To resume pushing the paper to its original GitHub origin:

```bash
mv paper/icns3-2026-summer/.git-uno-overleaf-backup paper/icns3-2026-summer/.git
cd paper/icns3-2026-summer
git status   # should show the original Overleaf-synced branch
```

After restoring, the same `.tex` / figure files are simultaneously tracked by both the parent ns-3 branch and the inner paper repo. Commit to whichever is appropriate for the change.
