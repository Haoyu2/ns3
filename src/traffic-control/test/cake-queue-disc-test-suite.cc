/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/cake-queue-disc.h"
#include "ns3/data-rate.h"
#include "ns3/enum.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/queue.h"
#include "ns3/queue-size.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/test.h"

#include <map>
#include <vector>

using namespace ns3;

/**
 * @ingroup traffic-control-test
 *
 * @brief Cake Queue Disc Test Item
 *
 * The flow hash is supplied explicitly so that tests can deterministically
 * place packets into the same or different flow queues.
 */
class CakeQueueDiscTestItem : public QueueDiscItem
{
  public:
    /**
     * Constructor
     * @param p the packet
     * @param addr the address
     * @param hash the flow hash this item should report
     */
    CakeQueueDiscTestItem(Ptr<Packet> p, const Address& addr, uint32_t hash);
    ~CakeQueueDiscTestItem() override;

    // Delete default constructor, copy constructor and assignment operator to avoid misuse
    CakeQueueDiscTestItem() = delete;
    CakeQueueDiscTestItem(const CakeQueueDiscTestItem&) = delete;
    CakeQueueDiscTestItem& operator=(const CakeQueueDiscTestItem&) = delete;

    void AddHeader() override;
    bool Mark() override;
    uint32_t Hash(uint32_t perturbation) const override;

    /**
     * Mark this item as a pure TCP ACK carrying the given ack number.
     * @param ackNo the acknowledgement number
     */
    void SetAck(uint32_t ackNo);
    /// @return true if this item is a pure ACK
    bool IsAck() const;
    /// @return the acknowledgement number of this item
    uint32_t GetAckNo() const;

    /**
     * Set the source/destination host keys carried by this item.
     * @param srcHost source host key
     * @param dstHost destination host key
     */
    void SetHosts(uint32_t srcHost, uint32_t dstHost);
    /// @return the source host key
    uint32_t SrcHost() const;
    /// @return the destination host key
    uint32_t DstHost() const;

  private:
    uint32_t m_hash;        //!< the flow hash reported by this item
    bool m_isAck{false};    //!< whether this item is a pure ACK
    uint32_t m_ackNo{0};    //!< the acknowledgement number, if an ACK
    uint32_t m_srcHost{0};  //!< source host key
    uint32_t m_dstHost{0};  //!< destination host key
};

void
CakeQueueDiscTestItem::SetHosts(uint32_t srcHost, uint32_t dstHost)
{
    m_srcHost = srcHost;
    m_dstHost = dstHost;
}

uint32_t
CakeQueueDiscTestItem::SrcHost() const
{
    return m_srcHost;
}

uint32_t
CakeQueueDiscTestItem::DstHost() const
{
    return m_dstHost;
}

CakeQueueDiscTestItem::CakeQueueDiscTestItem(Ptr<Packet> p, const Address& addr, uint32_t hash)
    : QueueDiscItem(p, addr, 0),
      m_hash(hash)
{
}

void
CakeQueueDiscTestItem::SetAck(uint32_t ackNo)
{
    m_isAck = true;
    m_ackNo = ackNo;
}

bool
CakeQueueDiscTestItem::IsAck() const
{
    return m_isAck;
}

uint32_t
CakeQueueDiscTestItem::GetAckNo() const
{
    return m_ackNo;
}

CakeQueueDiscTestItem::~CakeQueueDiscTestItem()
{
}

void
CakeQueueDiscTestItem::AddHeader()
{
}

bool
CakeQueueDiscTestItem::Mark()
{
    return false;
}

uint32_t
CakeQueueDiscTestItem::Hash(uint32_t perturbation) const
{
    return m_hash;
}

/**
 * @ingroup traffic-control-test
 *
 * @brief CAKE single-flow FIFO test
 *
 * Packets that hash to the same flow must be dequeued in FIFO order.
 */
class CakeSingleFlowTestCase : public TestCase
{
  public:
    CakeSingleFlowTestCase();
    void DoRun() override;
};

CakeSingleFlowTestCase::CakeSingleFlowTestCase()
    : TestCase("CAKE preserves FIFO order within a single flow")
{
}

void
CakeSingleFlowTestCase::DoRun()
{
    uint32_t numPackets = 10;
    uint32_t pktSize = 1000;

    Ptr<CakeQueueDisc> queue = CreateObject<CakeQueueDisc>();
    queue->SetQuantum(pktSize);

    // The CAKE-specific configuration surface must be settable. Shaping is left
    // disabled (0 bps) so the synchronous enqueue/dequeue loop below is valid.
    NS_TEST_ASSERT_MSG_EQ(
        queue->SetAttributeFailSafe("Bandwidth", DataRateValue(DataRate("0bps"))),
        true,
        "Verify that we can set the Bandwidth attribute");
    NS_TEST_ASSERT_MSG_EQ(
        queue->SetAttributeFailSafe("DiffServMode", EnumValue(CakeQueueDisc::DIFFSERV_DIFFSERV4)),
        true,
        "Verify that we can set the DiffServMode attribute");
    NS_TEST_ASSERT_MSG_EQ(
        queue->SetAttributeFailSafe("AckFilter", EnumValue(CakeQueueDisc::ACK_FILTER_FILTER)),
        true,
        "Verify that we can set the AckFilter attribute");

    queue->Initialize();

    Address dest;
    std::vector<uint64_t> uids;

    NS_TEST_ASSERT_MSG_EQ(queue->GetCurrentSize().GetValue(), 0, "The queue disc should be empty");

    for (uint32_t i = 1; i <= numPackets; i++)
    {
        Ptr<Packet> p = Create<Packet>(pktSize);
        uids.push_back(p->GetUid());
        NS_TEST_ASSERT_MSG_EQ(queue->Enqueue(Create<CakeQueueDiscTestItem>(p, dest, 0)),
                              true,
                              "Enqueue of packet " << i << " should succeed");
        NS_TEST_ASSERT_MSG_EQ(queue->GetCurrentSize().GetValue(),
                              i,
                              "There should be " << i << " packet(s) in there");
    }

    // A single flow should have been created.
    NS_TEST_ASSERT_MSG_EQ(queue->GetNQueueDiscClasses(),
                          1,
                          "Exactly one flow queue should have been created");

    for (uint32_t i = 1; i <= numPackets; i++)
    {
        Ptr<QueueDiscItem> item = queue->Dequeue();
        NS_TEST_ASSERT_MSG_NE(item, nullptr, "A packet should have been dequeued");
        NS_TEST_ASSERT_MSG_EQ(item->GetPacket()->GetUid(),
                              uids[i - 1],
                              "Packets should be dequeued in FIFO order within a flow");
    }

    NS_TEST_ASSERT_MSG_EQ(queue->Dequeue(), nullptr, "There are really no packets in there");

    Simulator::Destroy();
}

/**
 * @ingroup traffic-control-test
 *
 * @brief CAKE two-flow DRR test
 *
 * With the quantum equal to the packet size, two backlogged flows must be
 * served in strict round-robin order.
 */
class CakeTwoFlowDrrTestCase : public TestCase
{
  public:
    CakeTwoFlowDrrTestCase();
    void DoRun() override;
};

CakeTwoFlowDrrTestCase::CakeTwoFlowDrrTestCase()
    : TestCase("CAKE serves two backlogged flows in round-robin (DRR)")
{
}

void
CakeTwoFlowDrrTestCase::DoRun()
{
    uint32_t perFlow = 3;
    uint32_t pktSize = 1000;

    Ptr<CakeQueueDisc> queue = CreateObject<CakeQueueDisc>();
    // Quantum == packet size => one packet per flow per round.
    queue->SetQuantum(pktSize);
    queue->Initialize();

    Address dest;
    std::vector<uint64_t> uidsA;
    std::vector<uint64_t> uidsB;

    // Flow A (hash 0) is seen first, then flow B (hash 1).
    for (uint32_t i = 0; i < perFlow; i++)
    {
        Ptr<Packet> p = Create<Packet>(pktSize);
        uidsA.push_back(p->GetUid());
        NS_TEST_ASSERT_MSG_EQ(queue->Enqueue(Create<CakeQueueDiscTestItem>(p, dest, 0)),
                              true,
                              "Enqueue into flow A should succeed");
    }
    for (uint32_t i = 0; i < perFlow; i++)
    {
        Ptr<Packet> p = Create<Packet>(pktSize);
        uidsB.push_back(p->GetUid());
        NS_TEST_ASSERT_MSG_EQ(queue->Enqueue(Create<CakeQueueDiscTestItem>(p, dest, 1)),
                              true,
                              "Enqueue into flow B should succeed");
    }

    NS_TEST_ASSERT_MSG_EQ(queue->GetNQueueDiscClasses(),
                          2,
                          "Two flow queues should have been created");

    // Expected DRR order: A0, B0, A1, B1, A2, B2
    std::vector<uint64_t> expected;
    for (uint32_t i = 0; i < perFlow; i++)
    {
        expected.push_back(uidsA[i]);
        expected.push_back(uidsB[i]);
    }

    for (uint32_t i = 0; i < expected.size(); i++)
    {
        Ptr<QueueDiscItem> item = queue->Dequeue();
        NS_TEST_ASSERT_MSG_NE(item, nullptr, "A packet should have been dequeued");
        NS_TEST_ASSERT_MSG_EQ(item->GetPacket()->GetUid(),
                              expected[i],
                              "DRR should alternate between the two flows (position " << i << ")");
    }

    NS_TEST_ASSERT_MSG_EQ(queue->Dequeue(), nullptr, "Both flows should be drained");

    Simulator::Destroy();
}

/**
 * @ingroup traffic-control-test
 *
 * @brief CAKE deficit-mode shaper test
 *
 * With shaping enabled, backlogged packets must leave the disc paced at the
 * configured rate. The disc is driven by a recording send callback and its own
 * self-wake (no NetDevice required).
 */
class CakeShaperTestCase : public TestCase
{
  public:
    CakeShaperTestCase();
    void DoRun() override;

  private:
    /**
     * Records the simulation time and UID of each transmitted packet.
     * @param item the transmitted item
     */
    void SendPacket(Ptr<QueueDiscItem> item);

    std::vector<Time> m_sendTimes;   //!< time at which each packet left the disc
    std::vector<uint64_t> m_sendUids; //!< UID of each transmitted packet, in order
};

CakeShaperTestCase::CakeShaperTestCase()
    : TestCase("CAKE deficit-mode shaper paces packets at the configured rate")
{
}

void
CakeShaperTestCase::SendPacket(Ptr<QueueDiscItem> item)
{
    m_sendTimes.push_back(Simulator::Now());
    m_sendUids.push_back(item->GetPacket()->GetUid());
}

void
CakeShaperTestCase::DoRun()
{
    uint32_t pktSize = 1000;
    uint32_t numPackets = 3;

    Ptr<CakeQueueDisc> queue = CreateObject<CakeQueueDisc>();
    queue->SetQuantum(pktSize);
    // 8 Mbps == 1 MB/s, so each 1000-byte packet takes exactly 1 ms. The rate is
    // kept fast so the whole experiment fits well within CoDel's interval and the
    // per-flow AQM does not drop the packets we are pacing.
    queue->SetAttribute("Bandwidth", DataRateValue(DataRate("8Mbps")));
    queue->SetSendCallback([this](Ptr<QueueDiscItem> item) { SendPacket(item); });
    queue->Initialize();

    Address dest;
    std::vector<uint64_t> uids;
    for (uint32_t i = 0; i < numPackets; i++)
    {
        Ptr<Packet> p = Create<Packet>(pktSize);
        uids.push_back(p->GetUid());
        queue->Enqueue(Create<CakeQueueDiscTestItem>(p, dest, 0));
    }

    // Kick the disc; the shaper's self-wake drives the remaining packets.
    queue->Run();

    Simulator::Stop(Seconds(1));
    Simulator::Run();

    NS_TEST_ASSERT_MSG_EQ(m_sendTimes.size(),
                          numPackets,
                          "All packets should eventually be transmitted");

    // Each 1000-byte packet takes 1 ms at 8 Mbps, so packet i leaves at i ms.
    for (uint32_t i = 0; i < numPackets; i++)
    {
        NS_TEST_ASSERT_MSG_EQ_TOL(m_sendTimes[i].GetSeconds(),
                                  i * 1e-3,
                                  1e-9,
                                  "Packet " << i << " should be paced to about " << i << " ms");
        NS_TEST_ASSERT_MSG_EQ(m_sendUids[i], uids[i], "Packets should leave in FIFO order");
    }

    Simulator::Destroy();
}

/**
 * @ingroup traffic-control-test
 *
 * @brief CAKE unlimited-mode (no shaping) test
 *
 * With Bandwidth set to 0 bps the shaper must be bypassed: all backlogged
 * packets are released at the same instant.
 */
class CakeUnlimitedTestCase : public TestCase
{
  public:
    CakeUnlimitedTestCase();
    void DoRun() override;

  private:
    /**
     * Records the simulation time of each transmitted packet.
     * @param item the transmitted item
     */
    void SendPacket(Ptr<QueueDiscItem> item);

    std::vector<Time> m_sendTimes; //!< time at which each packet left the disc
};

CakeUnlimitedTestCase::CakeUnlimitedTestCase()
    : TestCase("CAKE bypasses the shaper when Bandwidth is 0 bps")
{
}

void
CakeUnlimitedTestCase::SendPacket(Ptr<QueueDiscItem> item)
{
    m_sendTimes.push_back(Simulator::Now());
}

void
CakeUnlimitedTestCase::DoRun()
{
    uint32_t pktSize = 1000;
    uint32_t numPackets = 4;

    Ptr<CakeQueueDisc> queue = CreateObject<CakeQueueDisc>();
    queue->SetQuantum(pktSize);
    queue->SetAttribute("Bandwidth", DataRateValue(DataRate("0bps")));
    queue->SetSendCallback([this](Ptr<QueueDiscItem> item) { SendPacket(item); });
    queue->Initialize();

    Address dest;
    for (uint32_t i = 0; i < numPackets; i++)
    {
        queue->Enqueue(Create<CakeQueueDiscTestItem>(Create<Packet>(pktSize), dest, 0));
    }

    queue->Run();

    NS_TEST_ASSERT_MSG_EQ(m_sendTimes.size(),
                          numPackets,
                          "All packets should be transmitted without shaping");
    for (uint32_t i = 0; i < numPackets; i++)
    {
        NS_TEST_ASSERT_MSG_EQ(m_sendTimes[i].GetSeconds(),
                              0.0,
                              "Unshaped packets should all leave at the same instant");
    }

    Simulator::Destroy();
}

/**
 * @ingroup traffic-control-test
 *
 * @brief CAKE DiffServ tin priority test
 *
 * In diffserv3 mode, packets tagged for different tins must be served in tin
 * priority order (latency-sensitive before best-effort before bulk),
 * regardless of enqueue order.
 */
class CakeTinPriorityTestCase : public TestCase
{
  public:
    CakeTinPriorityTestCase();
    void DoRun() override;

  private:
    /**
     * Enqueue a packet carrying a given socket priority.
     * @param queue the queue disc
     * @param prio the socket priority (selects the tin)
     * @return the UID of the created packet
     */
    uint64_t EnqueuePrio(Ptr<CakeQueueDisc> queue, uint8_t prio);
};

CakeTinPriorityTestCase::CakeTinPriorityTestCase()
    : TestCase("CAKE serves DiffServ tins in priority order")
{
}

uint64_t
CakeTinPriorityTestCase::EnqueuePrio(Ptr<CakeQueueDisc> queue, uint8_t prio)
{
    Ptr<Packet> p = Create<Packet>(1000);
    SocketPriorityTag tag;
    tag.SetPriority(prio);
    p->AddPacketTag(tag);
    Address dest;
    queue->Enqueue(Create<CakeQueueDiscTestItem>(p, dest, 0));
    return p->GetUid();
}

void
CakeTinPriorityTestCase::DoRun()
{
    Ptr<CakeQueueDisc> queue = CreateObject<CakeQueueDisc>();
    queue->SetQuantum(1000);
    queue->SetAttribute("DiffServMode", EnumValue(CakeQueueDisc::DIFFSERV_DIFFSERV3));
    queue->Initialize();

    // diffserv3 priomap: priority 1 -> bulk (tin 2), 0 -> best effort (tin 1),
    // 6 -> latency-sensitive (tin 0). Enqueue in reverse priority order.
    uint64_t bulkUid = EnqueuePrio(queue, 1);
    uint64_t bestEffortUid = EnqueuePrio(queue, 0);
    uint64_t latencyUid = EnqueuePrio(queue, 6);

    NS_TEST_ASSERT_MSG_EQ(queue->GetNQueueDiscClasses(),
                          3,
                          "One flow should have been created in each of the three tins");

    Ptr<QueueDiscItem> item = queue->Dequeue();
    NS_TEST_ASSERT_MSG_NE(item, nullptr, "A packet should have been dequeued");
    NS_TEST_ASSERT_MSG_EQ(item->GetPacket()->GetUid(),
                          latencyUid,
                          "The latency-sensitive tin should be served first");

    item = queue->Dequeue();
    NS_TEST_ASSERT_MSG_NE(item, nullptr, "A packet should have been dequeued");
    NS_TEST_ASSERT_MSG_EQ(item->GetPacket()->GetUid(),
                          bestEffortUid,
                          "The best-effort tin should be served second");

    item = queue->Dequeue();
    NS_TEST_ASSERT_MSG_NE(item, nullptr, "A packet should have been dequeued");
    NS_TEST_ASSERT_MSG_EQ(item->GetPacket()->GetUid(),
                          bulkUid,
                          "The bulk tin should be served last");

    NS_TEST_ASSERT_MSG_EQ(queue->Dequeue(), nullptr, "All tins should be drained");

    Simulator::Destroy();
}

/**
 * @ingroup traffic-control-test
 *
 * @brief CAKE ACK-filtering test
 *
 * A pure ACK that is superseded by a later ACK in the same flow must be dropped
 * before transmission, while data packets and the most recent ACK are kept.
 */
class CakeAckFilterTestCase : public TestCase
{
  public:
    CakeAckFilterTestCase();
    void DoRun() override;
};

CakeAckFilterTestCase::CakeAckFilterTestCase()
    : TestCase("CAKE drops superseded ACKs and keeps data and the latest ACK")
{
}

void
CakeAckFilterTestCase::DoRun()
{
    Ptr<CakeQueueDisc> queue = CreateObject<CakeQueueDisc>();
    queue->SetQuantum(1000);
    queue->SetAttribute("DiffServMode", EnumValue(CakeQueueDisc::DIFFSERV_BESTEFFORT));
    queue->SetAttribute("AckFilter", EnumValue(CakeQueueDisc::ACK_FILTER_FILTER));
    // The identifier reads the ACK info we stamped onto the test item, so the
    // CAKE model itself needs no knowledge of TCP/IP headers.
    queue->SetAckIdentifier([](Ptr<const QueueDiscItem> it, uint32_t& ackNo) {
        auto t = DynamicCast<const CakeQueueDiscTestItem>(it);
        if (t && t->IsAck())
        {
            ackNo = t->GetAckNo();
            return true;
        }
        return false;
    });
    queue->Initialize();

    Address dest;

    // Helper lambdas to enqueue data and ACK packets into the same flow (hash 0).
    auto enqueueData = [&]() {
        Ptr<Packet> p = Create<Packet>(1000);
        uint64_t uid = p->GetUid();
        queue->Enqueue(Create<CakeQueueDiscTestItem>(p, dest, 0));
        return uid;
    };
    auto enqueueAck = [&](uint32_t ackNo) {
        Ptr<Packet> p = Create<Packet>(40);
        uint64_t uid = p->GetUid();
        auto item = Create<CakeQueueDiscTestItem>(p, dest, 0);
        item->SetAck(ackNo);
        queue->Enqueue(item);
        return uid;
    };

    enqueueAck(100);                 // superseded by ack 200
    uint64_t dataUid = enqueueData(); // must be kept
    enqueueAck(200);                 // superseded by ack 300
    uint64_t lastAckUid = enqueueAck(300);

    // Only the data packet and the most recent ACK should survive, in order.
    Ptr<QueueDiscItem> item = queue->Dequeue();
    NS_TEST_ASSERT_MSG_NE(item, nullptr, "A packet should have been dequeued");
    NS_TEST_ASSERT_MSG_EQ(item->GetPacket()->GetUid(),
                          dataUid,
                          "The data packet must be delivered, not filtered");

    item = queue->Dequeue();
    NS_TEST_ASSERT_MSG_NE(item, nullptr, "A packet should have been dequeued");
    NS_TEST_ASSERT_MSG_EQ(item->GetPacket()->GetUid(),
                          lastAckUid,
                          "The most recent ACK must be delivered");

    NS_TEST_ASSERT_MSG_EQ(queue->Dequeue(), nullptr, "The two old ACKs should have been dropped");
    NS_TEST_ASSERT_MSG_EQ(queue->GetStats().nTotalDroppedPacketsAfterDequeue,
                          2,
                          "Exactly the two superseded ACKs should have been dropped");

    Simulator::Destroy();
}

/**
 * @ingroup traffic-control-test
 *
 * @brief CAKE host-fairness (triple isolation) test
 *
 * Two flows from host A and one flow from host B all saturate the disc. With
 * plain flow fairness host A (2 flows) gets roughly twice host B's share; with
 * host fairness enabled the two hosts get roughly equal shares.
 */
class CakeHostFairnessTestCase : public TestCase
{
  public:
    CakeHostFairnessTestCase();
    void DoRun() override;

  private:
    /**
     * Backlog 2 flows of host A and 1 of host B, then dequeue and tally per host.
     * @param hostFair whether to enable host-fair scheduling
     * @param aTotal [out] packets served belonging to host A
     * @param bTotal [out] packets served belonging to host B
     */
    void RunScenario(bool hostFair, uint32_t& aTotal, uint32_t& bTotal);
};

CakeHostFairnessTestCase::CakeHostFairnessTestCase()
    : TestCase("CAKE host fairness equalises per-host shares (triple isolation)")
{
}

void
CakeHostFairnessTestCase::RunScenario(bool hostFair, uint32_t& aTotal, uint32_t& bTotal)
{
    const uint32_t pktSize = 1000;
    const uint32_t perFlow = 40;
    const uint32_t toDequeue = 90;

    Ptr<CakeQueueDisc> queue = CreateObject<CakeQueueDisc>();
    queue->SetQuantum(pktSize);
    queue->SetAttribute("DiffServMode", EnumValue(CakeQueueDisc::DIFFSERV_BESTEFFORT));
    if (hostFair)
    {
        queue->SetHostClassifier([](Ptr<const QueueDiscItem> it, uint32_t& s, uint32_t& d) {
            auto t = DynamicCast<const CakeQueueDiscTestItem>(it);
            if (t)
            {
                s = t->SrcHost();
                d = t->DstHost();
            }
        });
    }
    queue->Initialize();

    Address dest;
    // host A: two flows (hash 0 and 1); host B: one flow (hash 2).
    struct FlowSpec
    {
        uint32_t hash;
        uint32_t src;
        uint32_t dst;
    };
    const FlowSpec flows[] = {{0, 1, 10}, {1, 1, 11}, {2, 2, 12}};

    for (uint32_t i = 0; i < perFlow; i++)
    {
        for (const auto& f : flows)
        {
            auto item = Create<CakeQueueDiscTestItem>(Create<Packet>(pktSize), dest, f.hash);
            item->SetHosts(f.src, f.dst);
            queue->Enqueue(item);
        }
    }

    aTotal = 0;
    bTotal = 0;
    for (uint32_t i = 0; i < toDequeue; i++)
    {
        Ptr<QueueDiscItem> item = queue->Dequeue();
        if (!item)
        {
            break;
        }
        auto t = DynamicCast<CakeQueueDiscTestItem>(item);
        if (t->SrcHost() == 1)
        {
            aTotal++;
        }
        else if (t->SrcHost() == 2)
        {
            bTotal++;
        }
    }

    Simulator::Destroy();
}

void
CakeHostFairnessTestCase::DoRun()
{
    uint32_t aFlow;
    uint32_t bFlow;
    RunScenario(false, aFlow, bFlow);
    // Flow fairness: host A has twice the flows, so it gets clearly more.
    NS_TEST_ASSERT_MSG_EQ(aFlow > bFlow * 1.4,
                          true,
                          "Without host fairness host A (2 flows) should out-serve host B ("
                              << aFlow << " vs " << bFlow << ")");

    uint32_t aHost;
    uint32_t bHost;
    RunScenario(true, aHost, bHost);
    // Host fairness: the two hosts should get roughly equal shares.
    bool balanced = (aHost <= bHost * 1.3) && (bHost <= aHost * 1.3);
    NS_TEST_ASSERT_MSG_EQ(balanced,
                          true,
                          "With host fairness hosts A and B should get ~equal shares ("
                              << aHost << " vs " << bHost << ")");
}

/**
 * @ingroup traffic-control-test
 *
 * @brief CAKE per-tin rate-cap test
 *
 * With shaping enabled, each tin is limited to its share of the bandwidth. A
 * high-priority but small-share tin (latency) is therefore out-served by a
 * low-priority but large-share tin (best effort) when both are backlogged,
 * while a single backlogged tin borrows the whole link (work conserving).
 */
class CakeTinRateCapTestCase : public TestCase
{
  public:
    CakeTinRateCapTestCase();
    void DoRun() override;

  private:
    void OnSend(Ptr<QueueDiscItem> item);
    /**
     * Drive the shaped disc and tally transmitted packets per tin.
     * @param backlogBe whether to also backlog the best-effort tin
     * @param latencyCount [out] packets sent from the latency tin
     * @param beCount [out] packets sent from the best-effort tin
     * @param total [out] total packets sent
     */
    void RunScenario(bool backlogBe, uint32_t& latencyCount, uint32_t& beCount, uint32_t& total);

    std::map<uint64_t, uint8_t> m_uidPrio; //!< packet UID -> priority (tin marker)
    std::vector<uint64_t> m_sentUids;      //!< UIDs of transmitted packets, in order
};

CakeTinRateCapTestCase::CakeTinRateCapTestCase()
    : TestCase("CAKE limits each tin to its bandwidth share and borrows idle capacity")
{
}

void
CakeTinRateCapTestCase::OnSend(Ptr<QueueDiscItem> item)
{
    m_sentUids.push_back(item->GetPacket()->GetUid());
}

void
CakeTinRateCapTestCase::RunScenario(bool backlogBe,
                                    uint32_t& latencyCount,
                                    uint32_t& beCount,
                                    uint32_t& total)
{
    m_uidPrio.clear();
    m_sentUids.clear();

    Ptr<CakeQueueDisc> queue = CreateObject<CakeQueueDisc>();
    queue->SetQuantum(1000);
    queue->SetAttribute("DiffServMode", EnumValue(CakeQueueDisc::DIFFSERV_DIFFSERV3));
    // 8 Mbps == 1 ms per 1000-byte packet.
    queue->SetAttribute("Bandwidth", DataRateValue(DataRate("8Mbps")));
    queue->SetSendCallback([this](Ptr<QueueDiscItem> item) { OnSend(item); });
    queue->Initialize();

    Address dest;
    auto enqueue = [&](uint8_t prio, uint32_t hash) {
        Ptr<Packet> p = Create<Packet>(1000);
        SocketPriorityTag tag;
        tag.SetPriority(prio);
        p->AddPacketTag(tag);
        m_uidPrio[p->GetUid()] = prio;
        queue->Enqueue(Create<CakeQueueDiscTestItem>(p, dest, hash));
    };

    const uint32_t perFlow = 300; // deep backlog so the shaper, not the queue, is the limit
    for (uint32_t i = 0; i < perFlow; i++)
    {
        enqueue(6, 0); // priority 6 -> latency tin (tin 0), small share, high priority
        if (backlogBe)
        {
            enqueue(0, 1); // priority 0 -> best-effort tin (tin 1), large share, low priority
        }
    }

    queue->Run();
    Simulator::Stop(Seconds(0.15));
    Simulator::Run();

    latencyCount = 0;
    beCount = 0;
    for (uint64_t uid : m_sentUids)
    {
        uint8_t prio = m_uidPrio[uid];
        if (prio == 6)
        {
            latencyCount++;
        }
        else if (prio == 0)
        {
            beCount++;
        }
    }
    total = m_sentUids.size();

    Simulator::Destroy();
}

void
CakeTinRateCapTestCase::DoRun()
{
    // Both tins backlogged: the best-effort tin (16/21 share) must out-serve the
    // rate-capped latency tin (4/21 share) despite the latter's higher priority.
    uint32_t lat;
    uint32_t be;
    uint32_t total;
    RunScenario(true, lat, be, total);
    NS_TEST_ASSERT_MSG_EQ(be > lat * 2,
                          true,
                          "The large-share best-effort tin should out-serve the rate-capped "
                          "high-priority latency tin ("
                              << lat << " vs " << be << ")");
    NS_TEST_ASSERT_MSG_EQ(lat > total / 10,
                          true,
                          "The latency tin should still receive roughly its rate share ("
                              << lat << " of " << total << ")");

    // Only the latency tin backlogged: it must borrow idle capacity and reach
    // close to the full link rate (~150 packets over 0.15 s at 8 Mbps).
    uint32_t latOnly;
    uint32_t beOnly;
    uint32_t totalOnly;
    RunScenario(false, latOnly, beOnly, totalOnly);
    NS_TEST_ASSERT_MSG_EQ(latOnly > 100,
                          true,
                          "A single backlogged tin should borrow idle capacity and reach "
                          "near full rate ("
                              << latOnly << ")");
}

/**
 * @ingroup traffic-control-test
 *
 * @brief Cake Queue Disc Test Suite
 */
static class CakeQueueDiscTestSuite : public TestSuite
{
  public:
    CakeQueueDiscTestSuite()
        : TestSuite("cake-queue-disc", Type::UNIT)
    {
        AddTestCase(new CakeSingleFlowTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new CakeTwoFlowDrrTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new CakeShaperTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new CakeUnlimitedTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new CakeTinPriorityTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new CakeAckFilterTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new CakeHostFairnessTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new CakeTinRateCapTestCase(), TestCase::Duration::QUICK);
    }
} g_cakeQueueTestSuite; ///< the test suite
