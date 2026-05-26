.. include:: replace.txt
.. highlight:: cpp

CAKE queue disc
---------------

This chapter describes the CAKE (Common Applications Kept Enhanced) queue disc
``CakeQueueDisc``, an ns-3 model inspired by the Linux ``sch_cake`` queueing
discipline. CAKE is an *integrated* queue management scheme that combines, in a
single qdisc:

* a built-in **deficit-mode shaper**, so it does not need an external token
  bucket to act as the bottleneck;
* **per-flow isolation** with a Cobalt (BLUE + CoDel) AQM applied to each flow,
  scheduled with deficit round robin (DRR);
* **DiffServ "tins"**, i.e. a small number of priority classes, each with its
  own set of flow queues; and
* **ACK filtering**, which thins redundant TCP acknowledgements.

Model Description
*****************

The source code for the CAKE model lives in
``src/traffic-control/model/cake-queue-disc.{cc,h}``.

Shaper
======

When the ``Bandwidth`` attribute is non-zero, CAKE paces dequeues using a
virtual-clock shaper: a packet may leave only when the simulation time reaches
the shaper's ``time_next_packet``, which is advanced after each transmission by
the time needed to send the packet (plus the configured per-packet
``Overhead``) at the configured rate. When the shaper is throttling, the qdisc
schedules a self wake-up (mirroring the token bucket filter, ``TbfQueueDisc``).
Setting ``Bandwidth`` to ``0bps`` disables shaping (unlimited mode).

Flow isolation
==============

Each tin classifies packets into ``Flows`` queues using a hash of the packet
(optionally a set-associative hash, controlled by ``EnableSetAssociativeHash``
and ``SetWays``). Each flow queue is a ``CobaltQueueDisc`` instance, and flows
are scheduled with DRR over "new" and "old" flow lists, exactly as in
``FqCobaltQueueDisc``.

Host fairness (triple isolation)
================================

When a host classifier is installed via ``CakeQueueDisc::SetHostClassifier``,
CAKE additionally enforces fairness between hosts ("triple isolation": flow,
source host and destination host). Each flow's per-round DRR grant is scaled by
``1 / max(srcHostLoad, dstHostLoad)``, where the host loads are the numbers of
active flows of the flow's source and destination hosts. A host that opens many
flows therefore does not get more than its fair share of host-level bandwidth.
As with ACK filtering, address parsing is injected through the callback; the
internet module provides ``MakeIpv4HostClassifier()``. Without a classifier the
disc falls back to plain flow fairness.

DiffServ tins
=============

The ``DiffServMode`` attribute selects the number and meaning of the tins:

* ``BestEffort`` -- a single tin;
* ``DiffServ3`` -- latency-sensitive / best-effort / bulk;
* ``DiffServ4`` -- voice / video / best-effort / bulk;
* ``Precedence`` -- eight tins by IP precedence (discouraged).

Packets are mapped to a tin from their ``SocketPriorityTag`` (which the IP layer
derives from the DSCP/ToS field) via a per-mode priority map. When shaping is
enabled, each tin runs its own virtual-clock shaper at its share of the
bandwidth: the scheduler serves the highest-priority tin that is "due", and if
no tin is due it lets the least over-budget tin borrow idle capacity (work
conserving). This caps each tin to its rate share while giving latency-sensitive
tins lower delay. In unlimited mode (no shaper) there is no total rate to
apportion, so the scheduler falls back to priority plus weighted deficit round
robin.

ACK filtering
=============

When ``AckFilter`` is not ``Disabled`` and an ACK identifier has been installed
via ``CakeQueueDisc::SetAckIdentifier``, CAKE detects when a newly enqueued pure
ACK supersedes an older queued ACK of the same flow and drops the older one.
Because header parsing must not pull TCP/IP types into the traffic-control
module, the identifier is supplied as a callback; the internet module provides
``MakeTcpAckIdentifier()`` (see ``src/internet/helper/cake-ack-identifier.h``)
for IPv4 TCP. Superseded ACKs are dropped lazily as they reach the head of the
flow queue, because ns-3's ``Queue`` supports removal only from the head; they
are therefore never transmitted, but briefly occupy queue space.

Attributes
==========

Key attributes include ``Bandwidth``, ``DiffServMode``, ``AckFilter``,
``Overhead``, ``UseEcn``, ``Interval`` and ``Target`` (the Cobalt parameters of
each flow), ``Flows``, ``EnableSetAssociativeHash`` and ``SetWays``.

Example
*******

``src/traffic-control/examples/cake-bufferbloat-example.cc`` builds a single
bottleneck, installs CAKE (shaping below the link rate), runs several long-lived
TCP flows, and reports per-flow throughput and mean delay together with CAKE's
drop statistics.

Validation against Linux sch_cake
*********************************

Two helpers support validating the model against the Linux ``sch_cake``
implementation using Flent's *rrul* ("Realtime Response Under Load") test:

* ``src/traffic-control/examples/cake-rrul-validation.cc`` -- an rrul-like ns-3
  scenario: ``nFlows`` long-lived TCP flows in each direction over a
  CAKE-shaped bottleneck, plus a UDP echo probe that samples the round-trip
  time under load. It prints throughput (down/up) and RTT percentiles
  (p50/p90/p99/max).
* ``src/traffic-control/examples/cake-validation-compare.py`` -- lines the ns-3
  result up against a JSON file of numbers measured on Linux, printing the
  relative error per metric and a PASS/FAIL verdict against a tolerance.

Capturing the Linux reference
=============================

On a Linux host (or a network-namespace / veth testbed), shape the egress
interface with CAKE, add the base RTT with ``netem`` on the path, and run
Flent's rrul test::

    # bottleneck shaped by CAKE (match --bandwidth)
    tc qdisc replace dev eth0 root cake bandwidth 10Mbit
    # add the base path delay (e.g. on a veth peer or a middle namespace)
    tc qdisc add dev veth0 root netem delay 20ms

    # drive the rrul workload (Flent must be installed on both ends)
    flent rrul -H <server-ip> -l 60 -t cake-10mbit-40ms -o linux-cake.flent.gz

    # read off average throughput and ping percentiles
    flent --format=summary linux-cake.flent.gz

Record the resulting numbers in a reference JSON (see
``cake-validation-reference.example.json``)::

    {
      "scenario":  { "bandwidth_mbps": 10, "rtt_ms": 40, "nflows": 4 },
      "linux_cake": {
        "down_mbps": 9.4, "up_mbps": 9.4,
        "rtt_p50_ms": 41.0, "rtt_p90_ms": 43.0, "rtt_p99_ms": 45.0
      },
      "source": "flent rrul, Linux 6.x, tc cake bandwidth 10Mbit, netem 40ms"
    }

Running the comparison
======================

::

    ./ns3 build cake-rrul-validation
    python3 src/traffic-control/examples/cake-validation-compare.py \
        --reference my-linux-cake.json --tolerance 0.2

The script runs the ns-3 scenario for the reference's parameters, compares
throughput and RTT percentiles, and exits non-zero if any metric falls outside
the tolerance (suitable for a reproducibility gate).

Results
=======

Against Linux ``sch_cake`` (Flent rrul, 4 flows per direction), the model agrees
within a 20% tolerance across a range of operating points. Captured references
are checked in as ``cake-validation-linux-<rate>-<rtt>.json``:

==================  ===========================  ====================  =======
Operating point     Throughput (ns-3 vs Linux)   RTT p99 (ns-3/Linux)  Verdict
==================  ===========================  ====================  =======
10 Mbit / 40 ms     8.5 / 9.3 Mbps               43 / 46 ms            pass
50 Mbit / 80 ms     43 / 45 Mbps                 81 / 81 ms            pass
100 Mbit / 20 ms    94 / 93 Mbps                 20 / 23 ms            pass
20 Mbit / 100 ms    16 / 18 Mbps                 102 / 103 ms          pass
1 Mbit / 40 ms      0.6 / 0.9 Mbps               72 / 90 ms            fail
==================  ===========================  ====================  =======

The 1 Mbit point exposes a real limitation (below): at very low rates the model
over-drops, delivering less throughput and lower latency than Linux.

Limitations and current status
******************************

The model implements flow isolation, the shaper, DiffServ tins and ACK
filtering. The following CAKE features are not yet modelled, or are
approximated:

* the per-flow CoDel target is fixed (5 ms) rather than scaled to the configured
  rate as Linux CAKE does (``cake_set_rate``); at very low bandwidths
  (around 1 Mbit/s and below) one MTU takes longer to serialise than the target,
  so the model over-drops and under-delivers throughput (see the 1 Mbit row above);
* only IPv4 TCP ACKs are recognised by the provided ACK identifier, and only
  IPv4 addresses by the provided host classifier;
* ACK filtering removes superseded ACKs lazily (at dequeue) rather than in
  place.
