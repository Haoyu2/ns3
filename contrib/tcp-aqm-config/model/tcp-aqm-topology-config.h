/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef TCP_AQM_TOPOLOGY_CONFIG_H
#define TCP_AQM_TOPOLOGY_CONFIG_H

#include "ns3/data-rate.h"
#include "ns3/nstime.h"
#include "ns3/queue-size.h"

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace ns3
{

/**
 * Supported topology templates.
 */
enum class TcpAqmTopologyKind
{
    SINGLE_BOTTLENECK,
    TWO_BOTTLENECK,
};

/**
 * Supported bottleneck queue-disc choices.
 */
enum class TcpAqmQueueDiscType
{
    FQ_CODEL,
    CODEL,
    PIE,
    RED,
};

TcpAqmTopologyKind ParseTcpAqmTopologyKind(const std::string& value);
std::string TcpAqmTopologyKindToString(TcpAqmTopologyKind kind);

TcpAqmQueueDiscType ParseTcpAqmQueueDiscType(const std::string& value);
std::string TcpAqmQueueDiscTypeToString(TcpAqmQueueDiscType type);
std::string TcpAqmQueueDiscTypeIdName(TcpAqmQueueDiscType type);

/**
 * Configuration for one queue-controlled bottleneck segment.
 */
struct TcpAqmBottleneckConfig
{
    std::string name{"bottleneck"};
    DataRate rate{DataRate("50Mbps")};
    std::optional<Time> delay;
    std::optional<TcpAqmQueueDiscType> queueType;
    bool trace{true};
};

/**
 * Declarative topology configuration for TCP/AQM benchmark scenarios.
 *
 * The first supported templates are intentionally small and reviewable:
 * a single dumbbell bottleneck and two serial bottlenecks.  More general graph
 * topologies can be added later without changing the experiment schema.
 */
class TcpAqmTopologyConfig
{
  public:
    TcpAqmTopologyKind kind{TcpAqmTopologyKind::SINGLE_BOTTLENECK};
    DataRate accessRate{DataRate("1000Mbps")};
    Time accessDelay{MicroSeconds(1)};
    QueueSize deviceQueueSize{QueueSize("3p")};
    std::vector<TcpAqmBottleneckConfig> bottlenecks{{}};

    bool IsSingleBottleneck() const;
    bool IsTwoBottleneck() const;
    void Validate(bool requireDelays = true) const;
    void FillDerivedDelays(Time baseRtt);
    std::string ToString() const;
};

std::ostream& operator<<(std::ostream& os, const TcpAqmTopologyConfig& config);

} // namespace ns3

#endif // TCP_AQM_TOPOLOGY_CONFIG_H
