# CAKE unit tests

The CAKE model's unit tests live in `cake-queue-disc-test-suite.cc` and are
registered as the `cake-queue-disc` test suite. The suite has eight test
cases, each one focused on a single CAKE mechanism so a failure points
directly at the responsible code path. The other test files in this
directory predate this work and are not described here.

## Running

After building with tests enabled, run just this suite with:

```
./test.py -s cake-queue-disc
```

or invoke the test runner directly:

```
./build/utils/ns3.47-test-runner-debug --test-name=cake-queue-disc --verbose
```

All eight cases are `Duration::QUICK` and complete in well under a second
in the debug build.

## Test cases

| # | Test case | What it covers | Key assertion |
|---|---|---|---|
| 1 | `CakeSingleFlowTestCase` | One flow, lossless: every enqueue succeeds, the queue grows, every dequeue returns a packet in FIFO order. | enqueue/dequeue round-trips preserve FIFO within a flow; exactly one flow class is created. |
| 2 | `CakeTwoFlowDrrTestCase` | Two backlogged flows under DRR (quantum = packet size). | dequeued packets alternate strictly between the two flows (`A0, B0, A1, B1, …`); two flow classes are created. |
| 3 | `CakeShaperTestCase` | Deficit-mode shaper paces packets at the configured rate. Drives the disc through `SetSendCallback` + `Run` (no NetDevice). | per-packet send times equal `i × 1 ms` at 8 Mb/s within `1 ns` tolerance; FIFO order preserved. |
| 4 | `CakeUnlimitedTestCase` | `Bandwidth = 0` bypasses the shaper. | all four backlogged packets leave at the same instant (`t = 0`). |
| 5 | `CakeTinPriorityTestCase` | DiffServ tin priority via `SocketPriorityTag` (DiffServ3 mode). Packets tagged for *bulk*, *best effort*, and *latency-sensitive* tins, enqueued worst-first. | three flow classes created (one per tin); dequeue order is latency → best-effort → bulk regardless of enqueue order. |
| 6 | `CakeAckFilterTestCase` | Lazy ACK filtering. Enqueue `ack(100), data, ack(200), ack(300)` into one flow with a mock `AckIdentifier`. | only `data` and `ack(300)` are delivered, in that order; `nTotalDroppedPacketsAfterDequeue == 2`. |
| 7 | `CakeHostFairnessTestCase` | Triple isolation. Two flows from host A plus one flow from host B; runs the scenario with and without a host classifier. | without host fairness, host A's share is at least `1.4 ×` host B's; with host fairness, the two hosts' shares differ by less than `30 %`. |
| 8 | `CakeTinRateCapTestCase` | Per-tin virtual-clock rate caps + work-conserving borrowing. Latency tin (high priority, small share) + best-effort tin (low priority, large share) both backlogged at 8 Mb/s under shaping. | the (lower-priority) best-effort tin out-serves the rate-capped latency tin (`bytes_BE > 2 × bytes_latency`); a single backlogged tin borrows the link to near-full rate (`> 100 / ~150` packets in 0.15 s). |

## Test fixtures

The test file defines a single `CakeQueueDiscTestItem` that:

* carries an explicit flow `hash` (overrides `QueueDiscItem::Hash`) so the
  per-flow tests can place packets into chosen flows deterministically,
* carries optional `isAck` + `ackNo` (used by case 6 with a mock
  `AckIdentifier` lambda), and
* carries optional source/destination host keys (used by case 7 with a mock
  `HostClassifier` lambda).

These per-item fields let every header-aware mechanism be unit-tested
without pulling in the `internet` module: the same callback interface
`internet/helper/cake-ack-identifier.{h,cc}` populates from real
IPv4/TCP headers in production is satisfied here by a mock that reads the
fields the test item already carries. The tests therefore exercise the
mechanism end-to-end (mark, queue, lazy-drop, throughput / latency
verification) independently of any real protocol stack.
