/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "cake-queue-disc.h"

#include "cobalt-queue-disc.h"

#include "ns3/boolean.h"
#include "ns3/data-rate.h"
#include "ns3/enum.h"
#include "ns3/log.h"
#include "ns3/net-device-queue-interface.h"
#include "ns3/object-factory.h"
#include "ns3/queue.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"

#include <algorithm>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("CakeQueueDisc");

NS_OBJECT_ENSURE_REGISTERED(CakeFlow);

TypeId
CakeFlow::GetTypeId()
{
    static TypeId tid = TypeId("ns3::CakeFlow")
                            .SetParent<QueueDiscClass>()
                            .SetGroupName("TrafficControl")
                            .AddConstructor<CakeFlow>();
    return tid;
}

CakeFlow::CakeFlow()
    : m_deficit(0),
      m_status(INACTIVE),
      m_index(0)
{
    NS_LOG_FUNCTION(this);
}

CakeFlow::~CakeFlow()
{
    NS_LOG_FUNCTION(this);
}

void
CakeFlow::SetDeficit(uint32_t deficit)
{
    NS_LOG_FUNCTION(this << deficit);
    m_deficit = deficit;
}

int32_t
CakeFlow::GetDeficit() const
{
    NS_LOG_FUNCTION(this);
    return m_deficit;
}

void
CakeFlow::IncreaseDeficit(int32_t deficit)
{
    NS_LOG_FUNCTION(this << deficit);
    m_deficit += deficit;
}

void
CakeFlow::SetStatus(FlowStatus status)
{
    NS_LOG_FUNCTION(this);
    m_status = status;
}

CakeFlow::FlowStatus
CakeFlow::GetStatus() const
{
    NS_LOG_FUNCTION(this);
    return m_status;
}

void
CakeFlow::SetIndex(uint32_t index)
{
    NS_LOG_FUNCTION(this);
    m_index = index;
}

uint32_t
CakeFlow::GetIndex() const
{
    return m_index;
}

void
CakeFlow::SetLastAck(uint64_t uid, uint32_t ackNo)
{
    m_hasLastAck = true;
    m_lastAckUid = uid;
    m_lastAckNo = ackNo;
}

bool
CakeFlow::HasLastAck() const
{
    return m_hasLastAck;
}

uint64_t
CakeFlow::GetLastAckUid() const
{
    return m_lastAckUid;
}

uint32_t
CakeFlow::GetLastAckNo() const
{
    return m_lastAckNo;
}

void
CakeFlow::ClearLastAck()
{
    m_hasLastAck = false;
}

void
CakeFlow::MarkFiltered(uint64_t uid)
{
    m_filteredUids.insert(uid);
}

bool
CakeFlow::IsFiltered(uint64_t uid) const
{
    return m_filteredUids.find(uid) != m_filteredUids.end();
}

void
CakeFlow::ClearFiltered(uint64_t uid)
{
    m_filteredUids.erase(uid);
}

void
CakeFlow::SetHosts(uint32_t srcHost, uint32_t dstHost)
{
    m_srcHost = srcHost;
    m_dstHost = dstHost;
}

uint32_t
CakeFlow::GetSrcHost() const
{
    return m_srcHost;
}

uint32_t
CakeFlow::GetDstHost() const
{
    return m_dstHost;
}

void
CakeFlow::SetHostCounted(bool counted)
{
    m_hostCounted = counted;
}

bool
CakeFlow::IsHostCounted() const
{
    return m_hostCounted;
}

NS_OBJECT_ENSURE_REGISTERED(CakeQueueDisc);

TypeId
CakeQueueDisc::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::CakeQueueDisc")
            .SetParent<QueueDisc>()
            .SetGroupName("TrafficControl")
            .AddConstructor<CakeQueueDisc>()
            .AddAttribute("MaxSize",
                          "The maximum number of packets accepted by this queue disc",
                          QueueSizeValue(QueueSize("10240p")),
                          MakeQueueSizeAccessor(&QueueDisc::SetMaxSize, &QueueDisc::GetMaxSize),
                          MakeQueueSizeChecker())
            .AddAttribute("Bandwidth",
                          "Shaper rate; 0 bps disables shaping (unlimited mode)",
                          DataRateValue(DataRate("0bps")),
                          MakeDataRateAccessor(&CakeQueueDisc::m_bandwidth),
                          MakeDataRateChecker())
            .AddAttribute("DiffServMode",
                          "DiffServ tin configuration",
                          EnumValue(DIFFSERV_DIFFSERV3),
                          MakeEnumAccessor<DiffServMode>(&CakeQueueDisc::m_diffServMode),
                          MakeEnumChecker(DIFFSERV_BESTEFFORT,
                                          "BestEffort",
                                          DIFFSERV_DIFFSERV3,
                                          "DiffServ3",
                                          DIFFSERV_DIFFSERV4,
                                          "DiffServ4",
                                          DIFFSERV_PRECEDENCE,
                                          "Precedence"))
            .AddAttribute("AckFilter",
                          "TCP ACK filtering mode",
                          EnumValue(ACK_FILTER_DISABLED),
                          MakeEnumAccessor<AckFilterMode>(&CakeQueueDisc::m_ackFilter),
                          MakeEnumChecker(ACK_FILTER_DISABLED,
                                          "Disabled",
                                          ACK_FILTER_FILTER,
                                          "Filter",
                                          ACK_FILTER_AGGRESSIVE,
                                          "Aggressive"))
            .AddAttribute("Overhead",
                          "Per-packet link-layer overhead added by the shaper, in bytes",
                          UintegerValue(0),
                          MakeUintegerAccessor(&CakeQueueDisc::m_overhead),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("UseEcn",
                          "True to use ECN (packets are marked instead of being dropped)",
                          BooleanValue(true),
                          MakeBooleanAccessor(&CakeQueueDisc::m_useEcn),
                          MakeBooleanChecker())
            .AddAttribute("Interval",
                          "The CoDel algorithm interval for each flow queue",
                          StringValue("100ms"),
                          MakeStringAccessor(&CakeQueueDisc::m_interval),
                          MakeStringChecker())
            .AddAttribute("Target",
                          "The CoDel algorithm target queue delay for each flow queue",
                          StringValue("5ms"),
                          MakeStringAccessor(&CakeQueueDisc::m_target),
                          MakeStringChecker())
            .AddAttribute("Flows",
                          "The number of flow queues per tin",
                          UintegerValue(1024),
                          MakeUintegerAccessor(&CakeQueueDisc::m_flows),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("DropBatchSize",
                          "The maximum number of packets dropped from the fat flow",
                          UintegerValue(64),
                          MakeUintegerAccessor(&CakeQueueDisc::m_dropBatchSize),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("Perturbation",
                          "The salt used as an additional input to the hash function",
                          UintegerValue(0),
                          MakeUintegerAccessor(&CakeQueueDisc::m_perturbation),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("EnableSetAssociativeHash",
                          "Enable/Disable set associative hashing",
                          BooleanValue(false),
                          MakeBooleanAccessor(&CakeQueueDisc::m_enableSetAssociativeHash),
                          MakeBooleanChecker())
            .AddAttribute("SetWays",
                          "The size of a set of queues (used by set associative hash)",
                          UintegerValue(8),
                          MakeUintegerAccessor(&CakeQueueDisc::m_setWays),
                          MakeUintegerChecker<uint32_t>());
    return tid;
}

CakeQueueDisc::CakeQueueDisc()
    : QueueDisc(QueueDiscSizePolicy::MULTIPLE_QUEUES, QueueSizeUnit::PACKETS),
      m_quantum(0)
{
    NS_LOG_FUNCTION(this);
}

CakeQueueDisc::~CakeQueueDisc()
{
    NS_LOG_FUNCTION(this);
}

void
CakeQueueDisc::SetQuantum(uint32_t quantum)
{
    NS_LOG_FUNCTION(this << quantum);
    m_quantum = quantum;
}

uint32_t
CakeQueueDisc::GetQuantum() const
{
    return m_quantum;
}

void
CakeQueueDisc::SetAckIdentifier(AckIdentifier identifier)
{
    NS_LOG_FUNCTION(this);
    m_ackIdentifier = identifier;
}

void
CakeQueueDisc::SetHostClassifier(HostClassifier classifier)
{
    NS_LOG_FUNCTION(this);
    m_hostClassifier = classifier;
    m_hostFairness = static_cast<bool>(classifier);
}

uint32_t
CakeQueueDisc::GrantQuantum(const CakeTin& tin, const CakeFlow& flow) const
{
    if (!m_hostFairness)
    {
        return m_quantum;
    }
    uint32_t srcLoad = 1;
    uint32_t dstLoad = 1;
    auto s = tin.srcHostCount.find(flow.GetSrcHost());
    if (s != tin.srcHostCount.end() && s->second > srcLoad)
    {
        srcLoad = s->second;
    }
    auto d = tin.dstHostCount.find(flow.GetDstHost());
    if (d != tin.dstHostCount.end() && d->second > dstLoad)
    {
        dstLoad = d->second;
    }
    uint32_t hostLoad = std::max(srcLoad, dstLoad);
    return std::max(1u, m_quantum / hostLoad);
}

void
CakeQueueDisc::ActivateHost(CakeTin& tin, Ptr<CakeFlow> flow)
{
    if (!m_hostFairness || flow->IsHostCounted())
    {
        return;
    }
    tin.srcHostCount[flow->GetSrcHost()]++;
    tin.dstHostCount[flow->GetDstHost()]++;
    flow->SetHostCounted(true);
}

void
CakeQueueDisc::DeactivateHost(CakeTin& tin, Ptr<CakeFlow> flow)
{
    if (!m_hostFairness || !flow->IsHostCounted())
    {
        return;
    }
    auto s = tin.srcHostCount.find(flow->GetSrcHost());
    if (s != tin.srcHostCount.end() && --(s->second) == 0)
    {
        tin.srcHostCount.erase(s);
    }
    auto d = tin.dstHostCount.find(flow->GetDstHost());
    if (d != tin.dstHostCount.end() && --(d->second) == 0)
    {
        tin.dstHostCount.erase(d);
    }
    flow->SetHostCounted(false);
}

void
CakeQueueDisc::TryAckFilter(Ptr<CakeFlow> flow, Ptr<QueueDiscItem> item)
{
    NS_LOG_FUNCTION(this << item);

    uint32_t ackNo;
    if (!m_ackIdentifier(item, ackNo))
    {
        return; // not a pure ACK; nothing to filter
    }

    uint64_t uid = item->GetPacket()->GetUid();

    // If this flow already has a queued ACK that this one acknowledges past (or
    // equals), that older ACK is now redundant. Mark it to be dropped when it
    // reaches the head of the flow queue (we cannot remove it in place).
    if (flow->HasLastAck() && ackNo >= flow->GetLastAckNo())
    {
        flow->MarkFiltered(flow->GetLastAckUid());
    }

    flow->SetLastAck(uid, ackNo);
}

void
CakeQueueDisc::SetupTins()
{
    NS_LOG_FUNCTION(this);

    uint32_t numTins;
    std::vector<uint32_t> weights; // relative bandwidth weight per tin
    m_prioMap.assign(16, 0);

    switch (m_diffServMode)
    {
    case DIFFSERV_BESTEFFORT:
        numTins = 1;
        weights = {1};
        // all priorities map to the single tin (m_prioMap already all-zero)
        break;
    case DIFFSERV_DIFFSERV4:
        // tins: 0 voice, 1 video, 2 best effort, 3 bulk
        numTins = 4;
        weights = {4, 8, 16, 1};
        m_prioMap = {2, 3, 3, 3, 1, 1, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2};
        break;
    case DIFFSERV_PRECEDENCE:
        // tins follow IP precedence; higher precedence -> lower (higher-priority) index
        numTins = 8;
        weights = {1, 1, 1, 1, 1, 1, 1, 1};
        for (uint8_t p = 0; p < 16; p++)
        {
            m_prioMap[p] = 7 - (p & 0x07);
        }
        break;
    case DIFFSERV_DIFFSERV3:
    default:
        // tins: 0 latency-sensitive, 1 best effort, 2 bulk
        numTins = 3;
        weights = {4, 16, 1};
        m_prioMap = {1, 2, 2, 2, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1};
        break;
    }

    uint32_t base = (m_quantum ? m_quantum : 1500);
    uint64_t sumWeights = 0;
    for (uint32_t w : weights)
    {
        sumWeights += w;
    }
    uint64_t totalBps = m_bandwidth.GetBitRate();

    m_tins.clear();
    m_tins.resize(numTins);
    for (uint32_t i = 0; i < numTins; i++)
    {
        m_tins[i].quantum = weights[i] * base; // weights are >= 1, so quantum > 0
        m_tins[i].deficit = static_cast<int32_t>(m_tins[i].quantum);
        // Per-tin threshold rate = this tin's share of the shaper bandwidth.
        uint64_t tinBps =
            (totalBps && sumWeights) ? std::max<uint64_t>(1, totalBps * weights[i] / sumWeights) : 0;
        m_tins[i].rate = DataRate(tinBps);
        m_tins[i].timeNextPacket = Seconds(0);
    }
}

uint8_t
CakeQueueDisc::ClassifyTin(Ptr<QueueDiscItem> item) const
{
    uint8_t prio = 0;
    SocketPriorityTag prioTag;
    if (item->GetPacket()->PeekPacketTag(prioTag))
    {
        prio = prioTag.GetPriority();
    }

    uint8_t tin = m_prioMap[prio & 0x0f];
    if (tin >= m_tins.size())
    {
        tin = 0;
    }
    return tin;
}

bool
CakeQueueDisc::TinHasTraffic(const CakeTin& tin) const
{
    return !tin.newFlows.empty() || !tin.oldFlows.empty();
}

uint32_t
CakeQueueDisc::SetAssociativeHash(CakeTin& tin, uint32_t flowHash)
{
    NS_LOG_FUNCTION(this << flowHash);

    uint32_t h = (flowHash % m_flows);
    uint32_t innerHash = h % m_setWays;
    uint32_t outerHash = h - innerHash;

    for (uint32_t i = outerHash; i < outerHash + m_setWays; i++)
    {
        auto it = tin.flowsIndices.find(i);

        if (it == tin.flowsIndices.end() ||
            (tin.tags.find(i) != tin.tags.end() && tin.tags[i] == flowHash) ||
            StaticCast<CakeFlow>(GetQueueDiscClass(it->second))->GetStatus() == CakeFlow::INACTIVE)
        {
            tin.tags[i] = flowHash;
            return i;
        }
    }

    tin.tags[outerHash] = flowHash;
    return outerHash;
}

bool
CakeQueueDisc::DoEnqueue(Ptr<QueueDiscItem> item)
{
    NS_LOG_FUNCTION(this << item);

    uint32_t flowHash;

    if (GetNPacketFilters() == 0)
    {
        flowHash = item->Hash(m_perturbation);
    }
    else
    {
        int32_t ret = Classify(item);

        if (ret != PacketFilter::PF_NO_MATCH)
        {
            flowHash = static_cast<uint32_t>(ret);
        }
        else
        {
            NS_LOG_ERROR("No filter has been able to classify this packet, drop it.");
            DropBeforeEnqueue(item, UNCLASSIFIED_DROP);
            return false;
        }
    }

    CakeTin& tin = m_tins[ClassifyTin(item)];

    uint32_t h;
    if (m_enableSetAssociativeHash)
    {
        h = SetAssociativeHash(tin, flowHash);
    }
    else
    {
        h = flowHash % m_flows;
    }

    Ptr<CakeFlow> flow;
    if (tin.flowsIndices.find(h) == tin.flowsIndices.end())
    {
        NS_LOG_DEBUG("Creating a new flow queue with index " << h);
        flow = m_flowFactory.Create<CakeFlow>();
        Ptr<QueueDisc> qd = m_queueDiscFactory.Create<QueueDisc>();
        Ptr<CobaltQueueDisc> cobalt = qd->GetObject<CobaltQueueDisc>();
        if (cobalt)
        {
            cobalt->SetAttribute("UseEcn", BooleanValue(m_useEcn));
        }
        qd->Initialize();
        flow->SetQueueDisc(qd);
        flow->SetIndex(h);
        AddQueueDiscClass(flow);

        tin.flowsIndices[h] = GetNQueueDiscClasses() - 1;
    }
    else
    {
        flow = StaticCast<CakeFlow>(GetQueueDiscClass(tin.flowsIndices[h]));
    }

    if (flow->GetStatus() == CakeFlow::INACTIVE)
    {
        if (m_hostFairness)
        {
            uint32_t srcHost = 0;
            uint32_t dstHost = 0;
            m_hostClassifier(item, srcHost, dstHost);
            flow->SetHosts(srcHost, dstHost);
            ActivateHost(tin, flow);
        }
        flow->SetStatus(CakeFlow::NEW_FLOW);
        flow->SetDeficit(GrantQuantum(tin, *flow));
        tin.newFlows.push_back(flow);
    }

    flow->GetQueueDisc()->Enqueue(item);

    if (m_ackFilter != ACK_FILTER_DISABLED && m_ackIdentifier)
    {
        TryAckFilter(flow, item);
    }

    if (GetCurrentSize() > GetMaxSize())
    {
        NS_LOG_DEBUG("Overload; enter CakeDrop ()");
        CakeDrop();
    }

    return true;
}

Ptr<QueueDiscItem>
CakeQueueDisc::DequeueFromTin(CakeTin& tin)
{
    NS_LOG_FUNCTION(this);

    Ptr<CakeFlow> flow;
    Ptr<QueueDiscItem> item;

    do
    {
        bool found = false;

        while (!found && !tin.newFlows.empty())
        {
            flow = tin.newFlows.front();

            if (flow->GetDeficit() <= 0)
            {
                flow->IncreaseDeficit(static_cast<int32_t>(GrantQuantum(tin, *flow)));
                flow->SetStatus(CakeFlow::OLD_FLOW);
                tin.oldFlows.push_back(flow);
                tin.newFlows.pop_front();
            }
            else
            {
                found = true;
            }
        }

        while (!found && !tin.oldFlows.empty())
        {
            flow = tin.oldFlows.front();

            if (flow->GetDeficit() <= 0)
            {
                flow->IncreaseDeficit(static_cast<int32_t>(GrantQuantum(tin, *flow)));
                tin.oldFlows.push_back(flow);
                tin.oldFlows.pop_front();
            }
            else
            {
                found = true;
            }
        }

        if (!found)
        {
            return nullptr;
        }

        item = flow->GetQueueDisc()->Dequeue();

        // Drop any ACKs that were superseded while queued (ACK filtering). These
        // are dropped lazily here because the flow's internal queue does not
        // support removing an item from the middle. Dropped ACKs are not charged
        // against the flow's deficit since they are never transmitted.
        while (item && flow->IsFiltered(item->GetPacket()->GetUid()))
        {
            flow->ClearFiltered(item->GetPacket()->GetUid());
            DropAfterDequeue(item, ACK_FILTER_DROP);
            item = flow->GetQueueDisc()->Dequeue();
        }

        if (item && flow->HasLastAck() && item->GetPacket()->GetUid() == flow->GetLastAckUid())
        {
            // The most recent ACK is leaving the queue; stop tracking it.
            flow->ClearLastAck();
        }

        if (!item)
        {
            if (!tin.newFlows.empty())
            {
                flow->SetStatus(CakeFlow::OLD_FLOW);
                tin.oldFlows.push_back(flow);
                tin.newFlows.pop_front();
            }
            else
            {
                flow->SetStatus(CakeFlow::INACTIVE);
                DeactivateHost(tin, flow);
                tin.oldFlows.pop_front();
            }
        }
    } while (!item);

    flow->IncreaseDeficit(item->GetSize() * -1);

    return item;
}

Ptr<QueueDiscItem>
CakeQueueDisc::DoDequeue()
{
    NS_LOG_FUNCTION(this);

    const bool shaping = (m_bandwidth.GetBitRate() != 0);
    const Time now = Simulator::Now();

    // Deficit-mode shaper gate: while we are ahead of the virtual transmission
    // clock, hold packets back and wake ourselves when the link is next free.
    if (shaping && now < m_timeNextPacket)
    {
        if (GetCurrentSize().GetValue() > 0 && m_shaperWake.IsExpired())
        {
            Time delay = m_timeNextPacket - now;
            m_shaperWake = Simulator::Schedule(delay, &QueueDisc::Run, this);
            NS_LOG_LOGIC("Shaper throttling; waking in " << delay.As(Time::S));
        }
        return nullptr;
    }

    // Inter-tin scheduler. With shaping enabled, each tin runs its own
    // virtual-clock shaper at its share of the bandwidth: serve the
    // highest-priority tin that has traffic and is "due" (its clock has caught
    // up to now); if none is due, serve the tin that is least over its budget
    // (work-conserving borrowing of idle capacity). Without shaping there is no
    // total rate to apportion, so fall back to priority + weighted DRR.
    Ptr<QueueDiscItem> item;
    uint32_t guard = m_tins.size() * 3 + 4;

    if (shaping)
    {
        while (guard-- > 0)
        {
            int best = -1;
            for (uint32_t t = 0; t < m_tins.size(); t++)
            {
                if (TinHasTraffic(m_tins[t]) && m_tins[t].timeNextPacket <= now)
                {
                    best = static_cast<int>(t);
                    break;
                }
            }
            if (best < 0)
            {
                // No tin is due: borrow idle capacity for the least over-budget tin.
                Time earliest = Time::Max();
                for (uint32_t t = 0; t < m_tins.size(); t++)
                {
                    if (TinHasTraffic(m_tins[t]) && m_tins[t].timeNextPacket < earliest)
                    {
                        earliest = m_tins[t].timeNextPacket;
                        best = static_cast<int>(t);
                    }
                }
            }
            if (best < 0)
            {
                return nullptr; // no tin has traffic
            }

            item = DequeueFromTin(m_tins[best]);
            if (item)
            {
                Time tinTx =
                    m_tins[best].rate.CalculateBytesTxTime(item->GetSize() + m_overhead);
                if (m_tins[best].timeNextPacket < now)
                {
                    m_tins[best].timeNextPacket = now;
                }
                m_tins[best].timeNextPacket += tinTx;
                break;
            }
            // Selected tin drained (e.g. via AQM); loop to reconsider.
        }
    }
    else
    {
        while (guard-- > 0)
        {
            int best = -1;
            bool anyTraffic = false;

            for (uint32_t t = 0; t < m_tins.size(); t++)
            {
                if (TinHasTraffic(m_tins[t]))
                {
                    anyTraffic = true;
                    if (m_tins[t].deficit > 0)
                    {
                        best = static_cast<int>(t);
                        break;
                    }
                }
            }

            if (best >= 0)
            {
                item = DequeueFromTin(m_tins[best]);
                if (item)
                {
                    m_tins[best].deficit -= static_cast<int32_t>(item->GetSize());
                    break;
                }
            }
            else if (anyTraffic)
            {
                for (auto& tin : m_tins)
                {
                    tin.deficit += static_cast<int32_t>(tin.quantum);
                }
            }
            else
            {
                return nullptr;
            }
        }
    }

    if (!item)
    {
        return nullptr;
    }

    // Advance the shaper's virtual clock by the time needed to transmit this
    // packet (including link-layer overhead). If the queue had gone idle we do
    // not let credit accumulate, so a burst cannot follow an idle period.
    if (shaping)
    {
        uint32_t len = item->GetSize() + m_overhead;
        Time txTime = m_bandwidth.CalculateBytesTxTime(len);
        if (m_timeNextPacket < now)
        {
            m_timeNextPacket = now;
        }
        m_timeNextPacket += txTime;
    }

    return item;
}

bool
CakeQueueDisc::CheckConfig()
{
    NS_LOG_FUNCTION(this);
    if (GetNQueueDiscClasses() > 0)
    {
        NS_LOG_ERROR("CakeQueueDisc cannot have classes");
        return false;
    }

    if (GetNInternalQueues() > 0)
    {
        NS_LOG_ERROR("CakeQueueDisc cannot have internal queues");
        return false;
    }

    // If the user has not set a quantum value, set the quantum to the MTU of
    // the device (if any).
    if (!m_quantum)
    {
        Ptr<NetDeviceQueueInterface> ndqi = GetNetDeviceQueueInterface();
        Ptr<NetDevice> dev;
        if (ndqi && (dev = ndqi->GetObject<NetDevice>()))
        {
            m_quantum = dev->GetMtu();
            NS_LOG_DEBUG("Setting the quantum to the MTU of the device: " << m_quantum);
        }

        if (!m_quantum)
        {
            NS_LOG_ERROR("The quantum parameter cannot be null");
            return false;
        }
    }

    if (m_enableSetAssociativeHash && (m_flows % m_setWays != 0))
    {
        NS_LOG_ERROR("The number of queues must be an integer multiple of the size "
                     "of the set of queues used by set associative hash");
        return false;
    }

    return true;
}

void
CakeQueueDisc::InitializeParams()
{
    NS_LOG_FUNCTION(this);

    m_flowFactory.SetTypeId("ns3::CakeFlow");

    m_queueDiscFactory.SetTypeId("ns3::CobaltQueueDisc");
    m_queueDiscFactory.Set("MaxSize", QueueSizeValue(GetMaxSize()));
    m_queueDiscFactory.Set("Interval", StringValue(m_interval));
    m_queueDiscFactory.Set("Target", StringValue(m_target));

    m_timeNextPacket = Seconds(0);
    m_shaperWake = EventId();

    SetupTins();
}

uint32_t
CakeQueueDisc::CakeDrop()
{
    NS_LOG_FUNCTION(this);

    uint32_t maxBacklog = 0;
    uint32_t index = 0;
    Ptr<QueueDisc> qd;

    // Queue is full: find the fat flow and drop packet(s) from it.
    for (uint32_t i = 0; i < GetNQueueDiscClasses(); i++)
    {
        qd = GetQueueDiscClass(i)->GetQueueDisc();
        uint32_t bytes = qd->GetNBytes();
        if (bytes > maxBacklog)
        {
            maxBacklog = bytes;
            index = i;
        }
    }

    // Drop approximately half of the fat flow's backlog.
    uint32_t len = 0;
    uint32_t count = 0;
    uint32_t threshold = maxBacklog >> 1;
    qd = GetQueueDiscClass(index)->GetQueueDisc();
    Ptr<QueueDiscItem> item;

    do
    {
        NS_LOG_DEBUG("Drop packet (overflow); count: " << count << " len: " << len
                                                       << " threshold: " << threshold);
        item = qd->GetInternalQueue(0)->Dequeue();
        DropAfterDequeue(item, OVERLIMIT_DROP);
        len += item->GetSize();
    } while (++count < m_dropBatchSize && len < threshold);

    return index;
}

} // namespace ns3
