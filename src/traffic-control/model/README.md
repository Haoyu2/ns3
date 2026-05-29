# CAKE model implementation notes

This file describes the ns-3 implementation of CAKE (Common Applications Kept
Enhanced) added to the `traffic-control` module. It is a developer-facing
reference; for user-facing documentation see
[`../doc/cake.rst`](../doc/cake.rst), and for the accompanying paper see
[`../../../paper/cake-icns3.tex`](../../../paper/cake-icns3.tex).

## File layout

* `cake-queue-disc.h` / `cake-queue-disc.cc` — the entire CAKE model.
* `cobalt-queue-disc.{h,cc}` — pre-existing; reused unmodified as the
  per-flow AQM. No COBALT changes were needed.

The internet-module helpers
([`../../internet/helper/cake-ack-identifier.{h,cc}`](../../internet/helper/))
supply concrete TCP/IPv4 implementations of CAKE's pluggable header-inspection
callbacks (see "Dependency-clean design" below).

## Class hierarchy

```
QueueDisc                        (ns-3 base)
└── CakeQueueDisc                root qdisc; one per device
    ├── std::vector<CakeTin>     DiffServ priority classes
    │     └── flow lists, DRR state, per-tin shaper clock & rate,
    │         per-host active-flow counts
    └── QueueDiscClass children = CakeFlow * N
          └── child queue disc = CobaltQueueDisc (one per flow)

QueueDiscClass
└── CakeFlow                     per-flow state (DRR deficit, FlowStatus,
                                  ACK-filter state, host keys)
```

A `CakeFlow` is the standard ns-3 way of carrying per-class state alongside
a child `QueueDisc`. A `CakeTin` is a plain struct held in
`CakeQueueDisc::m_tins`.

## Mechanisms

The five CAKE mechanisms are implemented as a small stack inside
`DoEnqueue` / `DoDequeue`:

1. **Flow isolation.** Packets are hashed (or classified via packet filters)
   into a configurable number of flow queues per tin, using set-associative
   placement (`SetAssociativeHash`). Each flow's child queue disc is a
   `CobaltQueueDisc`. Within a tin, flows are scheduled by deficit round
   robin over new-flow and old-flow lists, exactly mirroring
   `FqCobaltQueueDisc` — see `DequeueFromTin`.

2. **Deficit-mode shaper.** When the `Bandwidth` attribute is non-zero,
   dequeues are paced by a virtual transmission clock
   (`m_timeNextPacket`). Throttled dequeues schedule a self-wake via
   `Simulator::Schedule(delay, &QueueDisc::Run, this)`, reusing the
   pattern of `TbfQueueDisc`. An idle period resets the clock to *now*
   so a burst cannot follow idleness. `Bandwidth = 0` disables shaping.

3. **DiffServ tins.** `SetupTins` builds the tin vector from the
   `DiffServMode` attribute (`besteffort`, `diffserv3`, `diffserv4`,
   `precedence`) plus a per-mode priority map. Packets are mapped to a
   tin via `SocketPriorityTag` (which the IP layer derives from the
   DSCP field — same dependency-clean mechanism the `prio` qdisc uses).
   When shaping is enabled each tin runs *its own* virtual-clock shaper
   at its share of the bandwidth; the inter-tin scheduler serves the
   highest-priority *due* tin and lets the least over-budget tin
   *borrow* idle capacity when none is due (work-conserving). In
   unlimited mode the scheduler falls back to priority + weighted DRR.
   See Algorithm 1 in the paper.

4. **ACK filtering.** Recognising a pure TCP ACK requires inspecting
   TCP/IP headers, which the `traffic-control` module is forbidden from
   linking against (see below). The model therefore exposes a pluggable
   callback `AckIdentifier` (`SetAckIdentifier`); the concrete TCP/IPv4
   implementation is `MakeTcpAckIdentifier()` in the internet module.
   The model cannot splice an ACK out of the middle of a flow queue
   (the public `Queue` API removes only from the head), so a superseded
   ACK is **marked** and dropped *lazily* when it reaches the head —
   the bandwidth saving is still realised; the only fidelity gap is
   that the ACK briefly occupies queue space.

5. **Host fairness (triple isolation).** When a `HostClassifier` callback
   is installed (`SetHostClassifier`), each flow carries source/destination
   host keys and each tin maintains per-host active-flow counts. The
   per-round DRR grant to a flow is then scaled by
   `1 / max(srcHostLoad, dstHostLoad)` via `GrantQuantum`, so a host with
   many flows cannot claim more than its fair share of host-level
   bandwidth. The concrete IPv4 implementation is `MakeIpv4HostClassifier()`.

## Dependency-clean design

ns-3's module layering forbids `traffic-control` from depending on
`internet`. ACK filtering and host fairness, however, need to inspect TCP /
IPv4 headers. The model resolves this by **injecting all header inspection
through callbacks**:

* `using AckIdentifier = std::function<bool(Ptr<const QueueDiscItem>, uint32_t&)>;`
* `using HostClassifier = std::function<void(Ptr<const QueueDiscItem>, uint32_t&, uint32_t&)>;`

Concrete implementations live in the `internet` module
(`MakeTcpAckIdentifier()`, `MakeIpv4HostClassifier()`); the user wires
them in at simulation setup time, just like a `SendCallback` or a
`PacketFilter`. The core model never references TCP or IPv4 types.

## Attributes

| Attribute | Default | Notes |
|---|---|---|
| `MaxSize` | `10240p` | total disc occupancy (packets) |
| `Bandwidth` | `0bps` | shaper rate; `0` disables shaping |
| `DiffServMode` | `DiffServ3` | `BestEffort` / `DiffServ3` / `DiffServ4` / `Precedence` |
| `AckFilter` | `Disabled` | `Disabled` / `Filter` / `Aggressive` (the last two are aliased — both enable filtering) |
| `Overhead` | `0` | per-packet link-layer overhead in bytes (for the shaper) |
| `UseEcn` | `true` | enable ECN marking in the per-flow COBALT |
| `Interval` | `100ms` | per-flow COBALT/CoDel interval |
| `Target` | `5ms` | per-flow COBALT/CoDel target |
| `Flows` | `1024` | flow queues per tin |
| `DropBatchSize` | `64` | max packets dropped from the fat flow on overflow |
| `Perturbation` | `0` | salt for the flow hash |
| `EnableSetAssociativeHash` | `false` | enable set-associative flow placement |
| `SetWays` | `8` | set size for the set-associative hash |

Some configuration is per-callback rather than per-attribute:
`SetQuantum(uint32_t)` for the DRR quantum (defaults to the device MTU),
`SetAckIdentifier(...)` and `SetHostClassifier(...)` for the pluggable
header inspection.

## Limitations

The known limitations are listed in the "Limitations and Future Work" section
of `paper/cake-icns3.tex` and the "Limitations and current status" section
of `doc/cake.rst`. The largest current gap is that the per-flow CoDel target
is fixed at `5 ms`, where Linux CAKE scales the target with the configured
rate (`cake_set_rate`); at very low bandwidths (≲ 1 Mb/s) one MTU takes
longer than 5 ms to serialise, so the model over-drops. This is the cause
of the 1 Mb/s validation point falling outside tolerance in the paper.
