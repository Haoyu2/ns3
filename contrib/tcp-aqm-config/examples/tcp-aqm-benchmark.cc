/*
 * Copyright (c) 2019 Cable Television Laboratories, Inc.
 * Copyright (c) 2020 Tom Henderson (adapted for DCTCP testing)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// This program is designed to observe long-running TCP congestion control
// behavior over a configurable bottleneck link.  The program is also
// instrumented to check program data against validated results, when
// the validation option is enabled.
//
// ---> downstream (primary data transfer from servers to clients)
// <--- upstream (return acks and ICMP echo response)
//
//              ----   bottleneck link    ----
//  servers ---| WR |--------------------| LR |--- clients
//              ----                      ----
//  ns-3 node IDs:
//  nodes 0-2    3                         4        5-7
//
// - The box WR is notionally a WAN router, aggregating all server links
// - The box LR is notionally a LAN router, aggregating all client links
// - Three servers are connected to WR, three clients are connected to LR
//
// clients and servers are configured for ICMP measurements and TCP throughput
// and latency measurements in the downstream direction
//
// All link rates are enforced by a point-to-point (P2P) ns-3 model with full
// duplex operation.  Dynamic queue limits
// (BQL) are enabled to allow for queueing to occur at the priority queue layer;
// the notional P2P hardware device queue is limited to three packets.
//
// One-way link delays and link rates
// -----------------------------------
// (1) server to WR links, 1000 Mbps, 1us delay
// (2) bottleneck link:  configurable rate, configurable delay
// (3) client to LR links, 1000 Mbps, 1us delay
//
// By default, ns-3 FQ-CoDel model is installed on all interfaces, but
// the bottleneck queue uses CoDel by default and is configurable.
//
// The ns-3 FQ-CoDel model uses ns-3 defaults:
// - 100ms interval
// - 5ms target
// - drop batch size of 64 packets
// - minbytes of 1500
//
// Default simulation time is 70 sec.  For single flow experiments, the flow is
// started at simulation time 5 sec; if a second flow is used, it starts
// at 15 sec.
//
// ping frequency is set at 100ms.
//
// A command-line option to enable a step-threshold CE threshold
// from the CoDel queue model is provided.
//
// Measure:
//  - ping RTT
//  - TCP RTT estimate
//  - TCP throughput
//
// IPv4 addressing
// ----------------------------
// pingServer       10.1.1.2 (ping source)
// firstServer      10.1.2.2 (data sender)
// secondServer     10.1.3.2 (data sender)
// pingClient       192.168.1.2
// firstClient      192.168.2.2
// secondClient     192.168.3.2
//
// Program Options:
// ---------------
//    --firstTcpType:   first TCP type alias or ns-3 TypeId [cubic]
//    --secondTcpType:  second TCP type alias or ns-3 TypeId []
//    --queueType:      bottleneck queue type (fq, codel, pie, or red) [codel]
//    --baseRtt:        base RTT [+80ms]
//    --ceThreshold:    CoDel CE threshold (for DCTCP) [+1ms]
//    --linkRate:       data rate of bottleneck link [50000000bps]
//    --stopTime:       simulation stop time [+1.16667min]
//    --firstStartTime: first TCP flow start time [+5s]
//    --secondStartTime: second TCP flow base start time [+15s]
//    --secondStartJitter: random start jitter added to the second TCP flow [+0s]
//    --firstStartJitter: random start jitter added to the first TCP flow [+0s]
//    --queueUseEcn:    use ECN on queue [false]
//    --enablePcap:     enable Pcap [false]
//    --validate:       validation case to run []
//
// validation cases (and syntax of how to run):
// ------------
// Case 'dctcp-10ms':  DCTCP single flow, 10ms base RTT, 50 Mbps link, ECN enabled, CoDel:
//     ./ns3 run 'tcp-aqm-benchmark --firstTcpType=dctcp --linkRate=50Mbps --baseRtt=10ms
//     --queueUseEcn=1 --stopTime=15s --validate=1 --validation=dctcp-10ms'
//    - Throughput between 48 Mbps and 49 Mbps for time greater than 5.6s
//    - DCTCP alpha below 0.1 for time greater than 5.4s
//    - DCTCP alpha between 0.06 and 0.085 for time greater than 7s
//
// Case 'dctcp-80ms': DCTCP single flow, 80ms base RTT, 50 Mbps link, ECN enabled, CoDel:
//     ./ns3 run 'tcp-aqm-benchmark --firstTcpType=dctcp --linkRate=50Mbps --baseRtt=80ms
//     --queueUseEcn=1 --stopTime=40s --validate=1 --validation=dctcp-80ms'
//    - Throughput less than 20 Mbps for time less than 14s
//    - Throughput less than 48 Mbps for time less than 30s
//    - Throughput between 47.5 Mbps and 48.5 for time greater than 32s
//    - DCTCP alpha above 0.1 for time less than 7.5
//    - DCTCP alpha below 0.01 for time greater than 11 and less than 30
//    - DCTCP alpha between 0.015 and 0.025 for time greater than 34
//
// Case 'cubic-50ms-no-ecn': CUBIC single flow, 50ms base RTT, 50 Mbps link, ECN disabled, CoDel:
//     ./ns3 run 'tcp-aqm-benchmark --firstTcpType=cubic --linkRate=50Mbps --baseRtt=50ms
//     --queueUseEcn=0 --stopTime=20s --validate=1 --validation=cubic-50ms-no-ecn'
//    - Maximum value of cwnd is 511 segments at 5.4593 seconds
//    - cwnd decreases to 173 segments at 5.80304 seconds
//    - cwnd reaches another local maxima around 14.2815 seconds of 236 segments
//    - cwnd reaches a second maximum around 18.048 seconds of 234 segments
//
// Case 'cubic-50ms-ecn': CUBIC single flow, 50ms base RTT, 50 Mbps link, ECN enabled, CoDel:
//     ./ns3 run 'tcp-aqm-benchmark --firstTcpType=cubic --linkRate=50Mbps --baseRtt=50ms
//     --queueUseEcn=0 --stopTime=20s --validate=1 --validation=cubic-50ms-no-ecn'
//    - Maximum value of cwnd is 511 segments at 5.4593 seconds
//    - cwnd decreases to 173 segments at 5.7939 seconds
//    - cwnd reaches another local maxima around 14.3477 seconds of 236 segments
//    - cwnd reaches a second maximum around 18.064 seconds of 234 segments

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-apps-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/tcp-aqm-experiment-config.h"
#include "ns3/traffic-control-module.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpAqmBenchmark");

// These variables are declared outside of main() so that they can
// be used in trace sinks.
uint32_t g_firstBytesReceived = 0;  //!< First received packet size.
uint32_t g_secondBytesReceived = 0; //!< Second received packet size.
uint32_t g_marksObserved = 0;       //!< Number of marked packets observed.
uint32_t g_dropsObserved = 0;       //!< Number of dropped packets observed.
std::string g_validate = "";        //!< Empty string disables validation.
bool g_validationFailed = false;    //!< True if validation failed.

/**
 * Convert a TCP congestion-control alias or TypeId name to a full ns-3 TypeId name.
 *
 * @param tcpType User-provided TCP type.
 * @return ns-3 TypeId name.
 */
std::string
NormalizeTcpTypeName(const std::string& tcpType)
{
    std::string lower = tcpType;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return std::tolower(c);
    });

    if (lower == "reno" || lower == "linuxreno")
    {
        return "ns3::TcpLinuxReno";
    }
    if (lower == "newreno")
    {
        return "ns3::TcpNewReno";
    }
    if (lower == "cubic")
    {
        return "ns3::TcpCubic";
    }
    if (lower == "dctcp")
    {
        return "ns3::TcpDctcp";
    }
    if (lower == "bbr")
    {
        return "ns3::TcpBbr";
    }
    if (lower == "bic")
    {
        return "ns3::TcpBic";
    }
    if (lower == "yeah")
    {
        return "ns3::TcpYeah";
    }
    if (lower == "vegas")
    {
        return "ns3::TcpVegas";
    }
    if (lower == "veno")
    {
        return "ns3::TcpVeno";
    }
    if (lower == "westwoodplus")
    {
        return "ns3::TcpWestwoodPlus";
    }
    if (lower == "illinois")
    {
        return "ns3::TcpIllinois";
    }
    if (lower == "highspeed")
    {
        return "ns3::TcpHighSpeed";
    }
    if (lower == "hybla")
    {
        return "ns3::TcpHybla";
    }
    if (lower == "htcp")
    {
        return "ns3::TcpHtcp";
    }
    if (lower == "scalable")
    {
        return "ns3::TcpScalable";
    }
    if (lower == "ledbat")
    {
        return "ns3::TcpLedbat";
    }
    if (lower == "lp")
    {
        return "ns3::TcpLp";
    }
    if (tcpType.rfind("ns3::", 0) == 0)
    {
        return tcpType;
    }
    if (tcpType.rfind("Tcp", 0) == 0)
    {
        return "ns3::" + tcpType;
    }
    return tcpType;
}

/**
 * Look up and validate a TCP congestion-control TypeId.
 *
 * @param tcpType User-provided TCP type.
 * @return TCP congestion-control TypeId.
 */
TypeId
ResolveTcpTypeId(const std::string& tcpType)
{
    const std::string typeName = NormalizeTcpTypeName(tcpType);
    TypeId tcpTid;
    NS_ABORT_MSG_UNLESS(TypeId::LookupByNameFailSafe(typeName, &tcpTid),
                        "TypeId " << typeName << " not found");
    NS_ABORT_MSG_UNLESS(tcpTid.IsChildOf(TcpCongestionOps::GetTypeId()),
                        "TypeId " << typeName << " is not a TcpCongestionOps");
    return tcpTid;
}

/**
 * Resolve a queue-disc alias to an ns-3 TypeId.
 *
 * @param queueType Queue-disc alias.
 * @return Queue-disc TypeId.
 */
TypeId
ResolveQueueDiscTypeId(const std::string& queueType)
{
    if (queueType == "fq")
    {
        return FqCoDelQueueDisc::GetTypeId();
    }
    if (queueType == "codel")
    {
        return CoDelQueueDisc::GetTypeId();
    }
    if (queueType == "pie")
    {
        return PieQueueDisc::GetTypeId();
    }
    if (queueType == "red")
    {
        return RedQueueDisc::GetTypeId();
    }
    NS_FATAL_ERROR("Fatal error: queueType unsupported: " << queueType);
}

/**
 * Read a command-line value before ns-3 CommandLine parsing.
 *
 * This is used only for --configFile so that file values can establish defaults
 * before regular CommandLine parsing applies explicit command-line overrides.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @param name Option name without leading dashes.
 * @return Option value or empty string.
 */
std::string
GetEarlyCommandLineValue(int argc, char* argv[], const std::string& name)
{
    const std::string prefix = "--" + name + "=";
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument.rfind(prefix, 0) == 0)
        {
            return argument.substr(prefix.size());
        }
        if (argument == "--" + name && index + 1 < argc)
        {
            return argv[index + 1];
        }
    }
    return "";
}

/**
 * Trace first congestion window.
 *
 * @param ofStream Output filestream.
 * @param oldCwnd Old value.
 * @param newCwnd new value.
 */
void
TraceFirstCwnd(std::ofstream* ofStream, uint32_t oldCwnd, uint32_t newCwnd)
{
    // TCP segment size is configured below to be 1448 bytes
    // so that we can report cwnd in units of segments
    if (g_validate.empty())
    {
        *ofStream << Simulator::Now().GetSeconds() << " " << static_cast<double>(newCwnd) / 1448
                  << std::endl;
    }
    // Validation checks; both the ECN enabled and disabled cases are similar
    if (g_validate == "cubic-50ms-no-ecn" || g_validate == "cubic-50ms-ecn")
    {
        double now = Simulator::Now().GetSeconds();
        double cwnd = static_cast<double>(newCwnd) / 1448;
        if ((now > 5.43) && (now < 5.465) && (cwnd < 500))
        {
            NS_LOG_WARN("now " << Now().As(Time::S) << " cwnd " << cwnd << " (expected >= 500)");
            g_validationFailed = true;
        }
        else if ((now > 5.795) && (now < 6) && (cwnd > 190))
        {
            NS_LOG_WARN("now " << Now().As(Time::S) << " cwnd " << cwnd << " (expected <= 190)");
            g_validationFailed = true;
        }
        else if ((now > 14) && (now < 14.197) && (cwnd < 224))
        {
            NS_LOG_WARN("now " << Now().As(Time::S) << " cwnd " << cwnd << " (expected >= 224)");
            g_validationFailed = true;
        }
        else if ((now > 17) && (now < 18.026) && (cwnd < 212))
        {
            NS_LOG_WARN("now " << Now().As(Time::S) << " cwnd " << cwnd << " (expected >= 212)");
            g_validationFailed = true;
        }
    }
}

/**
 * Trace first TcpDctcp.
 *
 * @param ofStream Output filestream.
 * @param bytesMarked Bytes marked.
 * @param bytesAcked Bytes ACKed.
 * @param alpha Alpha.
 */
void
TraceFirstDctcp(std::ofstream* ofStream, uint32_t bytesMarked, uint32_t bytesAcked, double alpha)
{
    if (g_validate.empty())
    {
        *ofStream << Simulator::Now().GetSeconds() << " " << alpha << std::endl;
    }
    // Validation checks
    if (g_validate == "dctcp-80ms")
    {
        double now = Simulator::Now().GetSeconds();
        if ((now < 7.5) && (alpha < 0.1))
        {
            NS_LOG_WARN("now " << Now().As(Time::S) << " alpha " << alpha << " (expected >= 0.1)");
            g_validationFailed = true;
        }
        else if ((now > 11) && (now < 30) && (alpha > 0.01))
        {
            NS_LOG_WARN("now " << Now().As(Time::S) << " alpha " << alpha << " (expected <= 0.01)");
            g_validationFailed = true;
        }
        else if ((now > 34) && (alpha < 0.015) && (alpha > 0.025))
        {
            NS_LOG_WARN("now " << Now().As(Time::S) << " alpha " << alpha
                               << " (expected 0.015 <= alpha <= 0.025)");
            g_validationFailed = true;
        }
    }
    else if (g_validate == "dctcp-10ms")
    {
        double now = Simulator::Now().GetSeconds();
        if ((now > 5.6) && (alpha > 0.1))
        {
            NS_LOG_WARN("now " << Now().As(Time::S) << " alpha " << alpha << " (expected <= 0.1)");
            g_validationFailed = true;
        }
        // Validation range was originally written backwards in the upstream
        // tcp-validation example; the intended window is 0.049 <= alpha <= 0.09.
        if ((now > 7) && ((alpha < 0.049) || (alpha > 0.09)))
        {
            NS_LOG_WARN("now " << Now().As(Time::S) << " alpha " << alpha
                               << " (expected 0.049 <= alpha <= 0.09)");
            g_validationFailed = true;
        }
    }
}

/**
 * Trace first RTT.
 *
 * @param ofStream Output filestream.
 * @param oldRtt Old value.
 * @param newRtt New value.
 */
void
TraceFirstRtt(std::ofstream* ofStream, Time oldRtt, Time newRtt)
{
    if (g_validate.empty())
    {
        *ofStream << Simulator::Now().GetSeconds() << " " << newRtt.GetSeconds() * 1000
                  << std::endl;
    }
}

/**
 * Trace second congestion window.
 *
 * @param ofStream Output filestream.
 * @param oldCwnd Old value.
 * @param newCwnd new value.
 */
void
TraceSecondCwnd(std::ofstream* ofStream, uint32_t oldCwnd, uint32_t newCwnd)
{
    // TCP segment size is configured below to be 1448 bytes
    // so that we can report cwnd in units of segments
    if (g_validate.empty())
    {
        *ofStream << Simulator::Now().GetSeconds() << " " << static_cast<double>(newCwnd) / 1448
                  << std::endl;
    }
}

/**
 * Trace second RTT.
 *
 * @param ofStream Output filestream.
 * @param oldRtt Old value.
 * @param newRtt New value.
 */
void
TraceSecondRtt(std::ofstream* ofStream, Time oldRtt, Time newRtt)
{
    if (g_validate.empty())
    {
        *ofStream << Simulator::Now().GetSeconds() << " " << newRtt.GetSeconds() * 1000
                  << std::endl;
    }
}

/**
 * Trace second TcpDctcp.
 *
 * @param ofStream Output filestream.
 * @param bytesMarked Bytes marked.
 * @param bytesAcked Bytes ACKed.
 * @param alpha Alpha.
 */
void
TraceSecondDctcp(std::ofstream* ofStream, uint32_t bytesMarked, uint32_t bytesAcked, double alpha)
{
    if (g_validate.empty())
    {
        *ofStream << Simulator::Now().GetSeconds() << " " << alpha << std::endl;
    }
}

/**
 * Trace ping RTT.
 *
 * @param ofStream Output filestream.
 * @param rtt RTT value.
 */
void
TracePingRtt(std::ofstream* ofStream, uint16_t, Time rtt)
{
    if (g_validate.empty())
    {
        *ofStream << Simulator::Now().GetSeconds() << " " << rtt.GetSeconds() * 1000 << std::endl;
    }
}

/**
 * Trace first Rx.
 *
 * @param packet The packet.
 * @param address The sender address.
 */
void
TraceFirstRx(Ptr<const Packet> packet, const Address& address)
{
    g_firstBytesReceived += packet->GetSize();
}

/**
 * Trace second Rx.
 *
 * @param packet The packet.
 * @param address The sender address.
 */
void
TraceSecondRx(Ptr<const Packet> packet, const Address& address)
{
    g_secondBytesReceived += packet->GetSize();
}

/**
 * Trace queue drop.
 *
 * @param ofStream Output filestream.
 * @param item The dropped QueueDiscItem.
 */
void
TraceQueueDrop(std::ofstream* ofStream, Ptr<const QueueDiscItem> item)
{
    if (g_validate.empty())
    {
        *ofStream << Simulator::Now().GetSeconds() << " " << std::hex << item->Hash() << std::endl;
    }
    g_dropsObserved++;
}

/**
 * Trace queue marks.
 *
 * @param ofStream Output filestream.
 * @param item The marked QueueDiscItem.
 * @param reason The reason.
 */
void
TraceQueueMark(std::ofstream* ofStream, Ptr<const QueueDiscItem> item, const char* reason)
{
    if (g_validate.empty())
    {
        *ofStream << Simulator::Now().GetSeconds() << " " << std::hex << item->Hash() << std::endl;
    }
    g_marksObserved++;
}

/**
 * Trace queue length.
 *
 * @param ofStream Output filestream.
 * @param queueLinkRate Queue link rate.
 * @param oldVal Old value.
 * @param newVal New value.
 */
void
TraceQueueLength(std::ofstream* ofStream, DataRate queueLinkRate, uint32_t oldVal, uint32_t newVal)
{
    // output in units of ms
    if (g_validate.empty())
    {
        *ofStream << Simulator::Now().GetSeconds() << " " << std::fixed
                  << static_cast<double>(newVal * 8) / (queueLinkRate.GetBitRate() / 1000)
                  << std::endl;
    }
}

/**
 * Trace marks frequency.
 *
 * @param ofStream Output filestream.
 * @param marksSamplingInterval The mark sampling interval.
 */
void
TraceMarksFrequency(std::ofstream* ofStream, Time marksSamplingInterval)
{
    if (g_validate.empty())
    {
        *ofStream << Simulator::Now().GetSeconds() << " " << g_marksObserved << std::endl;
    }
    g_marksObserved = 0;
    Simulator::Schedule(marksSamplingInterval,
                        &TraceMarksFrequency,
                        ofStream,
                        marksSamplingInterval);
}

/**
 * Trace the first throughput.
 *
 * @param ofStream Output filestream.
 * @param throughputInterval The throughput interval.
 */
void
TraceFirstThroughput(std::ofstream* ofStream, Time throughputInterval)
{
    double throughput = g_firstBytesReceived * 8 / throughputInterval.GetSeconds() / 1e6;
    if (g_validate.empty())
    {
        *ofStream << Simulator::Now().GetSeconds() << " " << throughput << std::endl;
    }
    g_firstBytesReceived = 0;
    Simulator::Schedule(throughputInterval, &TraceFirstThroughput, ofStream, throughputInterval);
    if (g_validate == "dctcp-80ms")
    {
        double now = Simulator::Now().GetSeconds();
        if ((now < 14) && (throughput > 20))
        {
            NS_LOG_WARN("now " << Now().As(Time::S) << " throughput " << throughput
                               << " (expected <= 20)");
            g_validationFailed = true;
        }
        if ((now < 30) && (throughput > 48))
        {
            NS_LOG_WARN("now " << Now().As(Time::S) << " throughput " << throughput
                               << " (expected <= 48)");
            g_validationFailed = true;
        }
        if ((now > 32) && ((throughput < 47.5) || (throughput > 48.5)))
        {
            NS_LOG_WARN("now " << Now().As(Time::S) << " throughput " << throughput
                               << " (expected 47.5 <= throughput <= 48.5)");
            g_validationFailed = true;
        }
    }
    else if (g_validate == "dctcp-10ms")
    {
        double now = Simulator::Now().GetSeconds();
        if ((now > 5.6) && ((throughput < 48) || (throughput > 49)))
        {
            NS_LOG_WARN("now " << Now().As(Time::S) << " throughput " << throughput
                               << " (expected 48 <= throughput <= 49)");
            g_validationFailed = true;
        }
    }
}

/**
 * Trace the second throughput.
 *
 * @param ofStream Output filestream.
 * @param throughputInterval The throughput interval.
 */
void
TraceSecondThroughput(std::ofstream* ofStream, Time throughputInterval)
{
    if (g_validate.empty())
    {
        *ofStream << Simulator::Now().GetSeconds() << " "
                  << g_secondBytesReceived * 8 / throughputInterval.GetSeconds() / 1e6 << std::endl;
    }
    g_secondBytesReceived = 0;
    Simulator::Schedule(throughputInterval, &TraceSecondThroughput, ofStream, throughputInterval);
}

/**
 * Schedule trace connection.
 *
 * @param ofStream Output filestream.
 */
void
ScheduleFirstTcpCwndTraceConnection(std::ofstream* ofStream)
{
    Config::ConnectWithoutContextFailSafe(
        "/NodeList/1/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow",
        MakeBoundCallback(&TraceFirstCwnd, ofStream));
}

/**
 * Schedule trace connection.
 *
 * @param ofStream Output filestream.
 */
void
ScheduleFirstTcpRttTraceConnection(std::ofstream* ofStream)
{
    Config::ConnectWithoutContextFailSafe("/NodeList/1/$ns3::TcpL4Protocol/SocketList/0/RTT",
                                          MakeBoundCallback(&TraceFirstRtt, ofStream));
}

/**
 * Schedule trace connection.
 *
 * @param ofStream Output filestream.
 */
void
ScheduleFirstDctcpTraceConnection(std::ofstream* ofStream)
{
    Config::ConnectWithoutContextFailSafe("/NodeList/1/$ns3::TcpL4Protocol/SocketList/0/"
                                          "CongestionOps/$ns3::TcpDctcp/CongestionEstimate",
                                          MakeBoundCallback(&TraceFirstDctcp, ofStream));
}

/**
 * Schedule trace connection.
 *
 * @param ofStream Output filestream.
 */
void
ScheduleSecondDctcpTraceConnection(std::ofstream* ofStream)
{
    Config::ConnectWithoutContextFailSafe("/NodeList/2/$ns3::TcpL4Protocol/SocketList/0/"
                                          "CongestionOps/$ns3::TcpDctcp/CongestionEstimate",
                                          MakeBoundCallback(&TraceSecondDctcp, ofStream));
}

/**
 * Schedule trace connection.
 */
void
ScheduleFirstPacketSinkConnection()
{
    Config::ConnectWithoutContextFailSafe("/NodeList/6/ApplicationList/*/$ns3::PacketSink/Rx",
                                          MakeCallback(&TraceFirstRx));
}

/**
 * Schedule trace connection.
 *
 * @param ofStream Output filestream.
 */
void
ScheduleSecondTcpCwndTraceConnection(std::ofstream* ofStream)
{
    Config::ConnectWithoutContext("/NodeList/2/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow",
                                  MakeBoundCallback(&TraceSecondCwnd, ofStream));
}

/**
 * Schedule trace connection.
 *
 * @param ofStream Output filestream.
 */
void
ScheduleSecondTcpRttTraceConnection(std::ofstream* ofStream)
{
    Config::ConnectWithoutContext("/NodeList/2/$ns3::TcpL4Protocol/SocketList/0/RTT",
                                  MakeBoundCallback(&TraceSecondRtt, ofStream));
}

/**
 * Schedule trace connection.
 */
void
ScheduleSecondPacketSinkConnection()
{
    Config::ConnectWithoutContext("/NodeList/7/ApplicationList/*/$ns3::PacketSink/Rx",
                                  MakeCallback(&TraceSecondRx));
}

int
main(int argc, char* argv[])
{
    ////////////////////////////////////////////////////////////
    // variables not configured at command line               //
    ////////////////////////////////////////////////////////////
    uint32_t pingSize = 100; // bytes
    bool enableSecondTcp = false;
    bool enableLogging = false;
    Time pingInterval = MilliSeconds(100);
    Time marksSamplingInterval = MilliSeconds(100);
    Time throughputSamplingInterval = MilliSeconds(200);
    std::string pingTraceFile = "tcp-aqm-benchmark-ping.dat";
    std::string firstTcpRttTraceFile = "tcp-aqm-benchmark-first-tcp-rtt.dat";
    std::string firstTcpCwndTraceFile = "tcp-aqm-benchmark-first-tcp-cwnd.dat";
    std::string firstDctcpTraceFile = "tcp-aqm-benchmark-first-dctcp-alpha.dat";
    std::string firstTcpThroughputTraceFile = "tcp-aqm-benchmark-first-tcp-throughput.dat";
    std::string secondTcpRttTraceFile = "tcp-aqm-benchmark-second-tcp-rtt.dat";
    std::string secondTcpCwndTraceFile = "tcp-aqm-benchmark-second-tcp-cwnd.dat";
    std::string secondTcpThroughputTraceFile = "tcp-aqm-benchmark-second-tcp-throughput.dat";
    std::string secondDctcpTraceFile = "tcp-aqm-benchmark-second-dctcp-alpha.dat";
    std::string queueMarkTraceFile = "tcp-aqm-benchmark-queue-mark.dat";
    std::string queueDropTraceFile = "tcp-aqm-benchmark-queue-drop.dat";
    std::string queueMarksFrequencyTraceFile = "tcp-aqm-benchmark-queue-marks-frequency.dat";
    std::string queueLengthTraceFile = "tcp-aqm-benchmark-queue-length.dat";

    ////////////////////////////////////////////////////////////
    // variables configured at command line                   //
    ////////////////////////////////////////////////////////////
    std::string firstTcpType = "cubic";
    std::string secondTcpType = "";
    std::string queueType = "codel";
    std::string configFile = "";
    Time stopTime = Seconds(70);
    Time firstStartTime = Seconds(5);
    Time secondStartTime = Seconds(15);
    Time secondStartJitter = Seconds(0);
    Time firstStartJitter = Seconds(0);
    Time baseRtt = MilliSeconds(80);
    DataRate linkRate("50Mbps");
    bool queueUseEcn = false;
    Time ceThreshold = MilliSeconds(1);
    bool enablePcap = false;
    uint32_t rngRun = 1;
    TcpAqmTopologyConfig topology;

    configFile = GetEarlyCommandLineValue(argc, argv, "configFile");
    const bool loadedConfigFile = !configFile.empty();
    if (!configFile.empty())
    {
        const auto fileConfig = TcpAqmExperimentConfig::LoadFromFile(configFile);
        firstTcpType = TcpAqmTcpTypeToString(fileConfig.firstTcpType);
        secondTcpType = fileConfig.secondTcpType.has_value()
                            ? TcpAqmTcpTypeToString(*fileConfig.secondTcpType)
                            : "";
        queueType = TcpAqmQueueDiscTypeToString(fileConfig.queueType);
        stopTime = fileConfig.stopTime;
        firstStartTime = fileConfig.firstStartTime;
        secondStartTime = fileConfig.secondStartTime;
        secondStartJitter = fileConfig.secondStartJitter;
        firstStartJitter = fileConfig.firstStartJitter;
        baseRtt = fileConfig.baseRtt;
        linkRate = fileConfig.linkRate;
        queueUseEcn = fileConfig.queueUseEcn;
        ceThreshold = fileConfig.ceThreshold;
        enablePcap = fileConfig.enablePcap;
        rngRun = fileConfig.rngRun;
        topology = fileConfig.topology;
    }

    ////////////////////////////////////////////////////////////
    // Override ns-3 defaults                                 //
    ////////////////////////////////////////////////////////////
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1448));
    // Increase default buffer sizes to improve throughput over long delay paths
    // Config::SetDefault ("ns3::TcpSocket::SndBufSize",UintegerValue (8192000));
    // Config::SetDefault ("ns3::TcpSocket::RcvBufSize",UintegerValue (8192000));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(32768000));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(32768000));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));
    Config::SetDefault("ns3::TcpL4Protocol::RecoveryType",
                       TypeIdValue(TcpPrrRecovery::GetTypeId()));
    // Validation criteria were written for TCP Cubic without Reno-friendly behavior, so disable it
    // for these tests
    Config::SetDefault("ns3::TcpCubic::TcpFriendliness", BooleanValue(false));

    ////////////////////////////////////////////////////////////
    // command-line argument parsing                          //
    ////////////////////////////////////////////////////////////
    CommandLine cmd(__FILE__);
    cmd.AddValue("configFile", "YAML or JSON benchmark configuration file", configFile);
    cmd.AddValue("firstTcpType",
                 "first TCP type alias or ns-3 TypeId (e.g., cubic, dctcp, bbr, ns3::TcpBbr)",
                 firstTcpType);
    cmd.AddValue("secondTcpType",
                 "second TCP type alias or ns-3 TypeId (e.g., reno, dctcp, bbr, ns3::TcpBbr)",
                 secondTcpType);
    cmd.AddValue("queueType", "bottleneck queue type (fq, codel, pie, or red)", queueType);
    cmd.AddValue("baseRtt", "base RTT", baseRtt);
    cmd.AddValue("ceThreshold", "CoDel CE threshold (for DCTCP)", ceThreshold);
    cmd.AddValue("linkRate", "data rate of bottleneck link", linkRate);
    cmd.AddValue("stopTime", "simulation stop time", stopTime);
    cmd.AddValue("firstStartTime", "first TCP flow start time", firstStartTime);
    cmd.AddValue("secondStartTime", "second TCP flow base start time", secondStartTime);
    cmd.AddValue("secondStartJitter",
                 "random start jitter added to the second TCP flow",
                 secondStartJitter);
    cmd.AddValue("firstStartJitter",
                 "random start jitter added to the first TCP flow; gives single-flow seeds real variance",
                 firstStartJitter);
    cmd.AddValue("queueUseEcn", "use ECN on queue", queueUseEcn);
    cmd.AddValue("enablePcap", "enable Pcap", enablePcap);
    cmd.AddValue("rngRun", "random run value used for ns-3 RNG streams", rngRun);
    cmd.AddValue("validate", "validation case to run", g_validate);
    cmd.Parse(argc, argv);

    if (!loadedConfigFile)
    {
        topology.bottlenecks.at(0).rate = linkRate;
    }
    topology.FillDerivedDelays(baseRtt);
    topology.Validate();
    RngSeedManager::SetRun(rngRun);

    TypeId firstTcpTypeId = ResolveTcpTypeId(firstTcpType);
    const bool firstTcpIsDctcp = firstTcpTypeId == TcpDctcp::GetTypeId();
    TypeId secondTcpTypeId;
    bool secondTcpIsDctcp = false;
    if (secondTcpType.empty())
    {
        enableSecondTcp = false;
        NS_LOG_DEBUG("No second TCP selected");
    }
    else
    {
        enableSecondTcp = true;
        secondTcpTypeId = ResolveTcpTypeId(secondTcpType);
        secondTcpIsDctcp = secondTcpTypeId == TcpDctcp::GetTypeId();
    }

    Time firstFlowStart = firstStartTime;
    Time secondFlowStart = secondStartTime;
    // RngSeedManager::SetRun was applied above, so any UniformRandomVariable
    // sampled here is fully driven by --rngRun. Sampling firstStartJitter even
    // for a single-flow run lets reviewers produce meaningful confidence
    // intervals without changing the topology or queue settings.
    if (firstStartJitter > Seconds(0))
    {
        Ptr<UniformRandomVariable> jitter = CreateObject<UniformRandomVariable>();
        firstFlowStart += Seconds(jitter->GetValue(0.0, firstStartJitter.GetSeconds()));
    }
    if (enableSecondTcp && secondStartJitter > Seconds(0))
    {
        Ptr<UniformRandomVariable> jitter = CreateObject<UniformRandomVariable>();
        secondFlowStart += Seconds(jitter->GetValue(0.0, secondStartJitter.GetSeconds()));
    }
    NS_ABORT_MSG_UNLESS(firstFlowStart.IsPositive(), "firstStartTime must not be negative");
    NS_ABORT_MSG_UNLESS(firstFlowStart < stopTime - Seconds(1),
                        "firstStartTime must leave at least one second before stopTime");
    if (enableSecondTcp)
    {
        NS_ABORT_MSG_UNLESS(secondFlowStart.IsPositive(), "secondStartTime must not be negative");
        NS_ABORT_MSG_UNLESS(secondFlowStart < stopTime - Seconds(1),
                            "second TCP start time must leave at least one second before stopTime");
    }

    // If validation is selected, perform some configuration checks
    if (!g_validate.empty())
    {
        NS_ABORT_MSG_UNLESS(g_validate == "dctcp-10ms" || g_validate == "dctcp-80ms" ||
                                g_validate == "cubic-50ms-no-ecn" || g_validate == "cubic-50ms-ecn",
                            "Unknown test");
        if (g_validate == "dctcp-10ms" || g_validate == "dctcp-80ms")
        {
            NS_ABORT_MSG_UNLESS(firstTcpIsDctcp, "Incorrect TCP");
            NS_ABORT_MSG_UNLESS(secondTcpType.empty(), "Incorrect TCP");
            NS_ABORT_MSG_UNLESS(linkRate == DataRate("50Mbps"), "Incorrect data rate");
            NS_ABORT_MSG_UNLESS(queueUseEcn == true, "Incorrect ECN configuration");
            NS_ABORT_MSG_UNLESS(stopTime >= Seconds(15), "Incorrect stopTime");
            if (g_validate == "dctcp-10ms")
            {
                NS_ABORT_MSG_UNLESS(baseRtt == MilliSeconds(10), "Incorrect RTT");
            }
            else if (g_validate == "dctcp-80ms")
            {
                NS_ABORT_MSG_UNLESS(baseRtt == MilliSeconds(80), "Incorrect RTT");
            }
        }
        else if (g_validate == "cubic-50ms-no-ecn" || g_validate == "cubic-50ms-ecn")
        {
            NS_ABORT_MSG_UNLESS(firstTcpTypeId == TcpCubic::GetTypeId(), "Incorrect TCP");
            NS_ABORT_MSG_UNLESS(secondTcpType.empty(), "Incorrect TCP");
            NS_ABORT_MSG_UNLESS(baseRtt == MilliSeconds(50), "Incorrect RTT");
            NS_ABORT_MSG_UNLESS(linkRate == DataRate("50Mbps"), "Incorrect data rate");
            NS_ABORT_MSG_UNLESS(stopTime >= Seconds(20), "Incorrect stopTime");
            if (g_validate == "cubic-50ms-no-ecn")
            {
                NS_ABORT_MSG_UNLESS(queueUseEcn == false, "Incorrect ECN configuration");
            }
            else if (g_validate == "cubic-50ms-ecn")
            {
                NS_ABORT_MSG_UNLESS(queueUseEcn == true, "Incorrect ECN configuration");
            }
        }
    }

    if (enableLogging)
    {
        LogComponentEnable(
            "TcpSocketBase",
            (LogLevel)(LOG_PREFIX_FUNC | LOG_PREFIX_NODE | LOG_PREFIX_TIME | LOG_LEVEL_ALL));
        LogComponentEnable(
            "TcpDctcp",
            (LogLevel)(LOG_PREFIX_FUNC | LOG_PREFIX_NODE | LOG_PREFIX_TIME | LOG_LEVEL_ALL));
    }

    if (firstTcpIsDctcp || secondTcpIsDctcp)
    {
        Config::SetDefault("ns3::CoDelQueueDisc::CeThreshold", TimeValue(ceThreshold));
        Config::SetDefault("ns3::FqCoDelQueueDisc::CeThreshold", TimeValue(ceThreshold));
        if (!queueUseEcn)
        {
            std::cout << "Warning: using DCTCP with queue ECN disabled" << std::endl;
        }
    }
    TypeId queueTypeId = ResolveQueueDiscTypeId(queueType);

    if (queueUseEcn)
    {
        Config::SetDefault("ns3::CoDelQueueDisc::UseEcn", BooleanValue(true));
        Config::SetDefault("ns3::FqCoDelQueueDisc::UseEcn", BooleanValue(true));
        Config::SetDefault("ns3::PieQueueDisc::UseEcn", BooleanValue(true));
        Config::SetDefault("ns3::RedQueueDisc::UseEcn", BooleanValue(true));
    }
    // Enable TCP to use ECN regardless
    Config::SetDefault("ns3::TcpSocketBase::UseEcn", StringValue("On"));

    // Report on configuration
    if (enableSecondTcp)
    {
        NS_LOG_DEBUG("first TCP: " << firstTcpTypeId.GetName()
                                   << "; second TCP: " << secondTcpTypeId.GetName()
                                   << "; queue: " << queueTypeId.GetName()
                                   << "; ceThreshold: " << ceThreshold.GetSeconds() * 1000 << "ms");
    }
    else
    {
        NS_LOG_DEBUG("first TCP: " << firstTcpTypeId.GetName()
                                   << "; queue: " << queueTypeId.GetName()
                                   << "; ceThreshold: " << ceThreshold.GetSeconds() * 1000 << "ms");
    }

    // Write traces only if we are not in validation mode (g_validate == "")
    std::ofstream pingOfStream;
    std::ofstream firstTcpRttOfStream;
    std::ofstream firstTcpCwndOfStream;
    std::ofstream firstTcpThroughputOfStream;
    std::ofstream firstTcpDctcpOfStream;
    std::ofstream secondTcpRttOfStream;
    std::ofstream secondTcpCwndOfStream;
    std::ofstream secondTcpThroughputOfStream;
    std::ofstream secondTcpDctcpOfStream;
    std::ofstream queueDropOfStream;
    std::ofstream queueMarkOfStream;
    std::ofstream queueMarksFrequencyOfStream;
    std::ofstream queueLengthOfStream;
    if (g_validate.empty())
    {
        pingOfStream.open(pingTraceFile, std::ofstream::out);
        firstTcpRttOfStream.open(firstTcpRttTraceFile, std::ofstream::out);
        firstTcpCwndOfStream.open(firstTcpCwndTraceFile, std::ofstream::out);
        firstTcpThroughputOfStream.open(firstTcpThroughputTraceFile, std::ofstream::out);
        if (firstTcpIsDctcp)
        {
            firstTcpDctcpOfStream.open(firstDctcpTraceFile, std::ofstream::out);
        }
        if (enableSecondTcp)
        {
            secondTcpRttOfStream.open(secondTcpRttTraceFile, std::ofstream::out);
            secondTcpCwndOfStream.open(secondTcpCwndTraceFile, std::ofstream::out);
            secondTcpThroughputOfStream.open(secondTcpThroughputTraceFile, std::ofstream::out);
            if (secondTcpIsDctcp)
            {
                secondTcpDctcpOfStream.open(secondDctcpTraceFile, std::ofstream::out);
            }
        }
        queueDropOfStream.open(queueDropTraceFile, std::ofstream::out);
        queueMarkOfStream.open(queueMarkTraceFile, std::ofstream::out);
        queueMarksFrequencyOfStream.open(queueMarksFrequencyTraceFile, std::ofstream::out);
        queueLengthOfStream.open(queueLengthTraceFile, std::ofstream::out);
    }

    ////////////////////////////////////////////////////////////
    // scenario setup                                         //
    ////////////////////////////////////////////////////////////
    Ptr<Node> pingServer = CreateObject<Node>();
    Ptr<Node> firstServer = CreateObject<Node>();
    Ptr<Node> secondServer = CreateObject<Node>();
    Ptr<Node> wanRouter = CreateObject<Node>();
    Ptr<Node> lanRouter = CreateObject<Node>();
    Ptr<Node> pingClient = CreateObject<Node>();
    Ptr<Node> firstClient = CreateObject<Node>();
    Ptr<Node> secondClient = CreateObject<Node>();
    Ptr<Node> midRouter;
    if (topology.IsTwoBottleneck())
    {
        midRouter = CreateObject<Node>();
    }

    // Device containers
    NetDeviceContainer pingServerDevices;
    NetDeviceContainer firstServerDevices;
    NetDeviceContainer secondServerDevices;
    NetDeviceContainer firstBottleneckDevices;
    NetDeviceContainer secondBottleneckDevices;
    NetDeviceContainer pingClientDevices;
    NetDeviceContainer firstClientDevices;
    NetDeviceContainer secondClientDevices;

    PointToPointHelper p2p;
    p2p.SetQueue("ns3::DropTailQueue", "MaxSize", QueueSizeValue(topology.deviceQueueSize));
    p2p.SetDeviceAttribute("DataRate", DataRateValue(topology.accessRate));
    p2p.SetChannelAttribute("Delay", TimeValue(topology.accessDelay));
    pingServerDevices = p2p.Install(wanRouter, pingServer);
    firstServerDevices = p2p.Install(wanRouter, firstServer);
    secondServerDevices = p2p.Install(wanRouter, secondServer);
    p2p.SetChannelAttribute("Delay", TimeValue(*topology.bottlenecks.at(0).delay));
    if (topology.IsTwoBottleneck())
    {
        firstBottleneckDevices = p2p.Install(wanRouter, midRouter);
        p2p.SetChannelAttribute("Delay", TimeValue(*topology.bottlenecks.at(1).delay));
        secondBottleneckDevices = p2p.Install(midRouter, lanRouter);
    }
    else
    {
        firstBottleneckDevices = p2p.Install(wanRouter, lanRouter);
    }
    p2p.SetQueue("ns3::DropTailQueue", "MaxSize", QueueSizeValue(topology.deviceQueueSize));
    p2p.SetChannelAttribute("Delay", TimeValue(topology.accessDelay));
    pingClientDevices = p2p.Install(lanRouter, pingClient);
    firstClientDevices = p2p.Install(lanRouter, firstClient);
    secondClientDevices = p2p.Install(lanRouter, secondClient);

    // Limit the bandwidth on downstream bottleneck interfaces.
    Ptr<PointToPointNetDevice> p =
        firstBottleneckDevices.Get(0)->GetObject<PointToPointNetDevice>();
    p->SetAttribute("DataRate", DataRateValue(topology.bottlenecks.at(0).rate));
    if (topology.IsTwoBottleneck())
    {
        Ptr<PointToPointNetDevice> p2 =
            secondBottleneckDevices.Get(0)->GetObject<PointToPointNetDevice>();
        p2->SetAttribute("DataRate", DataRateValue(topology.bottlenecks.at(1).rate));
    }

    InternetStackHelper stackHelper;
    stackHelper.Install(pingServer);
    Ptr<TcpL4Protocol> proto;
    stackHelper.Install(firstServer);
    proto = firstServer->GetObject<TcpL4Protocol>();
    proto->SetAttribute("SocketType", TypeIdValue(firstTcpTypeId));
    stackHelper.Install(secondServer);
    stackHelper.Install(wanRouter);
    if (topology.IsTwoBottleneck())
    {
        stackHelper.Install(midRouter);
    }
    stackHelper.Install(lanRouter);
    stackHelper.Install(pingClient);

    stackHelper.Install(firstClient);
    // Set the per-node TCP type here
    proto = firstClient->GetObject<TcpL4Protocol>();
    proto->SetAttribute("SocketType", TypeIdValue(firstTcpTypeId));
    stackHelper.Install(secondClient);

    if (enableSecondTcp)
    {
        proto = secondClient->GetObject<TcpL4Protocol>();
        proto->SetAttribute("SocketType", TypeIdValue(secondTcpTypeId));
        proto = secondServer->GetObject<TcpL4Protocol>();
        proto->SetAttribute("SocketType", TypeIdValue(secondTcpTypeId));
    }

    // InternetStackHelper will install a base TrafficControlLayer on the node,
    // but the Ipv4AddressHelper below will install the default FqCoDelQueueDisc
    // on all single device nodes.  The below code overrides the configuration
    // that is normally done by the Ipv4AddressHelper::Install() method by
    // instead explicitly configuring the queue discs we want on each device.
    TrafficControlHelper tchFq;
    tchFq.SetRootQueueDisc("ns3::FqCoDelQueueDisc");
    tchFq.SetQueueLimits("ns3::DynamicQueueLimits", "HoldTime", StringValue("1ms"));
    tchFq.Install(pingServerDevices);
    tchFq.Install(firstServerDevices);
    tchFq.Install(secondServerDevices);
    tchFq.Install(firstBottleneckDevices.Get(1));
    if (topology.IsTwoBottleneck())
    {
        tchFq.Install(secondBottleneckDevices.Get(1));
    }
    tchFq.Install(pingClientDevices);
    tchFq.Install(firstClientDevices);
    tchFq.Install(secondClientDevices);
    // Install queue discs for downstream bottleneck links.
    TrafficControlHelper tchFirstBottleneck;
    const std::string firstBottleneckQueue =
        topology.bottlenecks.at(0).queueType.has_value()
            ? TcpAqmQueueDiscTypeToString(*topology.bottlenecks.at(0).queueType)
            : queueType;
    tchFirstBottleneck.SetRootQueueDisc(ResolveQueueDiscTypeId(firstBottleneckQueue).GetName());
    tchFirstBottleneck.SetQueueLimits("ns3::DynamicQueueLimits", "HoldTime", StringValue("1ms"));
    tchFirstBottleneck.Install(firstBottleneckDevices.Get(0));
    if (topology.IsTwoBottleneck())
    {
        TrafficControlHelper tchSecondBottleneck;
        const std::string secondBottleneckQueue =
            topology.bottlenecks.at(1).queueType.has_value()
                ? TcpAqmQueueDiscTypeToString(*topology.bottlenecks.at(1).queueType)
                : queueType;
        tchSecondBottleneck.SetRootQueueDisc(
            ResolveQueueDiscTypeId(secondBottleneckQueue).GetName());
        tchSecondBottleneck.SetQueueLimits("ns3::DynamicQueueLimits",
                                           "HoldTime",
                                           StringValue("1ms"));
        tchSecondBottleneck.Install(secondBottleneckDevices.Get(0));
    }

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer pingServerIfaces = ipv4.Assign(pingServerDevices);
    ipv4.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer firstServerIfaces = ipv4.Assign(firstServerDevices);
    ipv4.SetBase("10.1.3.0", "255.255.255.0");
    Ipv4InterfaceContainer secondServerIfaces = ipv4.Assign(secondServerDevices);
    ipv4.SetBase("172.16.1.0", "255.255.255.0");
    Ipv4InterfaceContainer firstBottleneckIfaces = ipv4.Assign(firstBottleneckDevices);
    Ipv4InterfaceContainer secondBottleneckIfaces;
    if (topology.IsTwoBottleneck())
    {
        ipv4.SetBase("172.16.2.0", "255.255.255.0");
        secondBottleneckIfaces = ipv4.Assign(secondBottleneckDevices);
    }
    ipv4.SetBase("192.168.1.0", "255.255.255.0");
    Ipv4InterfaceContainer pingClientIfaces = ipv4.Assign(pingClientDevices);
    ipv4.SetBase("192.168.2.0", "255.255.255.0");
    Ipv4InterfaceContainer firstClientIfaces = ipv4.Assign(firstClientDevices);
    ipv4.SetBase("192.168.3.0", "255.255.255.0");
    Ipv4InterfaceContainer secondClientIfaces = ipv4.Assign(secondClientDevices);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    ////////////////////////////////////////////////////////////
    // application setup                                      //
    ////////////////////////////////////////////////////////////
    PingHelper pingHelper(Ipv4Address("192.168.1.2"));
    pingHelper.SetAttribute("Interval", TimeValue(pingInterval));
    pingHelper.SetAttribute("Size", UintegerValue(pingSize));
    pingHelper.SetAttribute("VerboseMode", EnumValue(Ping::VerboseMode::SILENT));
    ApplicationContainer pingContainer = pingHelper.Install(pingServer);
    Ptr<Ping> ping = pingContainer.Get(0)->GetObject<Ping>();
    ping->TraceConnectWithoutContext("Rtt", MakeBoundCallback(&TracePingRtt, &pingOfStream));
    pingContainer.Start(Seconds(1));
    pingContainer.Stop(stopTime - Seconds(1));

    ApplicationContainer firstApp;
    uint16_t firstPort = 5000;
    BulkSendHelper tcp("ns3::TcpSocketFactory", Address());
    // set to large value:  e.g. 1000 Mb/s for 60 seconds = 7500000000 bytes
    tcp.SetAttribute("MaxBytes", UintegerValue(7500000000));
    // Configure first TCP client/server pair
    InetSocketAddress firstDestAddress(firstClientIfaces.GetAddress(1), firstPort);
    tcp.SetAttribute("Remote", AddressValue(firstDestAddress));
    firstApp = tcp.Install(firstServer);
    firstApp.Start(firstFlowStart);
    firstApp.Stop(stopTime - Seconds(1));

    Address firstSinkAddress(InetSocketAddress(Ipv4Address::GetAny(), firstPort));
    ApplicationContainer firstSinkApp;
    PacketSinkHelper firstSinkHelper("ns3::TcpSocketFactory", firstSinkAddress);
    firstSinkApp = firstSinkHelper.Install(firstClient);
    firstSinkApp.Start(firstFlowStart);
    firstSinkApp.Stop(stopTime - MilliSeconds(500));

    // Configure second TCP client/server pair
    if (enableSecondTcp)
    {
        BulkSendHelper tcp("ns3::TcpSocketFactory", Address());
        uint16_t secondPort = 5000;
        ApplicationContainer secondApp;
        InetSocketAddress secondDestAddress(secondClientIfaces.GetAddress(1), secondPort);
        tcp.SetAttribute("Remote", AddressValue(secondDestAddress));
        secondApp = tcp.Install(secondServer);
        secondApp.Start(secondFlowStart);
        secondApp.Stop(stopTime - Seconds(1));

        Address secondSinkAddress(InetSocketAddress(Ipv4Address::GetAny(), secondPort));
        PacketSinkHelper secondSinkHelper("ns3::TcpSocketFactory", secondSinkAddress);
        ApplicationContainer secondSinkApp;
        secondSinkApp = secondSinkHelper.Install(secondClient);
        secondSinkApp.Start(secondFlowStart);
        secondSinkApp.Stop(stopTime - MilliSeconds(500));
    }

    // Setup traces that can be hooked now
    Ptr<TrafficControlLayer> tc;
    Ptr<QueueDisc> qd;
    Ptr<NetDevice> tracedBottleneckDevice = firstBottleneckDevices.Get(0);
    DataRate tracedBottleneckRate = topology.bottlenecks.at(0).rate;
    if (topology.IsTwoBottleneck() && !topology.bottlenecks.at(0).trace &&
        topology.bottlenecks.at(1).trace)
    {
        tracedBottleneckDevice = secondBottleneckDevices.Get(0);
        tracedBottleneckRate = topology.bottlenecks.at(1).rate;
    }
    // Trace drops, marks, and queueing delay for the configured bottleneck.
    tc = tracedBottleneckDevice->GetNode()->GetObject<TrafficControlLayer>();
    qd = tc->GetRootQueueDiscOnDevice(tracedBottleneckDevice);
    qd->TraceConnectWithoutContext("Drop", MakeBoundCallback(&TraceQueueDrop, &queueDropOfStream));
    qd->TraceConnectWithoutContext("Mark", MakeBoundCallback(&TraceQueueMark, &queueMarkOfStream));
    qd->TraceConnectWithoutContext(
        "BytesInQueue",
        MakeBoundCallback(&TraceQueueLength, &queueLengthOfStream, tracedBottleneckRate));

    // Setup scheduled traces; TCP traces must be hooked after socket creation
    Simulator::Schedule(firstFlowStart + MilliSeconds(100),
                        &ScheduleFirstTcpRttTraceConnection,
                        &firstTcpRttOfStream);
    Simulator::Schedule(firstFlowStart + MilliSeconds(100),
                        &ScheduleFirstTcpCwndTraceConnection,
                        &firstTcpCwndOfStream);
    Simulator::Schedule(firstFlowStart + MilliSeconds(100), &ScheduleFirstPacketSinkConnection);
    if (firstTcpIsDctcp)
    {
        Simulator::Schedule(firstFlowStart + MilliSeconds(100),
                            &ScheduleFirstDctcpTraceConnection,
                            &firstTcpDctcpOfStream);
    }
    Simulator::Schedule(throughputSamplingInterval,
                        &TraceFirstThroughput,
                        &firstTcpThroughputOfStream,
                        throughputSamplingInterval);
    if (enableSecondTcp)
    {
        // Setup scheduled traces; TCP traces must be hooked after socket creation
        Simulator::Schedule(secondFlowStart + MilliSeconds(100),
                            &ScheduleSecondTcpRttTraceConnection,
                            &secondTcpRttOfStream);
        Simulator::Schedule(secondFlowStart + MilliSeconds(100),
                            &ScheduleSecondTcpCwndTraceConnection,
                            &secondTcpCwndOfStream);
        Simulator::Schedule(secondFlowStart + MilliSeconds(100), &ScheduleSecondPacketSinkConnection);
        Simulator::Schedule(throughputSamplingInterval,
                            &TraceSecondThroughput,
                            &secondTcpThroughputOfStream,
                            throughputSamplingInterval);
        if (secondTcpIsDctcp)
        {
            Simulator::Schedule(secondFlowStart + MilliSeconds(100),
                                &ScheduleSecondDctcpTraceConnection,
                                &secondTcpDctcpOfStream);
        }
    }
    Simulator::Schedule(marksSamplingInterval,
                        &TraceMarksFrequency,
                        &queueMarksFrequencyOfStream,
                        marksSamplingInterval);

    if (enablePcap)
    {
        p2p.EnablePcapAll("tcp-aqm-benchmark", false);
    }

    Simulator::Stop(stopTime);
    Simulator::Run();
    Simulator::Destroy();

    if (g_validate.empty())
    {
        pingOfStream.close();
        firstTcpCwndOfStream.close();
        firstTcpRttOfStream.close();
        if (firstTcpIsDctcp)
        {
            firstTcpDctcpOfStream.close();
        }
        firstTcpThroughputOfStream.close();
        if (enableSecondTcp)
        {
            secondTcpCwndOfStream.close();
            secondTcpRttOfStream.close();
            secondTcpThroughputOfStream.close();
            if (secondTcpIsDctcp)
            {
                secondTcpDctcpOfStream.close();
            }
        }
        queueDropOfStream.close();
        queueMarkOfStream.close();
        queueMarksFrequencyOfStream.close();
        queueLengthOfStream.close();
    }

    if (g_validationFailed)
    {
        NS_FATAL_ERROR("Validation failed");
    }

    return 0;
}
