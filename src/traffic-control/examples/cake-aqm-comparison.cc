/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * Parametrized single-bottleneck benchmark used to compare CAKE against the
 * other ns-3 AQM / flow-isolation queue discs under a bufferbloat workload.
 *
 *   sender --- (1 Gb/s, 0.1 ms) --- router --- (bandwidth, rtt/2) --- receiver
 *                                         ^
 *                                  root queue disc under test
 *
 * Several long-lived TCP flows saturate the bottleneck while a sparse UDP probe
 * measures the latency experienced under load. The program prints a single
 * machine-readable line that the campaign driver collects:
 *
 *   RESULT,<qdisc>,<bwMbps>,<rttMs>,<nFlows>,<seed>,<aggMbps>,<jain>,
 *          <probeMeanDelayMs>,<probeJitterMs>,<drops>
 *
 * All queue discs see the same link-rate bottleneck, so this isolates the
 * AQM / flow-isolation behaviour. (CAKE runs in unlimited mode here; its
 * integrated shaper is exercised separately by cake-bufferbloat-example.)
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/net-device-container.h"
#include "ns3/node-container.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

#include <map>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("CakeAqmComparison");

int
main(int argc, char* argv[])
{
    std::string qdisc = "Cake";
    double bandwidth = 10.0; // Mbps
    double rtt = 40.0;       // ms (base, no queuing)
    uint32_t nFlows = 4;
    double simTime = 20.0;
    uint32_t seed = 1;

    CommandLine cmd(__FILE__);
    cmd.AddValue("qdisc", "Cake | FqCoDel | FqCobalt | Pie | PfifoFast", qdisc);
    cmd.AddValue("bandwidth", "Bottleneck rate in Mbps", bandwidth);
    cmd.AddValue("rtt", "Base RTT in ms", rtt);
    cmd.AddValue("nFlows", "Number of long-lived TCP flows", nFlows);
    cmd.AddValue("simTime", "Simulation time in seconds", simTime);
    cmd.AddValue("seed", "RNG run number (for variance across repetitions)", seed);
    cmd.Parse(argc, argv);

    static const std::map<std::string, std::string> kTypeIds = {
        {"Cake", "ns3::CakeQueueDisc"},
        {"FqCoDel", "ns3::FqCoDelQueueDisc"},
        {"FqCobalt", "ns3::FqCobaltQueueDisc"},
        {"Pie", "ns3::PieQueueDisc"},
        {"PfifoFast", "ns3::PfifoFastQueueDisc"},
    };
    auto it = kTypeIds.find(qdisc);
    if (it == kTypeIds.end())
    {
        std::cerr << "Unknown qdisc '" << qdisc << "'\n";
        return 1;
    }

    RngSeedManager::SetSeed(1);
    RngSeedManager::SetRun(seed);

    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpCubic"));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1448));

    NodeContainer sender;
    NodeContainer router;
    NodeContainer receiver;
    sender.Create(1);
    router.Create(1);
    receiver.Create(1);

    PointToPointHelper access;
    access.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    access.SetChannelAttribute("Delay", StringValue("0.1ms"));

    std::ostringstream bw;
    bw << bandwidth << "Mbps";
    std::ostringstream half;
    half << (rtt / 2.0) << "ms";

    PointToPointHelper bottleneck;
    bottleneck.SetDeviceAttribute("DataRate", StringValue(bw.str()));
    bottleneck.SetChannelAttribute("Delay", StringValue(half.str()));
    // Keep the device's own FIFO tiny so the queue disc under test is the real
    // buffer; otherwise the default 100-packet device queue would itself bloat.
    bottleneck.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("5p"));

    NetDeviceContainer accessDev = access.Install(sender.Get(0), router.Get(0));
    NetDeviceContainer bottleneckDev = bottleneck.Install(router.Get(0), receiver.Get(0));

    InternetStackHelper stack;
    stack.InstallAll();

    TrafficControlHelper tch;
    tch.SetRootQueueDisc(it->second);
    QueueDiscContainer qdiscs = tch.Install(bottleneckDev.Get(0));

    Ipv4AddressHelper address;
    address.SetBase("10.0.0.0", "255.255.255.0");
    address.Assign(accessDev);
    address.SetBase("10.0.1.0", "255.255.255.0");
    Ipv4InterfaceContainer bottleneckIf = address.Assign(bottleneckDev);
    Ipv4Address receiverAddr = bottleneckIf.GetAddress(1);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Long-lived TCP flows (bulk).
    const uint16_t basePort = 9000;
    ApplicationContainer sinks;
    ApplicationContainer sources;
    for (uint32_t f = 0; f < nFlows; f++)
    {
        uint16_t port = basePort + f;
        PacketSinkHelper sink("ns3::TcpSocketFactory",
                              InetSocketAddress(Ipv4Address::GetAny(), port));
        sinks.Add(sink.Install(receiver.Get(0)));
        BulkSendHelper source("ns3::TcpSocketFactory", InetSocketAddress(receiverAddr, port));
        source.SetAttribute("MaxBytes", UintegerValue(0));
        sources.Add(source.Install(sender.Get(0)));
    }
    sinks.Start(Seconds(0.0));
    sinks.Stop(Seconds(simTime));
    sources.Start(Seconds(0.1));
    sources.Stop(Seconds(simTime));

    // Sparse UDP latency probe sharing the bottleneck.
    const uint16_t probePort = 8000;
    OnOffHelper probe("ns3::UdpSocketFactory", InetSocketAddress(receiverAddr, probePort));
    probe.SetConstantRate(DataRate("200kbps"), 100);
    ApplicationContainer probeApp = probe.Install(sender.Get(0));
    probeApp.Start(Seconds(0.1));
    probeApp.Stop(Seconds(simTime));
    PacketSinkHelper probeSink("ns3::UdpSocketFactory",
                               InetSocketAddress(Ipv4Address::GetAny(), probePort));
    ApplicationContainer probeSinkApp = probeSink.Install(receiver.Get(0));
    probeSinkApp.Start(Seconds(0.0));
    probeSinkApp.Stop(Seconds(simTime));

    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> monitor = flowmonHelper.InstallAll();

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    monitor->CheckForLostPackets();
    auto classifier = DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
    auto stats = monitor->GetFlowStats();

    double aggThroughput = 0;
    double sumX = 0;
    double sumX2 = 0;
    uint32_t flowCount = 0;
    double probeMeanDelayMs = 0;
    double probeJitterMs = 0;

    for (const auto& kv : stats)
    {
        auto tuple = classifier->FindFlow(kv.first);
        if (tuple.destinationPort >= basePort && tuple.destinationPort < basePort + nFlows)
        {
            double mbps = kv.second.rxBytes * 8.0 / simTime / 1e6;
            aggThroughput += mbps;
            sumX += mbps;
            sumX2 += mbps * mbps;
            flowCount++;
        }
        else if (tuple.destinationPort == probePort && kv.second.rxPackets > 0)
        {
            probeMeanDelayMs = kv.second.delaySum.GetSeconds() / kv.second.rxPackets * 1000.0;
            if (kv.second.rxPackets > 1)
            {
                probeJitterMs =
                    kv.second.jitterSum.GetSeconds() / (kv.second.rxPackets - 1) * 1000.0;
            }
        }
    }

    double jain = (flowCount > 0 && sumX2 > 0) ? (sumX * sumX) / (flowCount * sumX2) : 0.0;
    uint64_t drops = qdiscs.Get(0)->GetStats().nTotalDroppedPackets;

    std::cout << "RESULT," << qdisc << "," << bandwidth << "," << rtt << "," << nFlows << "," << seed
              << "," << aggThroughput << "," << jain << "," << probeMeanDelayMs << ","
              << probeJitterMs << "," << drops << std::endl;

    Simulator::Destroy();
    return 0;
}
