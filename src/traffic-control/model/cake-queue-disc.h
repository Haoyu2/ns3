/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef CAKE_QUEUE_DISC_H
#define CAKE_QUEUE_DISC_H

#include "queue-disc.h"

#include "ns3/data-rate.h"
#include "ns3/event-id.h"
#include "ns3/nstime.h"
#include "ns3/object-factory.h"

#include <functional>
#include <list>
#include <map>
#include <set>
#include <vector>

namespace ns3
{

/**
 * @ingroup traffic-control
 *
 * @brief A flow queue used by the CAKE queue disc.
 *
 * Each flow owns a child queue disc (a CobaltQueueDisc) and carries the DRR
 * deficit and scheduling status used by the CAKE flow scheduler.
 */
class CakeFlow : public QueueDiscClass
{
  public:
    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    CakeFlow();
    ~CakeFlow() override;

    /**
     * @enum FlowStatus
     * @brief Used to determine the status of this flow queue.
     */
    enum FlowStatus
    {
        INACTIVE,
        NEW_FLOW,
        OLD_FLOW
    };

    /**
     * @brief Set the deficit for this flow.
     * @param deficit the deficit for this flow
     */
    void SetDeficit(uint32_t deficit);
    /**
     * @brief Get the deficit for this flow.
     * @return the deficit for this flow
     */
    int32_t GetDeficit() const;
    /**
     * @brief Increase the deficit for this flow.
     * @param deficit the amount by which the deficit is to be increased
     */
    void IncreaseDeficit(int32_t deficit);
    /**
     * @brief Set the status for this flow.
     * @param status the status for this flow
     */
    void SetStatus(FlowStatus status);
    /**
     * @brief Get the status of this flow.
     * @return the status of this flow
     */
    FlowStatus GetStatus() const;
    /**
     * @brief Set the index for this flow.
     * @param index the index for this flow
     */
    void SetIndex(uint32_t index);
    /**
     * @brief Get the index of this flow.
     * @return the index of this flow
     */
    uint32_t GetIndex() const;

    // --- ACK-filtering state (tracks the most recent pure ACK in this flow) ---

    /**
     * @brief Record the most recent pure ACK enqueued in this flow.
     * @param uid the packet UID of the ACK
     * @param ackNo the acknowledgement number carried by the ACK
     */
    void SetLastAck(uint64_t uid, uint32_t ackNo);
    /// @return true if a pending (queued) ACK is being tracked for this flow
    bool HasLastAck() const;
    /// @return the packet UID of the tracked pending ACK
    uint64_t GetLastAckUid() const;
    /// @return the acknowledgement number of the tracked pending ACK
    uint32_t GetLastAckNo() const;
    /// @brief Forget the tracked pending ACK (e.g. once it has been dequeued).
    void ClearLastAck();
    /**
     * @brief Mark a queued packet to be dropped (lazily) at dequeue time.
     * @param uid the packet UID to drop
     */
    void MarkFiltered(uint64_t uid);
    /**
     * @brief Whether a packet has been marked as a filtered (superseded) ACK.
     * @param uid the packet UID to test
     * @return true if the packet should be dropped at dequeue
     */
    bool IsFiltered(uint64_t uid) const;
    /**
     * @brief Clear the filtered mark for a packet UID.
     * @param uid the packet UID
     */
    void ClearFiltered(uint64_t uid);

    // --- Host-fairness state ---

    /**
     * @brief Set this flow's source and destination host keys.
     * @param srcHost source host key
     * @param dstHost destination host key
     */
    void SetHosts(uint32_t srcHost, uint32_t dstHost);
    /// @return this flow's source host key
    uint32_t GetSrcHost() const;
    /// @return this flow's destination host key
    uint32_t GetDstHost() const;
    /**
     * @brief Mark whether this flow is currently counted in the per-host tallies.
     * @param counted true if counted
     */
    void SetHostCounted(bool counted);
    /// @return true if this flow is currently counted in the per-host tallies
    bool IsHostCounted() const;

  private:
    int32_t m_deficit;   //!< the deficit for this flow
    FlowStatus m_status; //!< the status of this flow
    uint32_t m_index;    //!< the index for this flow

    bool m_hasLastAck{false};         //!< whether a pending ACK is tracked
    uint64_t m_lastAckUid{0};         //!< UID of the most recent pure ACK
    uint32_t m_lastAckNo{0};          //!< acknowledgement number of that ACK
    std::set<uint64_t> m_filteredUids; //!< UIDs of superseded ACKs to drop at dequeue

    uint32_t m_srcHost{0};   //!< source host key (for host fairness)
    uint32_t m_dstHost{0};   //!< destination host key (for host fairness)
    bool m_hostCounted{false}; //!< whether this flow is counted in per-host tallies
};

/**
 * @ingroup traffic-control
 *
 * @brief A DiffServ "tin": a priority class with its own set of flow queues.
 *
 * Each tin holds the flow-isolation state (new/old flow DRR lists and the
 * set-associative hash bookkeeping) for the traffic mapped to it, plus the
 * deficit and quantum used by the inter-tin weighted scheduler.
 */
struct CakeTin
{
    std::list<Ptr<CakeFlow>> newFlows;           //!< new flows in this tin
    std::list<Ptr<CakeFlow>> oldFlows;           //!< old flows in this tin
    std::map<uint32_t, uint32_t> flowsIndices;   //!< local queue index -> parent class index
    std::map<uint32_t, uint32_t> tags;           //!< tags used by set-associative hash
    int32_t deficit{0};                          //!< inter-tin DRR deficit (unlimited mode)
    uint32_t quantum{0};                         //!< inter-tin DRR quantum (unlimited mode)
    std::map<uint32_t, uint32_t> srcHostCount;   //!< active flow count per source host
    std::map<uint32_t, uint32_t> dstHostCount;   //!< active flow count per destination host
    Time timeNextPacket;                         //!< per-tin virtual-clock shaper (shaping mode)
    DataRate rate;                               //!< per-tin threshold rate (shaping mode)
};

/**
 * @ingroup traffic-control
 *
 * @brief A CAKE (Common Applications Kept Enhanced) queue disc.
 *
 * This is an in-progress ns-3 model of Linux' sch_cake. CAKE is an integrated
 * shaper + AQM + flow/host isolation queueing discipline.
 *
 * Implemented so far:
 *  - flow isolation: per-flow CobaltQueueDisc behind a (set-associative) hash,
 *    scheduled with deficit round robin (DRR);
 *  - a deficit-mode shaper driven by the Bandwidth attribute;
 *  - DiffServ "tins": multiple priority classes, each with its own flow queues,
 *    classified by SocketPriorityTag and scheduled by priority with per-tin
 *    weighted DRR for bandwidth sharing.
 *
 * Still to come: ACK filtering, host fairness, and overhead/ATM compensation
 * refinements.
 */
class CakeQueueDisc : public QueueDisc
{
  public:
    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    CakeQueueDisc();
    ~CakeQueueDisc() override;

    /**
     * @brief DiffServ operating mode (number and meaning of priority "tins").
     */
    enum DiffServMode
    {
        DIFFSERV_BESTEFFORT, //!< single best-effort tin
        DIFFSERV_DIFFSERV3,  //!< three tins (latency-sensitive / best-effort / bulk)
        DIFFSERV_DIFFSERV4,  //!< four tins (voice / video / best-effort / bulk)
        DIFFSERV_PRECEDENCE  //!< eight tins by IP precedence (legacy/discouraged)
    };

    /**
     * @brief TCP ACK filtering mode.
     */
    enum AckFilterMode
    {
        ACK_FILTER_DISABLED,  //!< do not filter ACKs
        ACK_FILTER_FILTER,    //!< drop redundant ACKs conservatively
        ACK_FILTER_AGGRESSIVE //!< drop redundant ACKs aggressively
    };

    /**
     * @brief Set the quantum value.
     * @param quantum the number of bytes each flow may dequeue per DRR round
     */
    void SetQuantum(uint32_t quantum);
    /**
     * @brief Get the quantum value.
     * @return the number of bytes each flow may dequeue per DRR round
     */
    uint32_t GetQuantum() const;

    /**
     * @brief Callback used to recognise a pure TCP ACK and extract its ack number.
     *
     * Given a queued item, the callback returns true if the item is a pure
     * (data-less) TCP ACK, and on success sets the second argument to the
     * acknowledgement number. Header parsing lives in the callback so that the
     * CAKE model itself does not depend on the internet module; a concrete
     * TCP/IPv4 implementation can be supplied by the internet module or the user.
     */
    typedef std::function<bool(Ptr<const QueueDiscItem>, uint32_t&)> AckIdentifier;

    /**
     * @brief Set the callback used to identify pure TCP ACKs for ACK filtering.
     * @param identifier the callback
     */
    void SetAckIdentifier(AckIdentifier identifier);

    /**
     * @brief Callback used to extract a flow's source and destination host keys.
     *
     * Given a queued item, the callback sets two opaque keys identifying the
     * source and destination hosts (e.g. hashes of the IP addresses). It enables
     * CAKE's host-fair ("triple isolation") scheduling. As with the ACK
     * identifier, address parsing lives in the callback so the model stays free
     * of internet-module types; the internet module provides
     * MakeIpv4HostClassifier().
     */
    typedef std::function<void(Ptr<const QueueDiscItem>, uint32_t&, uint32_t&)> HostClassifier;

    /**
     * @brief Enable host-fair scheduling by supplying a host-key extractor.
     * @param classifier the callback
     */
    void SetHostClassifier(HostClassifier classifier);

    // Reasons for dropping packets
    static constexpr const char* UNCLASSIFIED_DROP =
        "Unclassified drop"; //!< No packet filter able to classify packet
    static constexpr const char* OVERLIMIT_DROP = "Overlimit drop"; //!< Overlimit dropped packets
    static constexpr const char* ACK_FILTER_DROP =
        "ACK filter drop"; //!< Redundant ACK removed by the ACK filter

  private:
    bool DoEnqueue(Ptr<QueueDiscItem> item) override;
    Ptr<QueueDiscItem> DoDequeue() override;
    bool CheckConfig() override;
    void InitializeParams() override;

    /**
     * @brief Build the tins and the priority->tin map for the current DiffServ mode.
     */
    void SetupTins();

    /**
     * @brief Map a packet to a tin using its SocketPriorityTag.
     * @param item the packet to classify
     * @return the index of the tin the packet belongs to
     */
    uint8_t ClassifyTin(Ptr<QueueDiscItem> item) const;

    /**
     * @brief Whether a tin currently has any backlogged flow.
     * @param tin the tin to inspect
     * @return true if the tin has traffic
     */
    bool TinHasTraffic(const CakeTin& tin) const;

    /**
     * @brief Dequeue one packet from a tin using its per-flow DRR scheduler.
     * @param tin the tin to dequeue from
     * @return the dequeued item, or nullptr if the tin is empty
     */
    Ptr<QueueDiscItem> DequeueFromTin(CakeTin& tin);

    /**
     * @brief Apply ACK filtering to a newly enqueued packet.
     *
     * If the item is a pure ACK that supersedes the flow's previously queued
     * ACK, mark that older ACK to be dropped at dequeue.
     * @param flow the flow the item was enqueued into
     * @param item the newly enqueued item
     */
    void TryAckFilter(Ptr<CakeFlow> flow, Ptr<QueueDiscItem> item);

    /**
     * @brief Per-round DRR grant for a flow, scaled down by its host load.
     *
     * With host fairness enabled, the grant is quantum / max(srcHostLoad,
     * dstHostLoad), so a host with many flows does not get more than its fair
     * share of host-level bandwidth. Without host fairness it is just quantum.
     * @param tin the tin the flow belongs to
     * @param flow the flow
     * @return the byte grant for this round (>= 1)
     */
    uint32_t GrantQuantum(const CakeTin& tin, const CakeFlow& flow) const;

    /**
     * @brief Count a newly active flow against its source and destination hosts.
     * @param tin the tin
     * @param flow the flow being activated
     */
    void ActivateHost(CakeTin& tin, Ptr<CakeFlow> flow);

    /**
     * @brief Remove a now-inactive flow from its per-host tallies.
     * @param tin the tin
     * @param flow the flow being deactivated
     */
    void DeactivateHost(CakeTin& tin, Ptr<CakeFlow> flow);

    /**
     * @brief Drop packet(s) from the head of the flow with the largest backlog.
     * @return the index of the flow with the largest backlog
     */
    uint32_t CakeDrop();

    /**
     * @brief Compute the queue index for a flow hash using set-associative hashing.
     * @param tin the tin the flow belongs to
     * @param flowHash the hash of the flow 5-tuple
     * @return the index of the queue for the given flow
     */
    uint32_t SetAssociativeHash(CakeTin& tin, uint32_t flowHash);

    // CAKE-specific configuration
    DataRate m_bandwidth;        //!< shaper rate; 0 bps means "unlimited" (no shaping)
    DiffServMode m_diffServMode; //!< DiffServ tin configuration
    AckFilterMode m_ackFilter;   //!< ACK filtering mode (not yet acted upon)
    uint32_t m_overhead;         //!< per-packet link-layer overhead, in bytes

    // Per-flow AQM + DRR scheduler
    std::string m_interval;          //!< CoDel interval for each flow's COBALT instance
    std::string m_target;            //!< CoDel target delay for each flow's COBALT instance
    uint32_t m_quantum;              //!< deficit assigned to flows at each DRR round
    uint32_t m_flows;                //!< number of flow queues per tin
    uint32_t m_setWays;              //!< size of a set of queues (set-associative hash)
    uint32_t m_dropBatchSize;        //!< max number of packets dropped from the fat flow
    uint32_t m_perturbation;         //!< hash perturbation value
    bool m_useEcn;                   //!< mark with ECN instead of dropping
    bool m_enableSetAssociativeHash; //!< whether to enable set-associative hashing

    std::vector<CakeTin> m_tins;  //!< the DiffServ tins, in priority order (index 0 highest)
    std::vector<uint8_t> m_prioMap; //!< maps SocketPriority (0-15) to a tin index

    ObjectFactory m_flowFactory;      //!< factory to create a new flow
    ObjectFactory m_queueDiscFactory; //!< factory to create a new per-flow queue disc

    AckIdentifier m_ackIdentifier;   //!< callback used to recognise pure TCP ACKs
    HostClassifier m_hostClassifier; //!< callback used to extract host keys
    bool m_hostFairness{false};      //!< whether host-fair scheduling is enabled

    // Deficit-mode shaper (virtual clock); inactive when m_bandwidth is 0 bps
    Time m_timeNextPacket; //!< earliest time the next packet may be dequeued
    EventId m_shaperWake;  //!< scheduled wake-up while the shaper is throttling
};

} // namespace ns3

#endif /* CAKE_QUEUE_DISC_H */
