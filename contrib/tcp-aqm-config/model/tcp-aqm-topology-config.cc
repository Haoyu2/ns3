/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "tcp-aqm-topology-config.h"

#include "ns3/abort.h"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <iterator>
#include <ostream>

namespace ns3
{
namespace
{

std::string
Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    return value;
}

std::string
TimeToString(Time value)
{
    const auto ns = value.GetNanoSeconds();
    if (ns % 1000000000 == 0)
    {
        return fmt::format("{}s", ns / 1000000000);
    }
    if (ns % 1000000 == 0)
    {
        return fmt::format("{}ms", ns / 1000000);
    }
    if (ns % 1000 == 0)
    {
        return fmt::format("{}us", ns / 1000);
    }
    return fmt::format("{}ns", ns);
}

std::string
DataRateToString(DataRate value)
{
    const auto bps = value.GetBitRate();
    if (bps % 1000000000 == 0)
    {
        return fmt::format("{}Gbps", bps / 1000000000);
    }
    if (bps % 1000000 == 0)
    {
        return fmt::format("{}Mbps", bps / 1000000);
    }
    if (bps % 1000 == 0)
    {
        return fmt::format("{}kbps", bps / 1000);
    }
    return fmt::format("{}bps", bps);
}

std::string
QueueSizeToString(const QueueSize& value)
{
    const auto unit = value.GetUnit() == QueueSizeUnit::PACKETS ? "p" : "B";
    return fmt::format("{}{}", value.GetValue(), unit);
}

} // namespace

TcpAqmTopologyKind
ParseTcpAqmTopologyKind(const std::string& value)
{
    const auto lower = Lower(value);
    if (lower == "single-bottleneck" || lower == "single")
    {
        return TcpAqmTopologyKind::SINGLE_BOTTLENECK;
    }
    if (lower == "two-bottleneck" || lower == "two" || lower == "two-bottlenecks")
    {
        return TcpAqmTopologyKind::TWO_BOTTLENECK;
    }
    NS_ABORT_MSG("Unsupported TCP/AQM topology kind: " << value);
    return TcpAqmTopologyKind::SINGLE_BOTTLENECK;
}

std::string
TcpAqmTopologyKindToString(TcpAqmTopologyKind kind)
{
    switch (kind)
    {
    case TcpAqmTopologyKind::SINGLE_BOTTLENECK:
        return "single-bottleneck";
    case TcpAqmTopologyKind::TWO_BOTTLENECK:
        return "two-bottleneck";
    }
    NS_ABORT_MSG("Unhandled TCP/AQM topology kind");
    return "single-bottleneck";
}

TcpAqmQueueDiscType
ParseTcpAqmQueueDiscType(const std::string& value)
{
    const auto lower = Lower(value);
    if (lower == "fq" || lower == "fq-codel" || lower == "fqcodel")
    {
        return TcpAqmQueueDiscType::FQ_CODEL;
    }
    if (lower == "codel")
    {
        return TcpAqmQueueDiscType::CODEL;
    }
    if (lower == "pie")
    {
        return TcpAqmQueueDiscType::PIE;
    }
    if (lower == "red")
    {
        return TcpAqmQueueDiscType::RED;
    }
    NS_ABORT_MSG("Unsupported TCP/AQM queue type: " << value);
    return TcpAqmQueueDiscType::CODEL;
}

std::string
TcpAqmQueueDiscTypeToString(TcpAqmQueueDiscType type)
{
    switch (type)
    {
    case TcpAqmQueueDiscType::FQ_CODEL:
        return "fq";
    case TcpAqmQueueDiscType::CODEL:
        return "codel";
    case TcpAqmQueueDiscType::PIE:
        return "pie";
    case TcpAqmQueueDiscType::RED:
        return "red";
    }
    NS_ABORT_MSG("Unhandled TCP/AQM queue type");
    return "codel";
}

std::string
TcpAqmQueueDiscTypeIdName(TcpAqmQueueDiscType type)
{
    switch (type)
    {
    case TcpAqmQueueDiscType::FQ_CODEL:
        return "ns3::FqCoDelQueueDisc";
    case TcpAqmQueueDiscType::CODEL:
        return "ns3::CoDelQueueDisc";
    case TcpAqmQueueDiscType::PIE:
        return "ns3::PieQueueDisc";
    case TcpAqmQueueDiscType::RED:
        return "ns3::RedQueueDisc";
    }
    NS_ABORT_MSG("Unhandled TCP/AQM queue type");
    return "ns3::CoDelQueueDisc";
}

bool
TcpAqmTopologyConfig::IsSingleBottleneck() const
{
    return kind == TcpAqmTopologyKind::SINGLE_BOTTLENECK;
}

bool
TcpAqmTopologyConfig::IsTwoBottleneck() const
{
    return kind == TcpAqmTopologyKind::TWO_BOTTLENECK;
}

void
TcpAqmTopologyConfig::Validate(bool requireDelays) const
{
    const auto expectedBottlenecks = IsSingleBottleneck() ? 1U : 2U;
    NS_ABORT_MSG_UNLESS(bottlenecks.size() == expectedBottlenecks,
                        "Topology " << TcpAqmTopologyKindToString(kind) << " requires "
                                    << expectedBottlenecks << " bottleneck(s), found "
                                    << bottlenecks.size());

    NS_ABORT_MSG_UNLESS(accessRate.GetBitRate() > 0, "accessRate must be greater than zero");
    NS_ABORT_MSG_UNLESS(!accessDelay.IsNegative(), "accessDelay must not be negative");
    NS_ABORT_MSG_UNLESS(deviceQueueSize.GetValue() > 0,
                        "deviceQueueSize must be greater than zero");

    for (const auto& bottleneck : bottlenecks)
    {
        NS_ABORT_MSG_UNLESS(!bottleneck.name.empty(), "bottleneck name must not be empty");
        NS_ABORT_MSG_UNLESS(bottleneck.rate.GetBitRate() > 0,
                            "bottleneck " << bottleneck.name << " rate must be greater than zero");
        if (requireDelays)
        {
            NS_ABORT_MSG_UNLESS(bottleneck.delay.has_value(),
                                "bottleneck " << bottleneck.name << " delay must be set");
        }
        if (bottleneck.delay.has_value())
        {
            NS_ABORT_MSG_UNLESS(!bottleneck.delay->IsNegative(),
                                "bottleneck " << bottleneck.name << " delay must not be negative");
        }
    }
}

void
TcpAqmTopologyConfig::FillDerivedDelays(Time baseRtt)
{
    const uint32_t segments = IsTwoBottleneck() ? 4 : 2;
    const Time defaultDelay = baseRtt / segments;
    for (auto& bottleneck : bottlenecks)
    {
        if (!bottleneck.delay.has_value())
        {
            bottleneck.delay = defaultDelay;
        }
    }
}

std::string
TcpAqmTopologyConfig::ToString() const
{
    fmt::memory_buffer buffer;
    fmt::format_to(std::back_inserter(buffer),
                   "topology.kind={}\n"
                   "topology.accessRate={}\n"
                   "topology.accessDelay={}\n"
                   "topology.deviceQueueSize={}\n",
                   TcpAqmTopologyKindToString(kind),
                   DataRateToString(accessRate),
                   TimeToString(accessDelay),
                   QueueSizeToString(deviceQueueSize));
    for (uint32_t index = 0; index < bottlenecks.size(); ++index)
    {
        const auto& bottleneck = bottlenecks[index];
        fmt::format_to(std::back_inserter(buffer),
                       "topology.bottlenecks[{}].name={}\n"
                       "topology.bottlenecks[{}].rate={}\n"
                       "topology.bottlenecks[{}].delay={}\n"
                       "topology.bottlenecks[{}].queueType={}\n"
                       "topology.bottlenecks[{}].trace={}\n",
                       index,
                       bottleneck.name,
                       index,
                       DataRateToString(bottleneck.rate),
                       index,
                       bottleneck.delay.has_value() ? TimeToString(*bottleneck.delay) : "",
                       index,
                       bottleneck.queueType.has_value()
                           ? TcpAqmQueueDiscTypeToString(*bottleneck.queueType)
                           : "",
                       index,
                       bottleneck.trace);
    }
    return fmt::to_string(buffer);
}

std::ostream&
operator<<(std::ostream& os, const TcpAqmTopologyConfig& config)
{
    os << config.ToString();
    return os;
}

} // namespace ns3
