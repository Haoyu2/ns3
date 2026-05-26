/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * Network topology
 *
 *   sender --- (1 Gb/s, 1 ms) --- router --- (100 Mb/s, 10 ms) --- receiver
 *                                       ^
 *                                       |
 *                          CAKE root qdisc, shaped to --bandwidth
 *
 * Several long-lived TCP flows run from sender to receiver. CAKE on the router's
 * bottleneck device is the actual rate limiter (its shaper rate is below the
 * link rate), so it controls the standing queue. The example reports, per flow,
 * the achieved throughput and the mean one-way delay (which stays low thanks to
 * CAKE's per-flow COBALT AQM), plus CAKE's drop statistics. A TCP-aware ACK
 * identifier is installed so that ACK filtering is active.
 */

#include "ns3/applications-module.h"
#include "ns3/cake-ack-identifier.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("CakeBufferbloatExample");

int
main(int argc, char* argv[])
{
    std::string bandwidth = "10Mbps"; // CAKE shaper rate (the bottleneck)
    uint32_t nFlows = 3;
    double simTime = 10.0;

    CommandLine cmd(__FILE__);
    cmd.AddValue("bandwidth", "CAKE shaper rate", bandwidth);
    cmd.AddValue("nFlows", "Number of long-lived TCP flows", nFlows);
    cmd.AddValue("simTime", "Simulation time in seconds", simTime);
    cmd.Parse(argc, argv);

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
    access.SetChannelAttribute("Delay", StringValue("1ms"));

    PointToPointHelper bottleneck;
    bottleneck.SetDeviceAttribute("DataRate", StringValue("100Mbps")); // link faster than shaper
    bottleneck.SetChannelAttribute("Delay", StringValue("10ms"));

    NetDeviceContainer accessDev = access.Install(sender.Get(0), router.Get(0));
    NetDeviceContainer bottleneckDev = bottleneck.Install(router.Get(0), receiver.Get(0));

    InternetStackHelper stack;
    stack.InstallAll();

    // Install CAKE as the root qdisc on the router's bottleneck device.
    TrafficControlHelper tch;
    tch.SetRootQueueDisc("ns3::CakeQueueDisc",
                         "Bandwidth",
                         DataRateValue(DataRate(bandwidth)),
                         "AckFilter",
                         StringValue("Filter"));
    QueueDiscContainer qdiscs = tch.Install(bottleneckDev.Get(0));

    // Make ACK filtering effective by teaching CAKE how to recognise TCP ACKs.
    Ptr<CakeQueueDisc> cake = DynamicCast<CakeQueueDisc>(qdiscs.Get(0));
    cake->SetAckIdentifier(MakeTcpAckIdentifier());

    Ipv4AddressHelper address;
    address.SetBase("10.0.0.0", "255.255.255.0");
    address.Assign(accessDev);
    address.SetBase("10.0.1.0", "255.255.255.0");
    Ipv4InterfaceContainer bottleneckIf = address.Assign(bottleneckDev);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    uint16_t basePort = 9000;
    Ipv4Address receiverAddr = bottleneckIf.GetAddress(1);

    ApplicationContainer sinkApps;
    ApplicationContainer sourceApps;
    for (uint32_t f = 0; f < nFlows; f++)
    {
        uint16_t port = basePort + f;
        PacketSinkHelper sink("ns3::TcpSocketFactory",
                              InetSocketAddress(Ipv4Address::GetAny(), port));
        sinkApps.Add(sink.Install(receiver.Get(0)));

        BulkSendHelper source("ns3::TcpSocketFactory", InetSocketAddress(receiverAddr, port));
        source.SetAttribute("MaxBytes", UintegerValue(0)); // unlimited
        sourceApps.Add(source.Install(sender.Get(0)));
    }
    sinkApps.Start(Seconds(0.0));
    sinkApps.Stop(Seconds(simTime));
    sourceApps.Start(Seconds(1.0));
    sourceApps.Stop(Seconds(simTime - 1.0));

    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> monitor = flowmonHelper.InstallAll();

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    monitor->CheckForLostPackets();
    auto classifier = DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
    auto stats = monitor->GetFlowStats();

    std::cout << "\n=== CAKE bufferbloat example (shaper " << bandwidth << ", " << nFlows
              << " flows) ===\n";
    double aggregate = 0;
    for (const auto& kv : stats)
    {
        auto tuple = classifier->FindFlow(kv.first);
        if (tuple.destinationPort < basePort || tuple.destinationPort >= basePort + nFlows)
        {
            continue; // skip reverse (ACK) flows
        }
        double rxMbps = kv.second.rxBytes * 8.0 / simTime / 1e6;
        double meanDelayMs =
            kv.second.rxPackets ? kv.second.delaySum.GetSeconds() / kv.second.rxPackets * 1000 : 0;
        aggregate += rxMbps;
        std::cout << "  flow " << tuple.sourceAddress << " -> " << tuple.destinationAddress << ":"
                  << tuple.destinationPort << "  throughput=" << rxMbps
                  << " Mbps  meanDelay=" << meanDelayMs << " ms\n";
    }
    std::cout << "  aggregate throughput = " << aggregate << " Mbps (shaper " << bandwidth << ")\n";

    QueueDisc::Stats st = cake->GetStats();
    std::cout << "  CAKE total drops      = " << st.nTotalDroppedPackets << "\n";
    std::cout << "  CAKE ACK-filter drops = "
              << st.GetNDroppedPackets(CakeQueueDisc::ACK_FILTER_DROP)
              << "  (most useful on asymmetric/uplink-limited paths)\n";

    Simulator::Destroy();
    return 0;
}
