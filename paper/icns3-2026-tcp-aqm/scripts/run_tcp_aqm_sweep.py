#!/usr/bin/env python3
"""Run reproducible TCP/AQM sweeps using the tcp-aqm-benchmark example."""

from __future__ import annotations

import argparse
import copy
import hashlib
import itertools
import json
import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path


# Files the runner pins per run with a SHA-256 hash so a dirty working tree does
# not silently invalidate the recorded artifact. Paths are relative to repo root.
PINNED_SOURCE_FILES = (
    "contrib/tcp-aqm-config/examples/tcp-aqm-benchmark.cc",
    "contrib/tcp-aqm-config/examples/tcp-aqm-config-validate.cc",
    "contrib/tcp-aqm-config/model/tcp-aqm-experiment-config.h",
    "contrib/tcp-aqm-config/model/tcp-aqm-experiment-config.cc",
    "contrib/tcp-aqm-config/model/tcp-aqm-topology-config.h",
    "contrib/tcp-aqm-config/model/tcp-aqm-topology-config.cc",
)

# Environment variables that can change ns-3 behaviour without showing up in
# argv. Captured into metadata so reviewers can detect host-specific drift.
PINNED_ENV_VARS = (
    "NS_LOG",
    "NS_GLOBAL_VALUE",
    "NS_LOG_LEVEL",
    "NS3_LOG_LEVEL",
    "LD_LIBRARY_PATH",
    "DYLD_LIBRARY_PATH",
    "PYTHONPATH",
    "PATH",
    "CC",
    "CXX",
    "CCACHE_DISABLE",
    "OMP_NUM_THREADS",
)


TRACE_PREFIXES = ["tcp-aqm-benchmark", "tcp-validation"]
TRACE_SUFFIXES = [
    "ping.dat",
    "first-tcp-rtt.dat",
    "first-tcp-cwnd.dat",
    "first-dctcp-alpha.dat",
    "first-tcp-throughput.dat",
    "second-tcp-rtt.dat",
    "second-tcp-cwnd.dat",
    "second-tcp-throughput.dat",
    "second-dctcp-alpha.dat",
    "queue-mark.dat",
    "queue-drop.dat",
    "queue-marks-frequency.dat",
    "queue-length.dat",
]
TRACE_FILES = [f"{prefix}-{suffix}" for prefix in TRACE_PREFIXES for suffix in TRACE_SUFFIXES]


def safe_token(value: object) -> str:
    # "." is preserved as "-" (not stripped) so that fractional tokens such as
    # "1.5ms" stay distinct from "15ms" in run-directory names. The final
    # `.strip("-")` removes leading/trailing dashes that arise when the value
    # itself starts or ends with one of the substituted characters.
    return (
        str(value)
        .replace("ns3::", "")
        .replace("::", "-")
        .replace("/", "-")
        .replace(" ", "")
        .replace(".", "-")
        .replace("[", "")
        .replace("]", "")
        .replace(":", "-")
        .strip("-")
    )


@dataclass(frozen=True)
class RunConfig:
    first_tcp: str
    queue: str
    base_rtt: str
    ecn: bool
    rng_run: int
    stop_time: str
    link_rate: str
    second_tcp: str = ""
    topology: str = ""
    first_start_time: str = "5s"
    second_start_time: str = "15s"
    second_start_jitter: str = "0s"
    first_start_jitter: str = "0s"

    def run_id(self) -> str:
        pair = safe_token(self.first_tcp)
        if self.second_tcp:
            pair = f"{pair}-vs-{safe_token(self.second_tcp)}"
        topology = f"topology-{safe_token(self.topology)}_" if self.topology else ""
        return (
            f"{topology}tcp-{pair}_queue-{safe_token(self.queue)}_rtt-{safe_token(self.base_rtt)}_"
            f"ecn-{int(self.ecn)}_run-{self.rng_run}"
        )


@dataclass(frozen=True)
class RunSpec:
    config: RunConfig
    document: dict | None = None
    sweep: dict[str, object] = field(default_factory=dict)
    analysis_metrics: tuple[str, ...] = ()
    run_id_override: str = ""

    def run_id(self) -> str:
        return self.run_id_override or self.config.run_id()


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[3]


def get_git_metadata(repo: Path) -> dict[str, str]:
    def run_git(args: list[str]) -> str:
        result = subprocess.run(
            ["git", *args],
            cwd=repo,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        return result.stdout.strip()

    return {
        "commit": run_git(["rev-parse", "HEAD"]),
        "branch": run_git(["branch", "--show-current"]) or "detached",
        "status_short": run_git(["status", "--short"]),
    }


def hash_file(path: Path) -> str:
    """Return the SHA-256 hex digest of a file, or empty string if missing."""
    if not path.is_file():
        return ""
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def get_source_hashes(repo: Path, extra_paths: tuple[str, ...] = ()) -> dict[str, str]:
    """Per-file SHA-256 for pinned sources plus any extras (e.g., config file).

    Recording these per-file hashes means a dirty or untracked working tree
    does not silently invalidate the artifact: the exact bytes that produced
    each run are pinned even when the git commit is stale.
    """
    paths = list(PINNED_SOURCE_FILES) + [str(p) for p in extra_paths]
    return {rel: hash_file(repo / rel) for rel in paths}


def get_env_snapshot() -> dict[str, str]:
    """Snapshot ns-3-relevant environment variables for reproducibility audits."""
    return {name: os.environ[name] for name in PINNED_ENV_VARS if name in os.environ}


def warn_if_dirty_sources(repo: Path) -> None:
    """Print a warning when any pinned source is modified or untracked.

    The runner does not refuse to run on a dirty tree because the per-file SHA
    pinning still makes the artifact reproducible. The warning makes the
    condition visible so reviewers know to inspect ``git`` status before
    interpreting the recorded commit.
    """
    status = subprocess.run(
        ["git", "status", "--porcelain", "--", *PINNED_SOURCE_FILES],
        cwd=repo,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )
    lines = [line for line in status.stdout.splitlines() if line.strip()]
    if lines:
        print(
            "WARNING: pinned benchmark/contrib sources are modified or untracked.\n"
            "The recorded git commit will not reflect the running code; per-file\n"
            "SHA-256 hashes are stored in metadata.json to pin the actual bytes.\n"
            "Dirty paths:\n  " + "\n  ".join(lines),
            file=sys.stderr,
            flush=True,
        )


def parse_csv(value: str | None, default: list[str]) -> list[str]:
    if value is None:
        return default
    parsed = [item.strip() for item in value.split(",") if item.strip()]
    if not parsed:
        raise ValueError("comma-separated option cannot be empty")
    return parsed


def parse_ecns(value: str | None, default: list[bool]) -> list[bool]:
    tokens = parse_csv(value, ["1" if ecn else "0" for ecn in default])
    parsed = []
    for token in tokens:
        normalized = token.lower()
        if normalized in {"1", "true", "on", "yes", "ecn"}:
            parsed.append(True)
        elif normalized in {"0", "false", "off", "no", "noecn"}:
            parsed.append(False)
        else:
            raise ValueError(f"unsupported ECN value: {token}")
    return parsed


def parse_pairs(value: str | None, default: list[tuple[str, str]]) -> list[tuple[str, str]]:
    if value is None:
        return default
    pairs = []
    for token in parse_csv(value, []):
        if ":" not in token:
            raise ValueError(f"pair must be first:second, got {token}")
        first, second = [part.strip() for part in token.split(":", 1)]
        if not first or not second:
            raise ValueError(f"pair must be first:second, got {token}")
        pairs.append((first, second))
    return pairs


def parse_topologies(value: str | None, default: list[str]) -> list[str]:
    topologies = parse_csv(value, default)
    allowed = {"single-bottleneck", "two-bottleneck"}
    unsupported = [topology for topology in topologies if topology not in allowed]
    if unsupported:
        raise ValueError(f"unsupported topology value(s): {', '.join(unsupported)}")
    return topologies


def parse_bool_value(value: object) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, int):
        return bool(value)
    normalized = str(value).strip().lower()
    if normalized in {"1", "true", "on", "yes", "ecn"}:
        return True
    if normalized in {"0", "false", "off", "no", "noecn", ""}:
        return False
    raise ValueError(f"unsupported boolean value: {value}")


def load_config_document(path: Path) -> dict:
    if path.suffix.lower() == ".json":
        document = json.loads(path.read_text(encoding="utf-8"))
    else:
        try:
            import yaml  # type: ignore[import-not-found]
        except ImportError as exc:
            raise SystemExit(
                "YAML config sweeps require PyYAML for the Python runner; use JSON or install PyYAML"
            ) from exc
        document = yaml.safe_load(path.read_text(encoding="utf-8"))
    if not isinstance(document, dict):
        raise ValueError(f"config file must contain a mapping: {path}")
    return document


def canonical_parameter(parameter: str) -> str:
    return parameter.strip().lower().replace("-", "").replace("_", "")


def bottleneck_parameter_parts(parameter: str) -> tuple[str, int | None]:
    match = re.search(r"topology\.bottlenecks\[(\d+)\]\.(rate|delay|queuetype)$", parameter.lower())
    if match:
        return f"topology.bottlenecks.{match.group(2)}", int(match.group(1))
    return parameter, None


def ensure_section(document: dict, name: str) -> dict:
    section = document.setdefault(name, {})
    if not isinstance(section, dict):
        raise ValueError(f"{name} section must be a mapping")
    return section


def apply_sweep_value(document: dict, parameter: str, value: object, bottleneck_index: int | None) -> None:
    parameter, index_from_path = bottleneck_parameter_parts(parameter)
    if index_from_path is not None:
        bottleneck_index = index_from_path
    key = canonical_parameter(parameter)
    experiment = ensure_section(document, "experiment")
    topology = ensure_section(document, "topology")

    experiment_targets = {
        "experiment.firsttcptype": "firstTcpType",
        "firsttcptype": "firstTcpType",
        "experiment.secondtcptype": "secondTcpType",
        "secondtcptype": "secondTcpType",
        "experiment.queuetype": "queueType",
        "queuetype": "queueType",
        "experiment.basertt": "baseRtt",
        "basertt": "baseRtt",
        "experiment.cethreshold": "ceThreshold",
        "cethreshold": "ceThreshold",
        "experiment.linkrate": "linkRate",
        "linkrate": "linkRate",
        "experiment.stoptime": "stopTime",
        "stoptime": "stopTime",
        "experiment.firststarttime": "firstStartTime",
        "firststarttime": "firstStartTime",
        "experiment.secondstarttime": "secondStartTime",
        "secondstarttime": "secondStartTime",
        "experiment.secondstartjitter": "secondStartJitter",
        "secondstartjitter": "secondStartJitter",
        "experiment.firststartjitter": "firstStartJitter",
        "firststartjitter": "firstStartJitter",
        "experiment.queueuseecn": "queueUseEcn",
        "queueuseecn": "queueUseEcn",
        "ecn": "queueUseEcn",
        "experiment.rngrun": "rngRun",
        "rngrun": "rngRun",
    }
    topology_targets = {
        "topology.kind": "kind",
        "topologykind": "kind",
        "topology.accessrate": "accessRate",
        "accessrate": "accessRate",
        "topology.accessdelay": "accessDelay",
        "accessdelay": "accessDelay",
        "topology.devicequeuesize": "deviceQueueSize",
        "devicequeuesize": "deviceQueueSize",
    }
    bottleneck_targets = {
        "topology.bottlenecks.rate": "rate",
        "bottleneckrate": "rate",
        "topology.bottlenecks.delay": "delay",
        "bottleneckdelay": "delay",
        "topology.bottlenecks.queuetype": "queueType",
        "bottleneckqueuetype": "queueType",
    }

    if key in experiment_targets:
        target = experiment_targets[key]
        experiment[target] = value
        if target == "queueType":
            for bottleneck in topology.get("bottlenecks", []):
                if isinstance(bottleneck, dict) and "queueType" in bottleneck:
                    bottleneck["queueType"] = value
        return
    if key in topology_targets:
        topology[topology_targets[key]] = value
        return
    if key in bottleneck_targets:
        bottlenecks = topology.get("bottlenecks")
        if not isinstance(bottlenecks, list):
            raise ValueError("topology.bottlenecks must be a list for bottleneck sweeps")
        index = 0 if bottleneck_index is None else bottleneck_index
        if index >= len(bottlenecks):
            raise ValueError(f"bottleneck index {index} outside configured bottlenecks")
        if not isinstance(bottlenecks[index], dict):
            raise ValueError(f"topology.bottlenecks[{index}] must be a mapping")
        bottlenecks[index][bottleneck_targets[key]] = value
        return
    raise ValueError(f"unsupported sweep parameter: {parameter}")


def document_to_run_config(document: dict) -> RunConfig:
    experiment = document.get("experiment", {})
    topology = document.get("topology", {})
    if not isinstance(experiment, dict) or not isinstance(topology, dict):
        raise ValueError("config document requires experiment and topology mappings")
    return RunConfig(
        first_tcp=str(experiment.get("firstTcpType", "cubic")),
        queue=str(experiment.get("queueType", "codel")),
        base_rtt=str(experiment.get("baseRtt", "80ms")),
        ecn=parse_bool_value(experiment.get("queueUseEcn", False)),
        rng_run=int(experiment.get("rngRun", 1)),
        stop_time=str(experiment.get("stopTime", "30s")),
        link_rate=str(experiment.get("linkRate", "50Mbps")),
        second_tcp=str(experiment.get("secondTcpType", "") or ""),
        topology=str(topology.get("kind", "") or ""),
        first_start_time=str(experiment.get("firstStartTime", "5s")),
        second_start_time=str(experiment.get("secondStartTime", "15s")),
        second_start_jitter=str(experiment.get("secondStartJitter", "0s")),
        first_start_jitter=str(experiment.get("firstStartJitter", "0s")),
    )


def analysis_metrics_from_document(document: dict) -> tuple[str, ...]:
    analysis = document.get("analysis", {})
    if not isinstance(analysis, dict):
        return ()
    metrics = analysis.get("metrics", [])
    if not metrics:
        return ()
    if not isinstance(metrics, list):
        raise ValueError("analysis.metrics must be a list")
    return tuple(str(metric) for metric in metrics)


def build_specs_from_config(config_file: Path) -> list[RunSpec]:
    base_document = load_config_document(config_file)
    analysis_metrics = analysis_metrics_from_document(base_document)
    sweep = base_document.get("sweep", {})
    if not isinstance(sweep, dict) or not parse_bool_value(sweep.get("enabled", bool(sweep))):
        return [
            RunSpec(
                config=document_to_run_config(base_document),
                document=base_document,
                analysis_metrics=analysis_metrics,
            )
        ]

    parameter = str(sweep.get("parameter", "")).strip()
    if not parameter:
        raise ValueError("enabled sweep requires sweep.parameter")
    values = sweep.get("values", [])
    if not isinstance(values, list) or not values:
        raise ValueError("enabled sweep requires non-empty sweep.values")
    bottleneck_index = sweep.get("bottleneckIndex")
    if bottleneck_index is not None:
        bottleneck_index = int(bottleneck_index)

    specs: list[RunSpec] = []
    for value in values:
        document = copy.deepcopy(base_document)
        apply_sweep_value(document, parameter, value, bottleneck_index)
        document["sweep"] = {
            "enabled": False,
            "name": sweep.get("name", ""),
            "parameter": parameter,
            "selectedValue": value,
            "sourceValues": values,
        }
        cfg = document_to_run_config(document)
        sweep_meta = {
            "name": sweep.get("name", ""),
            "parameter": parameter,
            "value": value,
            "source_config": str(config_file),
        }
        run_id = f"{cfg.run_id()}_sweep-{safe_token(parameter)}-{safe_token(value)}"
        specs.append(
            RunSpec(
                config=cfg,
                document=document,
                sweep=sweep_meta,
                analysis_metrics=analysis_metrics,
                run_id_override=run_id,
            )
        )
    return specs


def config_has_dctcp(cfg: RunConfig) -> bool:
    return cfg.first_tcp.lower().endswith("dctcp") or cfg.second_tcp.lower().endswith("dctcp")


def build_matrix(
    mode: str,
    runs: int,
    stop_time: str,
    link_rate: str,
    tcp_types: str | None,
    queue_types: str | None,
    base_rtts_arg: str | None,
    ecns_arg: str | None,
    pairs_arg: str | None,
    topologies_arg: str | None,
    first_start_time_arg: str | None,
    second_start_time_arg: str | None,
    second_start_jitter_arg: str | None,
    first_start_jitter_arg: str | None,
    include_dctcp_no_ecn: bool,
) -> list[RunConfig]:
    first_start_time = first_start_time_arg or "5s"
    if mode == "mixed-flow":
        second_start_time = second_start_time_arg or "10s"
        second_start_jitter = second_start_jitter_arg or "5s"
    else:
        second_start_time = second_start_time_arg or "15s"
        second_start_jitter = second_start_jitter_arg or "0s"
    first_start_jitter = first_start_jitter_arg or "0s"

    if mode == "smoke":
        first_tcps = parse_csv(tcp_types, ["cubic", "dctcp"])
        queues = parse_csv(queue_types, ["codel", "fq"])
        base_rtts = parse_csv(base_rtts_arg, ["10ms", "80ms"])
        ecns = parse_ecns(ecns_arg, [False, True])
        rng_runs = [1]
    elif mode == "single-flow":
        first_tcps = parse_csv(tcp_types, ["reno", "cubic", "dctcp"])
        queues = parse_csv(queue_types, ["codel", "fq", "pie", "red"])
        base_rtts = parse_csv(base_rtts_arg, ["10ms", "50ms", "80ms"])
        ecns = parse_ecns(ecns_arg, [False, True])
        rng_runs = list(range(1, runs + 1))
    elif mode == "topology":
        first_tcps = parse_csv(tcp_types, ["cubic", "dctcp"])
        queues = parse_csv(queue_types, ["codel", "fq"])
        base_rtts = parse_csv(base_rtts_arg, ["10ms", "80ms"])
        ecns = parse_ecns(ecns_arg, [True])
        rng_runs = list(range(1, runs + 1))
        topologies = parse_topologies(topologies_arg, ["single-bottleneck", "two-bottleneck"])
        matrix = [
            RunConfig(
                first,
                queue,
                rtt,
                ecn,
                run,
                stop_time,
                link_rate,
                topology=topology,
                first_start_time=first_start_time,
                second_start_time=second_start_time,
                second_start_jitter=second_start_jitter,
                first_start_jitter=first_start_jitter,
            )
            for topology, first, queue, rtt, ecn, run in itertools.product(
                topologies, first_tcps, queues, base_rtts, ecns, rng_runs
            )
        ]
        if include_dctcp_no_ecn:
            return matrix
        return [cfg for cfg in matrix if not (config_has_dctcp(cfg) and not cfg.ecn)]
    elif mode == "mixed-flow":
        pairs = parse_pairs(pairs_arg, [("cubic", "reno"), ("cubic", "dctcp"), ("reno", "dctcp")])
        queues = parse_csv(queue_types, ["codel", "fq"])
        base_rtts = parse_csv(base_rtts_arg, ["10ms", "80ms"])
        ecns = parse_ecns(ecns_arg, [True])
        rng_runs = list(range(1, runs + 1))
        topologies = parse_topologies(topologies_arg, ["single-bottleneck", "two-bottleneck"])
        matrix = [
            RunConfig(
                first,
                queue,
                rtt,
                ecn,
                run,
                stop_time,
                link_rate,
                second_tcp=second,
                topology=topology,
                first_start_time=first_start_time,
                second_start_time=second_start_time,
                second_start_jitter=second_start_jitter,
                first_start_jitter=first_start_jitter,
            )
            for topology, (first, second), queue, rtt, ecn, run in itertools.product(
                topologies, pairs, queues, base_rtts, ecns, rng_runs
            )
        ]
        if include_dctcp_no_ecn:
            return matrix
        return [cfg for cfg in matrix if not (config_has_dctcp(cfg) and not cfg.ecn)]
    elif mode == "competition":
        pairs = parse_pairs(pairs_arg, [("cubic", "reno"), ("cubic", "dctcp"), ("reno", "dctcp")])
        queues = parse_csv(queue_types, ["codel", "fq", "pie", "red"])
        base_rtts = parse_csv(base_rtts_arg, ["10ms", "50ms", "80ms"])
        ecns = parse_ecns(ecns_arg, [False, True])
        rng_runs = list(range(1, runs + 1))
        matrix = [
            RunConfig(
                first,
                queue,
                rtt,
                ecn,
                run,
                stop_time,
                link_rate,
                second_tcp=second,
                first_start_time=first_start_time,
                second_start_time=second_start_time,
                second_start_jitter=second_start_jitter,
                first_start_jitter=first_start_jitter,
            )
            for (first, second), queue, rtt, ecn, run in itertools.product(
                pairs, queues, base_rtts, ecns, rng_runs
            )
        ]
        if include_dctcp_no_ecn:
            return matrix
        return [cfg for cfg in matrix if not (config_has_dctcp(cfg) and not cfg.ecn)]
    else:
        raise ValueError(f"unsupported mode: {mode}")

    matrix = [
        RunConfig(
            first,
            queue,
            rtt,
            ecn,
            run,
            stop_time,
            link_rate,
            first_start_time=first_start_time,
            second_start_time=second_start_time,
            second_start_jitter=second_start_jitter,
            first_start_jitter=first_start_jitter,
        )
        for first, queue, rtt, ecn, run in itertools.product(
            first_tcps, queues, base_rtts, ecns, rng_runs
        )
    ]

    # DCTCP without ECN is useful as a warning case, but it is not part of the
    # default matrix because it is not a meaningful protocol comparison.
    if include_dctcp_no_ecn:
        return matrix
    return [cfg for cfg in matrix if not (config_has_dctcp(cfg) and not cfg.ecn)]


def config_document(cfg: RunConfig) -> dict:
    bottlenecks = [
        {
            "name": "core" if cfg.topology == "single-bottleneck" else "edge-to-core",
            "rate": cfg.link_rate if cfg.topology == "single-bottleneck" else "60Mbps",
            "delay": "",
            "queueType": cfg.queue,
            "trace": cfg.topology == "single-bottleneck",
        }
    ]
    if cfg.topology == "two-bottleneck":
        bottlenecks.append(
            {
                "name": "core-to-edge",
                "rate": cfg.link_rate,
                "delay": "",
                "queueType": cfg.queue,
                "trace": True,
            }
        )
    return {
        "experiment": {
            "firstTcpType": cfg.first_tcp,
            "secondTcpType": cfg.second_tcp,
            "queueType": cfg.queue,
            "baseRtt": cfg.base_rtt,
            "ceThreshold": "1ms",
            "linkRate": cfg.link_rate,
            "stopTime": cfg.stop_time,
            "firstStartTime": cfg.first_start_time,
            "secondStartTime": cfg.second_start_time,
            "secondStartJitter": cfg.second_start_jitter,
            "firstStartJitter": cfg.first_start_jitter,
            "queueUseEcn": cfg.ecn,
            "enablePcap": False,
            "rngRun": cfg.rng_run,
        },
        "topology": {
            "kind": cfg.topology,
            "accessRate": "1000Mbps",
            "accessDelay": "1us",
            "deviceQueueSize": "3p",
            "bottlenecks": bottlenecks,
        },
    }


def find_direct_benchmark(repo: Path) -> Path:
    search_dirs = [
        repo / "build" / "contrib" / "tcp-aqm-config" / "examples",
        repo / "build" / "examples" / "tcp",
    ]
    for directory in search_dirs:
        candidates = sorted(directory.glob("ns3.*-tcp-aqm-benchmark-*"))
        if candidates:
            return candidates[-1]
    raise FileNotFoundError("tcp-aqm-benchmark binary not found; build the target first")


def ns3_command(
    repo: Path,
    cfg: RunConfig,
    config_path: Path | None = None,
    runner: str = "ns3",
) -> list[str]:
    if runner == "direct":
        executable = str(find_direct_benchmark(repo))
        if config_path is not None:
            return [executable, f"--configFile={config_path}"]
        args = [
            f"--firstTcpType={cfg.first_tcp}",
            f"--queueType={cfg.queue}",
            f"--baseRtt={cfg.base_rtt}",
            f"--linkRate={cfg.link_rate}",
            f"--queueUseEcn={int(cfg.ecn)}",
            f"--stopTime={cfg.stop_time}",
            f"--firstStartTime={cfg.first_start_time}",
            f"--secondStartTime={cfg.second_start_time}",
            f"--secondStartJitter={cfg.second_start_jitter}",
            f"--firstStartJitter={cfg.first_start_jitter}",
            f"--rngRun={cfg.rng_run}",
        ]
        if cfg.second_tcp:
            args.append(f"--secondTcpType={cfg.second_tcp}")
        return [executable, *args]

    if config_path is not None:
        return ["./ns3", "run", f"tcp-aqm-benchmark --configFile={config_path}"]

    args = [
        f"--firstTcpType={cfg.first_tcp}",
        f"--queueType={cfg.queue}",
        f"--baseRtt={cfg.base_rtt}",
        f"--linkRate={cfg.link_rate}",
        f"--queueUseEcn={int(cfg.ecn)}",
        f"--stopTime={cfg.stop_time}",
        f"--firstStartTime={cfg.first_start_time}",
        f"--secondStartTime={cfg.second_start_time}",
        f"--secondStartJitter={cfg.second_start_jitter}",
        f"--firstStartJitter={cfg.first_start_jitter}",
        f"--rngRun={cfg.rng_run}",
    ]
    if cfg.second_tcp:
        args.append(f"--secondTcpType={cfg.second_tcp}")
    return ["./ns3", "run", "tcp-aqm-benchmark " + " ".join(args)]


def clear_old_traces(repo: Path) -> None:
    for name in TRACE_FILES:
        path = repo / name
        if path.exists():
            path.unlink()


def collect_traces(repo: Path, run_dir: Path) -> list[str]:
    moved = []
    for name in TRACE_FILES:
        source = repo / name
        if source.exists():
            shutil.move(str(source), run_dir / name)
            moved.append(name)
    return moved


def write_json(path: Path, data: object) -> None:
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def run_one(repo: Path, results_dir: Path, spec: RunSpec, overwrite: bool, runner: str) -> int:
    cfg = spec.config
    run_id = spec.run_id()
    run_dir = results_dir / run_id
    if run_dir.exists() and not overwrite:
        print(f"skip existing {run_id}", flush=True)
        return 0
    if run_dir.exists():
        shutil.rmtree(run_dir)
    run_dir.mkdir(parents=True)

    clear_old_traces(repo)
    config_path = None
    if spec.document is not None:
        config_path = run_dir / "config.json"
        write_json(config_path, spec.document)
    elif cfg.topology:
        config_path = run_dir / "config.json"
        write_json(config_path, config_document(cfg))
    cmd = ns3_command(repo, cfg, config_path, runner)
    extra_pinned: tuple[str, ...] = ()
    if config_path is not None:
        try:
            extra_pinned = (str(config_path.relative_to(repo)),)
        except ValueError:
            extra_pinned = (str(config_path),)
    source_hashes = get_source_hashes(repo, extra_pinned)
    env_snapshot = get_env_snapshot()
    start = time.time()
    result = subprocess.run(
        cmd,
        cwd=repo,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    elapsed = time.time() - start
    moved = collect_traces(repo, run_dir)

    metadata = {
        "config": asdict(cfg),
        "run_id": run_id,
        "command": cmd,
        "elapsed_wall_seconds": elapsed,
        "returncode": result.returncode,
        "trace_files": moved,
        "sweep": spec.sweep,
        "analysis_metrics": list(spec.analysis_metrics),
        "git": get_git_metadata(repo),
        "source_sha256": source_hashes,
        "env": env_snapshot,
    }
    write_json(run_dir / "metadata.json", metadata)
    (run_dir / "command.txt").write_text(" ".join(cmd) + "\n", encoding="utf-8")
    (run_dir / "stdout.txt").write_text(result.stdout, encoding="utf-8")
    (run_dir / "stderr.txt").write_text(result.stderr, encoding="utf-8")

    status = "ok" if result.returncode == 0 else "failed"
    print(f"{status} {run_id} ({elapsed:.1f}s, {len(moved)} traces)", flush=True)
    return result.returncode


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--mode",
        choices=["smoke", "single-flow", "competition", "topology", "mixed-flow"],
        default="smoke",
    )
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--stop-time", default="30s")
    parser.add_argument("--link-rate", default="50Mbps")
    parser.add_argument("--tcp-types", default=None, help="Comma-separated first-flow TCP aliases/TypeIds")
    parser.add_argument("--queue-types", default=None, help="Comma-separated queue aliases")
    parser.add_argument("--base-rtts", default=None, help="Comma-separated base RTTs")
    parser.add_argument("--ecns", default=None, help="Comma-separated ECN states: 0,1,on,off,true,false")
    parser.add_argument(
        "--pairs",
        default=None,
        help="Comma-separated competition pairs formatted as first:second",
    )
    parser.add_argument(
        "--topologies",
        default=None,
        help="Comma-separated topology templates: single-bottleneck,two-bottleneck",
    )
    parser.add_argument("--first-start-time", default=None)
    parser.add_argument("--second-start-time", default=None)
    parser.add_argument("--second-start-jitter", default=None)
    parser.add_argument(
        "--first-start-jitter",
        default=None,
        help="Random jitter added to the first flow start; gives single-flow seeds real variance",
    )
    parser.add_argument(
        "--config-file",
        type=Path,
        default=None,
        help="YAML/JSON experiment config with optional one-parameter sweep",
    )
    parser.add_argument("--include-dctcp-no-ecn", action="store_true")
    parser.add_argument("--results-dir", type=Path, default=None)
    parser.add_argument("--runner", choices=["ns3", "direct"], default="ns3")
    parser.add_argument("--overwrite", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    repo = repo_root_from_script()
    results_dir = args.results_dir or repo / "paper" / "icns3-2026-tcp-aqm" / "results"
    results_dir.mkdir(parents=True, exist_ok=True)
    warn_if_dirty_sources(repo)

    if args.config_file is not None:
        specs = build_specs_from_config(args.config_file)
    else:
        matrix = build_matrix(
            args.mode,
            args.runs,
            args.stop_time,
            args.link_rate,
            args.tcp_types,
            args.queue_types,
            args.base_rtts,
            args.ecns,
            args.pairs,
            args.topologies,
            args.first_start_time,
            args.second_start_time,
            args.second_start_jitter,
            args.first_start_jitter,
            args.include_dctcp_no_ecn,
        )
        specs = [RunSpec(config=cfg) for cfg in matrix]
    print(f"repo: {repo}", flush=True)
    print(f"results: {results_dir}", flush=True)
    print(f"runs: {len(specs)}", flush=True)

    if args.dry_run:
        for spec in specs:
            cfg = spec.config
            if spec.document is not None:
                sweep = spec.sweep
                if sweep:
                    print(
                        f"{spec.run_id()} uses {args.config_file} "
                        f"with {sweep.get('parameter')}={sweep.get('value')}"
                    )
                else:
                    print(f"{spec.run_id()} uses {args.config_file}")
            elif cfg.topology:
                print(f"{spec.run_id()} uses generated config.json for {cfg.topology}")
            else:
                print(" ".join(ns3_command(repo, cfg, runner=args.runner)))
        return 0

    failures = 0
    for spec in specs:
        failures += int(run_one(repo, results_dir, spec, args.overwrite, args.runner) != 0)

    print(f"completed {len(specs)} runs with {failures} failures", flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
