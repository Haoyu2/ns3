#!/usr/bin/env bash
# Copyright (c) 2026
# SPDX-License-Identifier: GPL-2.0-only
#
# CAKE validation testbed on a single Linux host using network namespaces:
#
#   client --(netem delay)-- router --(CAKE shaped both ways)-- server --(netem delay)--
#
# It captures a Linux sch_cake reference for cake-validation-compare.py by
# running Flent's rrul test through the shaped, delayed path.
#
# Requirements (on the Linux host, run as root):
#   - kernel with sch_cake (>= 4.19) and iproute2 with cake support
#   - flent, netperf (netserver), fping
#
# Usage:
#   sudo ./cake-linux-testbed.sh up   --rate 10mbit --rtt 40
#   sudo ./cake-linux-testbed.sh run  --rate 10mbit --rtt 40 --duration 60 --out linux-cake.flent.gz
#   sudo ./cake-linux-testbed.sh down
#   sudo ./cake-linux-testbed.sh all  --rate 10mbit --rtt 40 --duration 60   # up; run; down
#
# Then turn the .flent.gz into a reference JSON with cake-flent-extract.py.

set -euo pipefail

RATE=10mbit   # CAKE downlink shaper rate (tc cake syntax, e.g. 10mbit, 50mbit)
UP_RATE=""    # CAKE uplink rate; empty => symmetric (= RATE)
RTT=40        # total base RTT in ms (split half each direction via netem)
DURATION=60   # Flent test length in seconds
ACK_FILTER=off # enable CAKE ack-filter on the uplink (on/off)
OUT=""        # output .flent.gz (default derived from rate/rtt)

SERVER_IP=10.0.1.1

usage() {
    sed -n '4,24p' "$0"
}

require_root() {
    if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
        echo "error: run as root (sudo)" >&2
        exit 1
    fi
}

parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --rate) RATE="$2"; shift 2;;
            --up-rate) UP_RATE="$2"; shift 2;;
            --rtt) RTT="$2"; shift 2;;
            --ack-filter) ACK_FILTER="$2"; shift 2;;
            --duration) DURATION="$2"; shift 2;;
            --out) OUT="$2"; shift 2;;
            -h|--help) usage; exit 0;;
            *) echo "unknown argument: $1" >&2; usage; exit 1;;
        esac
    done
}

clean() {
    for ns in client router server; do ip netns del "$ns" 2>/dev/null || true; done
    ip link del veth-c 2>/dev/null || true
    ip link del veth-s 2>/dev/null || true
}

up() {
    clean
    local half="$((RTT / 2))ms"

    ip netns add client
    ip netns add router
    ip netns add server

    ip link add veth-c type veth peer name veth-rc
    ip link add veth-s type veth peer name veth-rs
    ip link set veth-c netns client
    ip link set veth-rc netns router
    ip link set veth-rs netns router
    ip link set veth-s netns server

    ip netns exec client ip addr add 10.0.0.1/24 dev veth-c
    ip netns exec router ip addr add 10.0.0.2/24 dev veth-rc
    ip netns exec router ip addr add 10.0.1.2/24 dev veth-rs
    ip netns exec server ip addr add 10.0.1.1/24 dev veth-s

    for ns in client router server; do ip netns exec "$ns" ip link set lo up; done
    ip netns exec client ip link set veth-c up
    ip netns exec router ip link set veth-rc up
    ip netns exec router ip link set veth-rs up
    ip netns exec server ip link set veth-s up

    ip netns exec client ip route add default via 10.0.0.2
    ip netns exec server ip route add default via 10.0.1.2
    ip netns exec router sysctl -qw net.ipv4.ip_forward=1

    # CAKE on both router egress interfaces => both directions shaped (as in ns-3).
    # veth-rc = downlink (server->client); veth-rs = uplink (client->server),
    # which carries the downloads' ACKs, so ack-filter is applied there.
    local up_rate="${UP_RATE:-$RATE}"
    local ackopt=""
    if [[ "$ACK_FILTER" == "on" || "$ACK_FILTER" == "1" ]]; then
        ackopt="ack-filter"
    fi
    ip netns exec router tc qdisc add dev veth-rc root cake bandwidth "$RATE"
    ip netns exec router tc qdisc add dev veth-rs root cake bandwidth "$up_rate" $ackopt

    # Base RTT via netem at the endpoints (delay only, no shaping).
    ip netns exec client tc qdisc add dev veth-c root netem delay "$half"
    ip netns exec server tc qdisc add dev veth-s root netem delay "$half"

    echo "testbed up: rate=$RATE rtt=${RTT}ms (half=$half each way)"
    ip netns exec client ping -c2 -q "$SERVER_IP" || true
}

run() {
    [[ -n "$OUT" ]] || OUT="linux-cake-${RATE}-${RTT}ms.flent.gz"
    ip netns exec server pkill netserver 2>/dev/null || true
    ip netns exec server netserver >/dev/null 2>&1
    # Note: flent's -o sets the PLOT file; the data file is auto-named, so we
    # point --data-dir at a temp dir and pick up the resulting .flent.gz.
    local dir
    dir="$(mktemp -d)"
    echo "running flent rrul for ${DURATION}s ..."
    ip netns exec client flent rrul -H "$SERVER_IP" -l "$DURATION" \
        -t "cake-${RATE}-${RTT}ms" --data-dir "$dir"
    local data
    data="$(ls -t "$dir"/*.flent.gz 2>/dev/null | head -1)"
    if [[ -z "$data" ]]; then
        echo "error: flent produced no data file" >&2
        exit 1
    fi
    mv "$data" "$OUT"
    rmdir "$dir" 2>/dev/null || true
    echo "saved $OUT"
}

down() {
    clean
    echo "testbed down"
}

require_root
cmd="${1:-}"
shift || true
parse_args "$@"
case "$cmd" in
    up) up;;
    run) run;;
    down) down;;
    all) up; run; down;;
    *) usage; exit 1;;
esac
