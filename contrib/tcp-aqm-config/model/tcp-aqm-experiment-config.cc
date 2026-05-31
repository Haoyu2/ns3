/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "tcp-aqm-experiment-config.h"

#include "ns3/abort.h"

#include <fmt/format.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <iterator>
#include <ostream>
#include <set>
#include <type_traits>

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
MetricKey(std::string value)
{
    value = Lower(value);
    std::replace(value.begin(), value.end(), '-', '_');
    return value;
}

std::string
ParameterKey(std::string value)
{
    value = Lower(value);
    std::string key;
    key.reserve(value.size());
    for (char c : value)
    {
        if (c != '-' && c != '_')
        {
            key.push_back(c);
        }
    }
    return key;
}

std::string
RemoveBottleneckIndex(std::string value)
{
    value = Lower(value);
    const auto begin = value.find("bottlenecks[");
    if (begin == std::string::npos)
    {
        return value;
    }
    const auto close = value.find(']', begin);
    if (close == std::string::npos)
    {
        return value;
    }
    const auto bracketBegin = begin + std::string("bottlenecks").size();
    value.erase(bracketBegin, close - bracketBegin + 1);
    return value;
}

std::optional<uint32_t>
ReadBottleneckIndexFromParameter(std::string value)
{
    value = Lower(value);
    const auto begin = value.find("bottlenecks[");
    if (begin == std::string::npos)
    {
        return std::nullopt;
    }
    const auto indexBegin = begin + std::string("bottlenecks[").size();
    const auto indexEnd = value.find(']', indexBegin);
    if (indexEnd == std::string::npos)
    {
        NS_ABORT_MSG("Malformed bottleneck sweep parameter: " << value);
    }
    const auto indexText = value.substr(indexBegin, indexEnd - indexBegin);
    NS_ABORT_MSG_UNLESS(!indexText.empty(), "Empty bottleneck index in sweep parameter: " << value);
    return static_cast<uint32_t>(std::stoul(indexText));
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

Time
ParseTimeValue(const std::string& value)
{
    const auto lower = Lower(value);
    if (lower == "0" || lower == "0s" || lower == "0ms" || lower == "0us" ||
        lower == "0ns")
    {
        return Seconds(0);
    }
    return Time(value);
}

bool
ParseBoolValue(const YAML::Node& node)
{
    if (node.IsScalar())
    {
        const auto lower = Lower(node.as<std::string>());
        if (lower == "1" || lower == "true" || lower == "on" || lower == "yes")
        {
            return true;
        }
        if (lower == "0" || lower == "false" || lower == "off" || lower == "no")
        {
            return false;
        }
    }
    return node.as<bool>();
}

void
ReadOptionalString(const YAML::Node& node, const char* key, std::string& value)
{
    if (node && node[key])
    {
        value = node[key].as<std::string>();
    }
}

void
ReadOptionalBool(const YAML::Node& node, const char* key, bool& value)
{
    if (node && node[key])
    {
        value = node[key].as<bool>();
    }
}

void
ReadOptionalUint32(const YAML::Node& node, const char* key, uint32_t& value)
{
    if (node && node[key])
    {
        value = node[key].as<uint32_t>();
    }
}

void
ReadOptionalTime(const YAML::Node& node, const char* key, Time& value)
{
    if (node && node[key])
    {
        value = ParseTimeValue(node[key].as<std::string>());
    }
}

void
ReadOptionalDataRate(const YAML::Node& node, const char* key, DataRate& value)
{
    if (node && node[key])
    {
        value = DataRate(node[key].as<std::string>());
    }
}

void
ReadOptionalQueueSize(const YAML::Node& node, const char* key, QueueSize& value)
{
    if (node && node[key])
    {
        value = QueueSize(node[key].as<std::string>());
    }
}

TcpAqmBottleneckConfig
ReadBottleneck(const YAML::Node& node, const TcpAqmBottleneckConfig& defaults)
{
    TcpAqmBottleneckConfig config = defaults;
    ReadOptionalString(node, "name", config.name);
    ReadOptionalDataRate(node, "rate", config.rate);
    if (node && node["delay"])
    {
        const auto delay = node["delay"].as<std::string>();
        if (!delay.empty())
        {
            config.delay = ParseTimeValue(delay);
        }
    }
    if (node && node["queueType"])
    {
        const auto queueType = node["queueType"].as<std::string>();
        if (!queueType.empty())
        {
            config.queueType = ParseTcpAqmQueueDiscType(queueType);
        }
    }
    ReadOptionalBool(node, "trace", config.trace);
    return config;
}

TcpAqmTopologyConfig
ReadTopology(const YAML::Node& node)
{
    TcpAqmTopologyConfig config;
    if (!node)
    {
        return config;
    }

    if (node["kind"])
    {
        config.kind = ParseTcpAqmTopologyKind(node["kind"].as<std::string>());
    }
    ReadOptionalDataRate(node, "accessRate", config.accessRate);
    ReadOptionalTime(node, "accessDelay", config.accessDelay);
    ReadOptionalQueueSize(node, "deviceQueueSize", config.deviceQueueSize);

    if (node["bottlenecks"])
    {
        config.bottlenecks.clear();
        uint32_t index = 0;
        for (const auto& entry : node["bottlenecks"])
        {
            TcpAqmBottleneckConfig defaults;
            defaults.name = fmt::format("bottleneck-{}", index + 1);
            config.bottlenecks.push_back(ReadBottleneck(entry, defaults));
            ++index;
        }
    }

    return config;
}

TcpAqmSweepValue
ReadSweepValue(TcpAqmSweepParameter parameter, const YAML::Node& node)
{
    switch (parameter)
    {
    case TcpAqmSweepParameter::FIRST_TCP_TYPE:
    case TcpAqmSweepParameter::SECOND_TCP_TYPE:
        return ParseTcpAqmTcpType(node.as<std::string>());
    case TcpAqmSweepParameter::QUEUE_TYPE:
    case TcpAqmSweepParameter::BOTTLENECK_QUEUE_TYPE:
        return ParseTcpAqmQueueDiscType(node.as<std::string>());
    case TcpAqmSweepParameter::BASE_RTT:
    case TcpAqmSweepParameter::CE_THRESHOLD:
    case TcpAqmSweepParameter::STOP_TIME:
    case TcpAqmSweepParameter::FIRST_START_TIME:
    case TcpAqmSweepParameter::SECOND_START_TIME:
    case TcpAqmSweepParameter::SECOND_START_JITTER:
    case TcpAqmSweepParameter::FIRST_START_JITTER:
    case TcpAqmSweepParameter::ACCESS_DELAY:
    case TcpAqmSweepParameter::BOTTLENECK_DELAY:
        return ParseTimeValue(node.as<std::string>());
    case TcpAqmSweepParameter::LINK_RATE:
    case TcpAqmSweepParameter::ACCESS_RATE:
    case TcpAqmSweepParameter::BOTTLENECK_RATE:
        return DataRate(node.as<std::string>());
    case TcpAqmSweepParameter::QUEUE_USE_ECN:
        return ParseBoolValue(node);
    case TcpAqmSweepParameter::RNG_RUN:
        return node.as<uint32_t>();
    case TcpAqmSweepParameter::TOPOLOGY_KIND:
        return ParseTcpAqmTopologyKind(node.as<std::string>());
    case TcpAqmSweepParameter::DEVICE_QUEUE_SIZE:
        return QueueSize(node.as<std::string>());
    }
    NS_ABORT_MSG("Unhandled TCP/AQM sweep parameter");
    return uint32_t{0};
}

TcpAqmSweepConfig
ReadSweep(const YAML::Node& node)
{
    TcpAqmSweepConfig config;
    if (!node)
    {
        return config;
    }

    config.enabled =
        node["enabled"] ? node["enabled"].as<bool>() : static_cast<bool>(node["parameter"]);
    ReadOptionalString(node, "name", config.name);
    if (node["bottleneckIndex"])
    {
        config.bottleneckIndex = node["bottleneckIndex"].as<uint32_t>();
    }
    if (node["parameter"])
    {
        const auto parameterText = node["parameter"].as<std::string>();
        const auto indexedBottleneck = ReadBottleneckIndexFromParameter(parameterText);
        if (indexedBottleneck.has_value())
        {
            config.bottleneckIndex = *indexedBottleneck;
        }
        config.parameter = ParseTcpAqmSweepParameter(RemoveBottleneckIndex(parameterText));
    }
    if (node["values"])
    {
        NS_ABORT_MSG_UNLESS(node["values"].IsSequence(), "sweep.values must be a sequence");
        NS_ABORT_MSG_UNLESS(config.parameter.has_value(),
                            "sweep.parameter must be set before sweep.values can be parsed");
        for (const auto& value : node["values"])
        {
            config.values.push_back(ReadSweepValue(*config.parameter, value));
        }
    }
    return config;
}

TcpAqmAnalysisConfig
ReadAnalysis(const YAML::Node& node)
{
    TcpAqmAnalysisConfig config;
    if (!node)
    {
        return config;
    }
    if (node["metrics"])
    {
        NS_ABORT_MSG_UNLESS(node["metrics"].IsSequence(), "analysis.metrics must be a sequence");
        for (const auto& metric : node["metrics"])
        {
            config.metrics.push_back(ParseTcpAqmAnalysisMetric(metric.as<std::string>()));
        }
    }
    return config;
}

} // namespace

TcpAqmTcpType
ParseTcpAqmTcpType(const std::string& value)
{
    const auto lower = Lower(value);
    if (lower == "reno" || lower == "linuxreno" || lower == "tcplinuxreno" ||
        lower == "ns3::tcplinuxreno")
    {
        return TcpAqmTcpType::LINUX_RENO;
    }
    if (lower == "newreno" || lower == "tcpnewreno" || lower == "ns3::tcpnewreno")
    {
        return TcpAqmTcpType::NEW_RENO;
    }
    if (lower == "cubic" || lower == "tcpcubic" || lower == "ns3::tcpcubic")
    {
        return TcpAqmTcpType::CUBIC;
    }
    if (lower == "dctcp" || lower == "tcpdctcp" || lower == "ns3::tcpdctcp")
    {
        return TcpAqmTcpType::DCTCP;
    }
    if (lower == "bbr" || lower == "tcpbbr" || lower == "ns3::tcpbbr")
    {
        return TcpAqmTcpType::BBR;
    }
    if (lower == "bic" || lower == "tcpbic" || lower == "ns3::tcpbic")
    {
        return TcpAqmTcpType::BIC;
    }
    if (lower == "yeah" || lower == "tcpyeah" || lower == "ns3::tcpyeah")
    {
        return TcpAqmTcpType::YEAH;
    }
    if (lower == "vegas" || lower == "tcpvegas" || lower == "ns3::tcpvegas")
    {
        return TcpAqmTcpType::VEGAS;
    }
    if (lower == "veno" || lower == "tcpveno" || lower == "ns3::tcpveno")
    {
        return TcpAqmTcpType::VENO;
    }
    if (lower == "westwoodplus" || lower == "tcpwestwoodplus" ||
        lower == "ns3::tcpwestwoodplus")
    {
        return TcpAqmTcpType::WESTWOOD_PLUS;
    }
    if (lower == "illinois" || lower == "tcpillinois" || lower == "ns3::tcpillinois")
    {
        return TcpAqmTcpType::ILLINOIS;
    }
    if (lower == "highspeed" || lower == "tcphighspeed" || lower == "ns3::tcphighspeed")
    {
        return TcpAqmTcpType::HIGH_SPEED;
    }
    if (lower == "hybla" || lower == "tcphybla" || lower == "ns3::tcphybla")
    {
        return TcpAqmTcpType::HYBLA;
    }
    if (lower == "htcp" || lower == "tcphtcp" || lower == "ns3::tcphtcp")
    {
        return TcpAqmTcpType::HTCP;
    }
    if (lower == "scalable" || lower == "tcpscalable" || lower == "ns3::tcpscalable")
    {
        return TcpAqmTcpType::SCALABLE;
    }
    if (lower == "ledbat" || lower == "tcpledbat" || lower == "ns3::tcpledbat")
    {
        return TcpAqmTcpType::LEDBAT;
    }
    if (lower == "lp" || lower == "tcplp" || lower == "ns3::tcplp")
    {
        return TcpAqmTcpType::LP;
    }
    NS_ABORT_MSG("Unsupported TCP/AQM TCP type: " << value);
    return TcpAqmTcpType::CUBIC;
}

std::string
TcpAqmTcpTypeToString(TcpAqmTcpType type)
{
    switch (type)
    {
    case TcpAqmTcpType::LINUX_RENO:
        return "reno";
    case TcpAqmTcpType::NEW_RENO:
        return "newreno";
    case TcpAqmTcpType::CUBIC:
        return "cubic";
    case TcpAqmTcpType::DCTCP:
        return "dctcp";
    case TcpAqmTcpType::BBR:
        return "bbr";
    case TcpAqmTcpType::BIC:
        return "bic";
    case TcpAqmTcpType::YEAH:
        return "yeah";
    case TcpAqmTcpType::VEGAS:
        return "vegas";
    case TcpAqmTcpType::VENO:
        return "veno";
    case TcpAqmTcpType::WESTWOOD_PLUS:
        return "westwoodplus";
    case TcpAqmTcpType::ILLINOIS:
        return "illinois";
    case TcpAqmTcpType::HIGH_SPEED:
        return "highspeed";
    case TcpAqmTcpType::HYBLA:
        return "hybla";
    case TcpAqmTcpType::HTCP:
        return "htcp";
    case TcpAqmTcpType::SCALABLE:
        return "scalable";
    case TcpAqmTcpType::LEDBAT:
        return "ledbat";
    case TcpAqmTcpType::LP:
        return "lp";
    }
    NS_ABORT_MSG("Unhandled TCP/AQM TCP type");
    return "cubic";
}

std::string
TcpAqmTcpTypeIdName(TcpAqmTcpType type)
{
    switch (type)
    {
    case TcpAqmTcpType::LINUX_RENO:
        return "ns3::TcpLinuxReno";
    case TcpAqmTcpType::NEW_RENO:
        return "ns3::TcpNewReno";
    case TcpAqmTcpType::CUBIC:
        return "ns3::TcpCubic";
    case TcpAqmTcpType::DCTCP:
        return "ns3::TcpDctcp";
    case TcpAqmTcpType::BBR:
        return "ns3::TcpBbr";
    case TcpAqmTcpType::BIC:
        return "ns3::TcpBic";
    case TcpAqmTcpType::YEAH:
        return "ns3::TcpYeah";
    case TcpAqmTcpType::VEGAS:
        return "ns3::TcpVegas";
    case TcpAqmTcpType::VENO:
        return "ns3::TcpVeno";
    case TcpAqmTcpType::WESTWOOD_PLUS:
        return "ns3::TcpWestwoodPlus";
    case TcpAqmTcpType::ILLINOIS:
        return "ns3::TcpIllinois";
    case TcpAqmTcpType::HIGH_SPEED:
        return "ns3::TcpHighSpeed";
    case TcpAqmTcpType::HYBLA:
        return "ns3::TcpHybla";
    case TcpAqmTcpType::HTCP:
        return "ns3::TcpHtcp";
    case TcpAqmTcpType::SCALABLE:
        return "ns3::TcpScalable";
    case TcpAqmTcpType::LEDBAT:
        return "ns3::TcpLedbat";
    case TcpAqmTcpType::LP:
        return "ns3::TcpLp";
    }
    NS_ABORT_MSG("Unhandled TCP/AQM TCP type");
    return "ns3::TcpCubic";
}

TcpAqmSweepParameter
ParseTcpAqmSweepParameter(const std::string& value)
{
    const auto key = ParameterKey(value);
    if (key == "experiment.firsttcptype" || key == "firsttcptype")
    {
        return TcpAqmSweepParameter::FIRST_TCP_TYPE;
    }
    if (key == "experiment.secondtcptype" || key == "secondtcptype")
    {
        return TcpAqmSweepParameter::SECOND_TCP_TYPE;
    }
    if (key == "experiment.queuetype" || key == "queuetype")
    {
        return TcpAqmSweepParameter::QUEUE_TYPE;
    }
    if (key == "experiment.basertt" || key == "basertt")
    {
        return TcpAqmSweepParameter::BASE_RTT;
    }
    if (key == "experiment.cethreshold" || key == "cethreshold")
    {
        return TcpAqmSweepParameter::CE_THRESHOLD;
    }
    if (key == "experiment.linkrate" || key == "linkrate")
    {
        return TcpAqmSweepParameter::LINK_RATE;
    }
    if (key == "experiment.stoptime" || key == "stoptime")
    {
        return TcpAqmSweepParameter::STOP_TIME;
    }
    if (key == "experiment.firststarttime" || key == "firststarttime")
    {
        return TcpAqmSweepParameter::FIRST_START_TIME;
    }
    if (key == "experiment.secondstarttime" || key == "secondstarttime")
    {
        return TcpAqmSweepParameter::SECOND_START_TIME;
    }
    if (key == "experiment.secondstartjitter" || key == "secondstartjitter")
    {
        return TcpAqmSweepParameter::SECOND_START_JITTER;
    }
    if (key == "experiment.firststartjitter" || key == "firststartjitter")
    {
        return TcpAqmSweepParameter::FIRST_START_JITTER;
    }
    if (key == "experiment.queueuseecn" || key == "queueuseecn" || key == "ecn")
    {
        return TcpAqmSweepParameter::QUEUE_USE_ECN;
    }
    if (key == "experiment.rngrun" || key == "rngrun")
    {
        return TcpAqmSweepParameter::RNG_RUN;
    }
    if (key == "topology.kind" || key == "topologykind")
    {
        return TcpAqmSweepParameter::TOPOLOGY_KIND;
    }
    if (key == "topology.accessrate" || key == "accessrate")
    {
        return TcpAqmSweepParameter::ACCESS_RATE;
    }
    if (key == "topology.accessdelay" || key == "accessdelay")
    {
        return TcpAqmSweepParameter::ACCESS_DELAY;
    }
    if (key == "topology.devicequeuesize" || key == "devicequeuesize")
    {
        return TcpAqmSweepParameter::DEVICE_QUEUE_SIZE;
    }
    if (key == "topology.bottlenecks.rate" || key == "bottleneckrate")
    {
        return TcpAqmSweepParameter::BOTTLENECK_RATE;
    }
    if (key == "topology.bottlenecks.delay" || key == "bottleneckdelay")
    {
        return TcpAqmSweepParameter::BOTTLENECK_DELAY;
    }
    if (key == "topology.bottlenecks.queuetype" || key == "bottleneckqueuetype")
    {
        return TcpAqmSweepParameter::BOTTLENECK_QUEUE_TYPE;
    }
    NS_ABORT_MSG("Unsupported TCP/AQM sweep parameter: " << value);
    return TcpAqmSweepParameter::BASE_RTT;
}

std::string
TcpAqmSweepParameterToString(TcpAqmSweepParameter parameter)
{
    switch (parameter)
    {
    case TcpAqmSweepParameter::FIRST_TCP_TYPE:
        return "experiment.firstTcpType";
    case TcpAqmSweepParameter::SECOND_TCP_TYPE:
        return "experiment.secondTcpType";
    case TcpAqmSweepParameter::QUEUE_TYPE:
        return "experiment.queueType";
    case TcpAqmSweepParameter::BASE_RTT:
        return "experiment.baseRtt";
    case TcpAqmSweepParameter::CE_THRESHOLD:
        return "experiment.ceThreshold";
    case TcpAqmSweepParameter::LINK_RATE:
        return "experiment.linkRate";
    case TcpAqmSweepParameter::STOP_TIME:
        return "experiment.stopTime";
    case TcpAqmSweepParameter::FIRST_START_TIME:
        return "experiment.firstStartTime";
    case TcpAqmSweepParameter::SECOND_START_TIME:
        return "experiment.secondStartTime";
    case TcpAqmSweepParameter::SECOND_START_JITTER:
        return "experiment.secondStartJitter";
    case TcpAqmSweepParameter::FIRST_START_JITTER:
        return "experiment.firstStartJitter";
    case TcpAqmSweepParameter::QUEUE_USE_ECN:
        return "experiment.queueUseEcn";
    case TcpAqmSweepParameter::RNG_RUN:
        return "experiment.rngRun";
    case TcpAqmSweepParameter::TOPOLOGY_KIND:
        return "topology.kind";
    case TcpAqmSweepParameter::ACCESS_RATE:
        return "topology.accessRate";
    case TcpAqmSweepParameter::ACCESS_DELAY:
        return "topology.accessDelay";
    case TcpAqmSweepParameter::DEVICE_QUEUE_SIZE:
        return "topology.deviceQueueSize";
    case TcpAqmSweepParameter::BOTTLENECK_RATE:
        return "topology.bottlenecks.rate";
    case TcpAqmSweepParameter::BOTTLENECK_DELAY:
        return "topology.bottlenecks.delay";
    case TcpAqmSweepParameter::BOTTLENECK_QUEUE_TYPE:
        return "topology.bottlenecks.queueType";
    }
    NS_ABORT_MSG("Unhandled TCP/AQM sweep parameter");
    return "experiment.baseRtt";
}

bool
TcpAqmSweepParameterUsesBottleneckIndex(TcpAqmSweepParameter parameter)
{
    return parameter == TcpAqmSweepParameter::BOTTLENECK_RATE ||
           parameter == TcpAqmSweepParameter::BOTTLENECK_DELAY ||
           parameter == TcpAqmSweepParameter::BOTTLENECK_QUEUE_TYPE;
}

TcpAqmAnalysisMetric
ParseTcpAqmAnalysisMetric(const std::string& value)
{
    const auto key = MetricKey(value);
    if (key == "throughput_mean_mbps")
    {
        return TcpAqmAnalysisMetric::THROUGHPUT_MEAN_MBPS;
    }
    if (key == "throughput_p05_mbps")
    {
        return TcpAqmAnalysisMetric::THROUGHPUT_P05_MBPS;
    }
    if (key == "throughput_p95_mbps")
    {
        return TcpAqmAnalysisMetric::THROUGHPUT_P95_MBPS;
    }
    if (key == "second_throughput_mean_mbps")
    {
        return TcpAqmAnalysisMetric::SECOND_THROUGHPUT_MEAN_MBPS;
    }
    if (key == "total_throughput_mean_mbps")
    {
        return TcpAqmAnalysisMetric::TOTAL_THROUGHPUT_MEAN_MBPS;
    }
    if (key == "jain_fairness")
    {
        return TcpAqmAnalysisMetric::JAIN_FAIRNESS;
    }
    if (key == "ping_rtt_mean_ms")
    {
        return TcpAqmAnalysisMetric::PING_RTT_MEAN_MS;
    }
    if (key == "ping_rtt_p95_ms")
    {
        return TcpAqmAnalysisMetric::PING_RTT_P95_MS;
    }
    if (key == "queue_delay_mean_ms")
    {
        return TcpAqmAnalysisMetric::QUEUE_DELAY_MEAN_MS;
    }
    if (key == "queue_delay_p95_ms")
    {
        return TcpAqmAnalysisMetric::QUEUE_DELAY_P95_MS;
    }
    if (key == "queue_delay_max_ms")
    {
        return TcpAqmAnalysisMetric::QUEUE_DELAY_MAX_MS;
    }
    if (key == "cwnd_mean_segments")
    {
        return TcpAqmAnalysisMetric::CWND_MEAN_SEGMENTS;
    }
    if (key == "drop_count")
    {
        return TcpAqmAnalysisMetric::DROP_COUNT;
    }
    if (key == "mark_count")
    {
        return TcpAqmAnalysisMetric::MARK_COUNT;
    }
    if (key == "elapsed_wall_seconds")
    {
        return TcpAqmAnalysisMetric::ELAPSED_WALL_SECONDS;
    }
    NS_ABORT_MSG("Unsupported TCP/AQM analysis metric: " << value);
    return TcpAqmAnalysisMetric::THROUGHPUT_MEAN_MBPS;
}

std::string
TcpAqmAnalysisMetricToString(TcpAqmAnalysisMetric metric)
{
    switch (metric)
    {
    case TcpAqmAnalysisMetric::THROUGHPUT_MEAN_MBPS:
        return "throughput_mean_mbps";
    case TcpAqmAnalysisMetric::THROUGHPUT_P05_MBPS:
        return "throughput_p05_mbps";
    case TcpAqmAnalysisMetric::THROUGHPUT_P95_MBPS:
        return "throughput_p95_mbps";
    case TcpAqmAnalysisMetric::SECOND_THROUGHPUT_MEAN_MBPS:
        return "second_throughput_mean_mbps";
    case TcpAqmAnalysisMetric::TOTAL_THROUGHPUT_MEAN_MBPS:
        return "total_throughput_mean_mbps";
    case TcpAqmAnalysisMetric::JAIN_FAIRNESS:
        return "jain_fairness";
    case TcpAqmAnalysisMetric::PING_RTT_MEAN_MS:
        return "ping_rtt_mean_ms";
    case TcpAqmAnalysisMetric::PING_RTT_P95_MS:
        return "ping_rtt_p95_ms";
    case TcpAqmAnalysisMetric::QUEUE_DELAY_MEAN_MS:
        return "queue_delay_mean_ms";
    case TcpAqmAnalysisMetric::QUEUE_DELAY_P95_MS:
        return "queue_delay_p95_ms";
    case TcpAqmAnalysisMetric::QUEUE_DELAY_MAX_MS:
        return "queue_delay_max_ms";
    case TcpAqmAnalysisMetric::CWND_MEAN_SEGMENTS:
        return "cwnd_mean_segments";
    case TcpAqmAnalysisMetric::DROP_COUNT:
        return "drop_count";
    case TcpAqmAnalysisMetric::MARK_COUNT:
        return "mark_count";
    case TcpAqmAnalysisMetric::ELAPSED_WALL_SECONDS:
        return "elapsed_wall_seconds";
    }
    NS_ABORT_MSG("Unhandled TCP/AQM analysis metric");
    return "throughput_mean_mbps";
}

std::string
TcpAqmSweepValueToString(const TcpAqmSweepValue& value)
{
    return std::visit(
        [](const auto& entry) -> std::string {
            using ValueType = std::decay_t<decltype(entry)>;
            if constexpr (std::is_same_v<ValueType, TcpAqmTcpType>)
            {
                return TcpAqmTcpTypeToString(entry);
            }
            else if constexpr (std::is_same_v<ValueType, TcpAqmQueueDiscType>)
            {
                return TcpAqmQueueDiscTypeToString(entry);
            }
            else if constexpr (std::is_same_v<ValueType, TcpAqmTopologyKind>)
            {
                return TcpAqmTopologyKindToString(entry);
            }
            else if constexpr (std::is_same_v<ValueType, Time>)
            {
                return TimeToString(entry);
            }
            else if constexpr (std::is_same_v<ValueType, DataRate>)
            {
                return DataRateToString(entry);
            }
            else if constexpr (std::is_same_v<ValueType, QueueSize>)
            {
                return QueueSizeToString(entry);
            }
            else if constexpr (std::is_same_v<ValueType, bool>)
            {
                return entry ? "true" : "false";
            }
            else
            {
                return fmt::format("{}", entry);
            }
        },
        value);
}

void
TcpAqmSweepConfig::Validate() const
{
    if (!enabled)
    {
        return;
    }
    NS_ABORT_MSG_UNLESS(parameter.has_value(), "enabled sweep requires sweep.parameter");
    NS_ABORT_MSG_UNLESS(!values.empty(), "enabled sweep requires at least one sweep value");
}

std::string
TcpAqmSweepConfig::ToString() const
{
    fmt::memory_buffer buffer;
    fmt::format_to(std::back_inserter(buffer), "sweep.enabled={}\n", enabled);
    if (!enabled)
    {
        return fmt::to_string(buffer);
    }
    fmt::format_to(std::back_inserter(buffer),
                   "sweep.name={}\n"
                   "sweep.parameter={}\n",
                   name,
                   parameter.has_value() ? TcpAqmSweepParameterToString(*parameter) : "");
    if (parameter.has_value() && TcpAqmSweepParameterUsesBottleneckIndex(*parameter))
    {
        fmt::format_to(std::back_inserter(buffer),
                       "sweep.bottleneckIndex={}\n",
                       bottleneckIndex);
    }
    for (uint32_t index = 0; index < values.size(); ++index)
    {
        fmt::format_to(std::back_inserter(buffer),
                       "sweep.values[{}]={}\n",
                       index,
                       TcpAqmSweepValueToString(values[index]));
    }
    return fmt::to_string(buffer);
}

void
TcpAqmAnalysisConfig::Validate() const
{
    std::set<TcpAqmAnalysisMetric> seen;
    for (const auto metric : metrics)
    {
        NS_ABORT_MSG_UNLESS(seen.insert(metric).second,
                            "analysis.metrics contains duplicate metric "
                                << TcpAqmAnalysisMetricToString(metric));
    }
}

std::string
TcpAqmAnalysisConfig::ToString() const
{
    fmt::memory_buffer buffer;
    for (uint32_t index = 0; index < metrics.size(); ++index)
    {
        fmt::format_to(std::back_inserter(buffer),
                       "analysis.metrics[{}]={}\n",
                       index,
                       TcpAqmAnalysisMetricToString(metrics[index]));
    }
    return fmt::to_string(buffer);
}

TcpAqmExperimentConfig
TcpAqmExperimentConfig::LoadFromFile(const std::string& path)
{
    TcpAqmExperimentConfig config;
    const YAML::Node root = YAML::LoadFile(path);

    const YAML::Node experiment = root["experiment"] ? root["experiment"] : root;
    if (experiment["firstTcpType"])
    {
        config.firstTcpType = ParseTcpAqmTcpType(experiment["firstTcpType"].as<std::string>());
    }
    if (experiment["secondTcpType"])
    {
        const auto secondTcpType = experiment["secondTcpType"].as<std::string>();
        if (!secondTcpType.empty())
        {
            config.secondTcpType = ParseTcpAqmTcpType(secondTcpType);
        }
    }
    if (experiment["queueType"])
    {
        config.queueType = ParseTcpAqmQueueDiscType(experiment["queueType"].as<std::string>());
    }
    ReadOptionalTime(experiment, "baseRtt", config.baseRtt);
    ReadOptionalTime(experiment, "ceThreshold", config.ceThreshold);
    ReadOptionalDataRate(experiment, "linkRate", config.linkRate);
    ReadOptionalTime(experiment, "stopTime", config.stopTime);
    ReadOptionalTime(experiment, "firstStartTime", config.firstStartTime);
    ReadOptionalTime(experiment, "secondStartTime", config.secondStartTime);
    ReadOptionalTime(experiment, "secondStartJitter", config.secondStartJitter);
    ReadOptionalTime(experiment, "firstStartJitter", config.firstStartJitter);
    ReadOptionalBool(experiment, "queueUseEcn", config.queueUseEcn);
    ReadOptionalBool(experiment, "enablePcap", config.enablePcap);
    ReadOptionalUint32(experiment, "rngRun", config.rngRun);

    config.topology = ReadTopology(root["topology"]);
    config.sweep = ReadSweep(root["sweep"]);
    config.analysis = ReadAnalysis(root["analysis"]);
    config.Validate(false);
    return config;
}

void
TcpAqmExperimentConfig::Validate(bool requireResolvedTopology) const
{
    NS_ABORT_MSG_UNLESS(!baseRtt.IsNegative(), "baseRtt must not be negative");
    NS_ABORT_MSG_UNLESS(!ceThreshold.IsNegative(), "ceThreshold must not be negative");
    NS_ABORT_MSG_UNLESS(linkRate.GetBitRate() > 0, "linkRate must be greater than zero");
    NS_ABORT_MSG_UNLESS(stopTime > Seconds(0), "stopTime must be greater than zero");
    NS_ABORT_MSG_UNLESS(firstStartTime.IsPositive(), "firstStartTime must not be negative");
    NS_ABORT_MSG_UNLESS(secondStartTime.IsPositive(), "secondStartTime must not be negative");
    NS_ABORT_MSG_UNLESS(secondStartJitter.IsPositive(), "secondStartJitter must not be negative");
    NS_ABORT_MSG_UNLESS(firstStartJitter.IsPositive(), "firstStartJitter must not be negative");
    NS_ABORT_MSG_UNLESS(firstStartTime + firstStartJitter < stopTime,
                        "firstStartTime plus firstStartJitter must be before stopTime");
    NS_ABORT_MSG_UNLESS(firstStartTime < stopTime, "firstStartTime must be before stopTime");
    if (secondTcpType.has_value())
    {
        NS_ABORT_MSG_UNLESS(secondStartTime < stopTime, "secondStartTime must be before stopTime");
        NS_ABORT_MSG_UNLESS(secondStartTime + secondStartJitter < stopTime,
                            "secondStartTime plus secondStartJitter must be before stopTime");
    }
    NS_ABORT_MSG_UNLESS(rngRun > 0, "rngRun must be greater than zero");
    topology.Validate(requireResolvedTopology);
    sweep.Validate();
    if (sweep.enabled && sweep.parameter.has_value() &&
        TcpAqmSweepParameterUsesBottleneckIndex(*sweep.parameter))
    {
        NS_ABORT_MSG_UNLESS(sweep.bottleneckIndex < topology.bottlenecks.size(),
                            "sweep.bottleneckIndex " << sweep.bottleneckIndex
                                                     << " is outside topology bottleneck count "
                                                     << topology.bottlenecks.size());
    }
    analysis.Validate();
}

std::string
TcpAqmExperimentConfig::ToString() const
{
    return fmt::format("experiment.firstTcpType={}\n"
                       "experiment.secondTcpType={}\n"
                       "experiment.queueType={}\n"
                       "experiment.baseRtt={}\n"
                       "experiment.ceThreshold={}\n"
                       "experiment.linkRate={}\n"
                       "experiment.stopTime={}\n"
                       "experiment.firstStartTime={}\n"
                       "experiment.secondStartTime={}\n"
                       "experiment.secondStartJitter={}\n"
                       "experiment.firstStartJitter={}\n"
                       "experiment.queueUseEcn={}\n"
                       "experiment.enablePcap={}\n"
                       "experiment.rngRun={}\n"
                       "{}",
                       TcpAqmTcpTypeToString(firstTcpType),
                       secondTcpType.has_value() ? TcpAqmTcpTypeToString(*secondTcpType) : "",
                       TcpAqmQueueDiscTypeToString(queueType),
                       TimeToString(baseRtt),
                       TimeToString(ceThreshold),
                       DataRateToString(linkRate),
                       TimeToString(stopTime),
                       TimeToString(firstStartTime),
                       TimeToString(secondStartTime),
                       TimeToString(secondStartJitter),
                       TimeToString(firstStartJitter),
                       queueUseEcn,
                       enablePcap,
                       rngRun,
                       topology.ToString() + sweep.ToString() + analysis.ToString());
}

std::ostream&
operator<<(std::ostream& os, const TcpAqmExperimentConfig& config)
{
    os << config.ToString();
    return os;
}

} // namespace ns3
