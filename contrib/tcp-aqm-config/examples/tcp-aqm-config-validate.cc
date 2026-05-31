/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ns3/command-line.h"
#include "ns3/tcp-aqm-experiment-config.h"

#include <iostream>
#include <string>

using namespace ns3;

int
main(int argc, char* argv[])
{
    std::string configFile = "contrib/tcp-aqm-config/configs/single-bottleneck.yaml";

    CommandLine cmd(__FILE__);
    cmd.AddValue("configFile", "YAML or JSON TCP/AQM benchmark configuration", configFile);
    cmd.Parse(argc, argv);

    auto config = TcpAqmExperimentConfig::LoadFromFile(configFile);
    config.topology.FillDerivedDelays(config.baseRtt);
    config.Validate();
    std::cout << config;
    return 0;
}
