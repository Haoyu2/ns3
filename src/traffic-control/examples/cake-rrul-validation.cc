/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * An rrul-like ("Realtime Response Under Load") scenario used to validate the
 * ns-3 CAKE model against Linux' sch_cake. It mirrors the essence of Flent's
 * rrul test:
 *
 *   client <-- (1 Gb/s, rtt/2) --> router <-- (1 Gb/s, rtt/2) --> server
 *                                       ^                  ^
 *                                  CAKE shaped to --bandwidth (both directions)
 *
 *   * nFlows long-lived TCP flows downstream (server -> client)
 *   * nFlows long-lived TCP flows upstream  (client -> server)
 *   * a UDP echo "ping" that samples the round-trip time under load
 *
 * It prints one machine-readable line consumed by cake-validation-compare.py:
 *
 *   RESULT,<bwMbps>,<rttMs>,<nFlows>,<seed>,<downMbps>,<upMbps>,
 *          <rttP50>,<rttP90>,<rttP99>,<rttMax>,<ackDrops>
 *
 * The corresponding Linux reference is captured with `tc qdisc ... cake` plus
 * Flent's rrul test; see the "Validation against Linux sch_cake" section of
 * doc/cake.rst.
 */

#include "ns3/applications-module.h"
#include "ns3/cake-ack-identifier.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("CakeRrulValidation");

namespace
{
std::vector<double> g_rttMs; //!< collected round-trip-time samples (ms)

/// Server side: echo each probe packet back to its sender.
void
EchoProbe(Ptr<Socket> socket)
{
    Ptr<Packet> packet;
    Address from;
    while ((packet = socket->RecvFrom(from)))
    {
        socket->SendTo(packet, 0, from);
    }
}

/// Client side: recover the embedded timestamp and record the RTT.
void
RecordRtt(Ptr<Socket> socket)
{
    Ptr<Packet> packet;
    while ((packet = socket->Recv()))
    {
        if (packet->GetSize() >= sizeof(uint64_t))
        {
            uint8_t buffer[sizeof(uint64_t)];
            packet->CopyData(buffer, sizeof(uint64_t));
            uint64_t sentNs;
            std::memcpy(&sentNs, buffer, sizeof(uint64_t));
            double rtt = (Simulator::Now().GetNanoSeconds() - static_cast<int64_t>(sentNs)) / 1e6;
            g_rttMs.push_back(rtt);
        }
    }
}

/// Client side: send a timestamped probe and reschedule until stopTime.
void
SendProbe(Ptr<Socket> socket, Time interval, Time stopTime)
{
    uint64_t now = Simulator::Now().GetNanoSeconds();
    uint8_t buffer[100] = {0};
    std::memcpy(buffer, &now, sizeof(uint64_t));
    socket->Send(Create<Packet>(buffer, sizeof(buffer)));
    if (Simulator::Now() + interval < stopTime)
    {
        Simulator::Schedule(interval, &SendProbe, socket, interval, stopTime);
    }
}

double
Percentile(std::vector<double> values, double pct)
{
    if (values.empty())
    {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    auto idx = static_cast<size_t>(std::ceil(pct / 100.0 * values.size()));
    if (idx > 0)
    {
        idx -= 1;
    }
    if (idx >= values.size())
    {
        idx = values.size() - 1;
    }
    return values[idx];
}

double
TotalRxMbps(const ApplicationContainer& sinks, double simTime)
{
    uint64_t bytes = 0;
    for (uint32_t i = 0; i < sinks.GetN(); i++)
    {
        bytes += DynamicCast<PacketSink>(sinks.Get(i))->GetTotalRx();
    }
    return bytes * 8.0 / simTime / 1e6;
}
} // namespace

int
main(int argc, char* argv[])
{
    double bandwidth = 10.0; // Mbps (CAKE shaper, both directions)
    double rtt = 40.0;       // ms (base)
    uint32_t nFlows = 4;
    double simTime = 30.0;
    uint32_t seed = 1;
    std::string rttOut;
    double upRate = 0;      // upstream shaper rate in Mbps; 0 => symmetric (= bandwidth)
    bool ackFilter = true;  // enable CAKE ACK filtering
    uint32_t downFlows = 0; // 0 => nFlows
    uint32_t upFlows = 0;   // 0 => nFlows

    CommandLine cmd(__FILE__);
    cmd.AddValue("bandwidth", "CAKE downstream shaper rate in Mbps", bandwidth);
    cmd.AddValue("upRate", "CAKE upstream shaper rate in Mbps (0 = symmetric)", upRate);
    cmd.AddValue("rtt", "Base RTT in ms", rtt);
    cmd.AddValue("nFlows", "Default TCP flows per direction", nFlows);
    cmd.AddValue("downFlows", "Downstream TCP flows (0 = nFlows)", downFlows);
    cmd.AddValue("upFlows", "Upstream TCP flows (0 = nFlows)", upFlows);
    cmd.AddValue("ackFilter", "Enable CAKE ACK filtering", ackFilter);
    cmd.AddValue("simTime", "Simulation time in seconds", simTime);
    cmd.AddValue("seed", "RNG run number", seed);
    cmd.AddValue("rttout", "Optional file to dump per-sample RTTs (ms), one per line", rttOut);
    cmd.Parse(argc, argv);

    if (downFlows == 0)
    {
        downFlows = nFlows;
    }
    if (upFlows == 0)
    {
        upFlows = nFlows;
    }
    if (upRate == 0)
    {
        upRate = bandwidth;
    }

    RngSeedManager::SetSeed(1);
    RngSeedManager::SetRun(seed);

    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpCubic"));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1448));
    // Large socket buffers so the window is not the bottleneck at high
    // bandwidth-delay product (Linux autotunes; we size statically to match).
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(8 * 1024 * 1024));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(8 * 1024 * 1024));

    NodeContainer client;
    NodeContainer router;
    NodeContainer server;
    client.Create(1);
    router.Create(1);
    server.Create(1);

    std::ostringstream half;
    half << (rtt / 4.0) << "ms"; // half the RTT split over the two channels

    PointToPointHelper link;
    link.SetDeviceAttribute("DataRate", StringValue("1Gbps")); // CAKE shaper is the bottleneck
    link.SetChannelAttribute("Delay", StringValue(half.str()));

    NetDeviceContainer accessDev = link.Install(client.Get(0), router.Get(0));
    NetDeviceContainer bottleneckDev = link.Install(router.Get(0), server.Get(0));

    InternetStackHelper stack;
    stack.InstallAll();

    // CAKE shapes each direction of the bottleneck at its own rate.
    // bottleneckDev[0] = router->server (carries upstream data and the
    // downstream flows' ACKs); bottleneckDev[1] = server->router (downstream).
    auto mbps = [](double m) { return DataRateValue(DataRate(static_cast<uint64_t>(m * 1e6))); };
    std::string ackMode = ackFilter ? "Filter" : "Disabled";
    TrafficControlHelper tchUp;
    tchUp.SetRootQueueDisc("ns3::CakeQueueDisc",
                           "Bandwidth", mbps(upRate),
                           "AckFilter", StringValue(ackMode));
    TrafficControlHelper tchDown;
    tchDown.SetRootQueueDisc("ns3::CakeQueueDisc",
                             "Bandwidth", mbps(bandwidth),
                             "AckFilter", StringValue(ackMode));
    QueueDiscContainer qdiscs;
    qdiscs.Add(tchUp.Install(bottleneckDev.Get(0)));
    qdiscs.Add(tchDown.Install(bottleneckDev.Get(1)));
    for (uint32_t i = 0; i < qdiscs.GetN(); i++)
    {
        DynamicCast<CakeQueueDisc>(qdiscs.Get(i))->SetAckIdentifier(MakeTcpAckIdentifier());
    }

    Ipv4AddressHelper address;
    address.SetBase("10.0.0.0", "255.255.255.0");
    Ipv4InterfaceContainer accessIf = address.Assign(accessDev);
    address.SetBase("10.0.1.0", "255.255.255.0");
    Ipv4InterfaceContainer bottleneckIf = address.Assign(bottleneckDev);

    Ipv4Address clientAddr = accessIf.GetAddress(0);
    Ipv4Address serverAddr = bottleneckIf.GetAddress(1);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    const uint16_t downBase = 9000;
    const uint16_t upBase = 9500;
    ApplicationContainer downSinks;
    ApplicationContainer upSinks;
    ApplicationContainer sources;

    for (uint32_t f = 0; f < downFlows; f++)
    {
        // Downstream: server -> client.
        uint16_t dport = downBase + f;
        PacketSinkHelper dSink("ns3::TcpSocketFactory",
                               InetSocketAddress(Ipv4Address::GetAny(), dport));
        downSinks.Add(dSink.Install(client.Get(0)));
        BulkSendHelper dSrc("ns3::TcpSocketFactory", InetSocketAddress(clientAddr, dport));
        dSrc.SetAttribute("MaxBytes", UintegerValue(0));
        sources.Add(dSrc.Install(server.Get(0)));
    }
    for (uint32_t f = 0; f < upFlows; f++)
    {
        // Upstream: client -> server.
        uint16_t uport = upBase + f;
        PacketSinkHelper uSink("ns3::TcpSocketFactory",
                               InetSocketAddress(Ipv4Address::GetAny(), uport));
        upSinks.Add(uSink.Install(server.Get(0)));
        BulkSendHelper uSrc("ns3::TcpSocketFactory", InetSocketAddress(serverAddr, uport));
        uSrc.SetAttribute("MaxBytes", UintegerValue(0));
        sources.Add(uSrc.Install(client.Get(0)));
    }
    downSinks.Start(Seconds(0.0));
    upSinks.Start(Seconds(0.0));
    sources.Start(Seconds(0.1));
    downSinks.Stop(Seconds(simTime));
    upSinks.Stop(Seconds(simTime));
    sources.Stop(Seconds(simTime));

    // UDP echo RTT probe (client <-> server).
    const uint16_t probePort = 7000;
    Ptr<Socket> echo = Socket::CreateSocket(server.Get(0), UdpSocketFactory::GetTypeId());
    echo->Bind(InetSocketAddress(Ipv4Address::GetAny(), probePort));
    echo->SetRecvCallback(MakeCallback(&EchoProbe));

    Ptr<Socket> probe = Socket::CreateSocket(client.Get(0), UdpSocketFactory::GetTypeId());
    probe->Connect(InetSocketAddress(serverAddr, probePort));
    probe->SetRecvCallback(MakeCallback(&RecordRtt));
    Simulator::Schedule(Seconds(0.2), &SendProbe, probe, MilliSeconds(5), Seconds(simTime));

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    double downMbps = TotalRxMbps(downSinks, simTime);
    double upMbps = TotalRxMbps(upSinks, simTime);
    uint64_t ackDrops = 0;
    for (uint32_t i = 0; i < qdiscs.GetN(); i++)
    {
        ackDrops += qdiscs.Get(i)->GetStats().GetNDroppedPackets(CakeQueueDisc::ACK_FILTER_DROP);
    }

    std::cout << "RESULT," << bandwidth << "," << rtt << "," << nFlows << "," << seed << ","
              << downMbps << "," << upMbps << "," << Percentile(g_rttMs, 50) << ","
              << Percentile(g_rttMs, 90) << "," << Percentile(g_rttMs, 99) << ","
              << Percentile(g_rttMs, 100) << "," << ackDrops << std::endl;

    if (!rttOut.empty())
    {
        std::ofstream f(rttOut);
        for (double r : g_rttMs)
        {
            f << r << "\n";
        }
    }

    Simulator::Destroy();
    return 0;
}
