/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef TCP_AQM_EXPERIMENT_CONFIG_H
#define TCP_AQM_EXPERIMENT_CONFIG_H

#include "tcp-aqm-topology-config.h"

#include "ns3/data-rate.h"
#include "ns3/nstime.h"

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace ns3
{

/**
 * Supported TCP congestion-control choices for declarative configs.
 */
enum class TcpAqmTcpType
{
    LINUX_RENO,
    NEW_RENO,
    CUBIC,
    DCTCP,
    BBR,
    BIC,
    YEAH,
    VEGAS,
    VENO,
    WESTWOOD_PLUS,
    ILLINOIS,
    HIGH_SPEED,
    HYBLA,
    HTCP,
    SCALABLE,
    LEDBAT,
    LP,
};

TcpAqmTcpType ParseTcpAqmTcpType(const std::string& value);
std::string TcpAqmTcpTypeToString(TcpAqmTcpType type);
std::string TcpAqmTcpTypeIdName(TcpAqmTcpType type);

/**
 * Supported one-factor sweep targets for declarative campaigns.
 */
enum class TcpAqmSweepParameter
{
    FIRST_TCP_TYPE,
    SECOND_TCP_TYPE,
    QUEUE_TYPE,
    BASE_RTT,
    CE_THRESHOLD,
    LINK_RATE,
    STOP_TIME,
    FIRST_START_TIME,
    SECOND_START_TIME,
    SECOND_START_JITTER,
    FIRST_START_JITTER,
    QUEUE_USE_ECN,
    RNG_RUN,
    TOPOLOGY_KIND,
    ACCESS_RATE,
    ACCESS_DELAY,
    DEVICE_QUEUE_SIZE,
    BOTTLENECK_RATE,
    BOTTLENECK_DELAY,
    BOTTLENECK_QUEUE_TYPE,
};

TcpAqmSweepParameter ParseTcpAqmSweepParameter(const std::string& value);
std::string TcpAqmSweepParameterToString(TcpAqmSweepParameter parameter);
bool TcpAqmSweepParameterUsesBottleneckIndex(TcpAqmSweepParameter parameter);

/**
 * Metrics that the analysis pipeline can prioritize for a configured sweep.
 */
enum class TcpAqmAnalysisMetric
{
    THROUGHPUT_MEAN_MBPS,
    THROUGHPUT_P05_MBPS,
    THROUGHPUT_P95_MBPS,
    SECOND_THROUGHPUT_MEAN_MBPS,
    TOTAL_THROUGHPUT_MEAN_MBPS,
    JAIN_FAIRNESS,
    PING_RTT_MEAN_MS,
    PING_RTT_P95_MS,
    QUEUE_DELAY_MEAN_MS,
    QUEUE_DELAY_P95_MS,
    QUEUE_DELAY_MAX_MS,
    CWND_MEAN_SEGMENTS,
    DROP_COUNT,
    MARK_COUNT,
    ELAPSED_WALL_SECONDS,
};

TcpAqmAnalysisMetric ParseTcpAqmAnalysisMetric(const std::string& value);
std::string TcpAqmAnalysisMetricToString(TcpAqmAnalysisMetric metric);

using TcpAqmSweepValue = std::variant<TcpAqmTcpType,
                                      TcpAqmQueueDiscType,
                                      TcpAqmTopologyKind,
                                      Time,
                                      DataRate,
                                      QueueSize,
                                      bool,
                                      uint32_t>;

std::string TcpAqmSweepValueToString(const TcpAqmSweepValue& value);

struct TcpAqmSweepConfig
{
    bool enabled{false};
    std::string name;
    std::optional<TcpAqmSweepParameter> parameter;
    uint32_t bottleneckIndex{0};
    std::vector<TcpAqmSweepValue> values;

    void Validate() const;
    std::string ToString() const;
};

struct TcpAqmAnalysisConfig
{
    std::vector<TcpAqmAnalysisMetric> metrics;

    void Validate() const;
    std::string ToString() const;
};

/**
 * Declarative experiment configuration for TCP/AQM benchmark examples.
 */
class TcpAqmExperimentConfig
{
  public:
    TcpAqmTcpType firstTcpType{TcpAqmTcpType::CUBIC};
    std::optional<TcpAqmTcpType> secondTcpType;
    TcpAqmQueueDiscType queueType{TcpAqmQueueDiscType::CODEL};
    Time baseRtt{MilliSeconds(80)};
    Time ceThreshold{MilliSeconds(1)};
    DataRate linkRate{DataRate("50Mbps")};
    Time stopTime{Seconds(70)};
    Time firstStartTime{Seconds(5)};
    Time secondStartTime{Seconds(15)};
    Time secondStartJitter{Seconds(0)};
    Time firstStartJitter{Seconds(0)};
    bool queueUseEcn{false};
    bool enablePcap{false};
    uint32_t rngRun{1};
    TcpAqmTopologyConfig topology;
    TcpAqmSweepConfig sweep;
    TcpAqmAnalysisConfig analysis;

    static TcpAqmExperimentConfig LoadFromFile(const std::string& path);

    void Validate(bool requireResolvedTopology = true) const;
    std::string ToString() const;
};

std::ostream& operator<<(std::ostream& os, const TcpAqmExperimentConfig& config);

} // namespace ns3

#endif // TCP_AQM_EXPERIMENT_CONFIG_H
