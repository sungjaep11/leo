/*
 * Copyright (c) 2020 ETH Zurich
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Simon               2020
 */

#include <map>
#include <iostream>
#include <fstream>
#include <string>
#include <ctime>
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <chrono>
#include <stdexcept>
#include <cmath>

#include "ns3/point-to-point-laser-net-device.h"
#include "ns3/error-model.h"
#include "ns3/random-variable-stream.h"

#include "ns3/basic-simulation.h"
#include "ns3/tcp-flow-scheduler.h"
#include "ns3/udp-burst-scheduler.h"
#include "ns3/pingmesh-scheduler.h"
#include "ns3/topology-satellite-network.h"
#include "ns3/tcp-optimizer.h"
#include "ns3/arbiter-single-forward-helper.h"
#include "ns3/ipv4-arbiter-routing-helper.h"
#include "ns3/gsl-if-bandwidth-helper.h"

using namespace ns3;

int main(int argc, char *argv[]) {

    // No buffering of printf
    setbuf(stdout, nullptr);

    // Retrieve run directory
    CommandLine cmd;
    std::string run_dir = "";
    cmd.Usage("Usage: ./ns3 run \"main_satnet --run_dir=<path/to/run/directory>\"");
    cmd.AddValue("run_dir",  "Run directory", run_dir);
    cmd.Parse(argc, argv);
    if (run_dir.compare("") == 0) {
        printf("Usage: ./ns3 run \"main_satnet --run_dir=<path/to/run/directory>\"");
        return 0;
    }

    // Load basic simulation environment
    Ptr<BasicSimulation> basicSimulation = CreateObject<BasicSimulation>(run_dir);

    // Setting socket type
    Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue ("ns3::" + basicSimulation->GetConfigParamOrFail("tcp_socket_type")));

    // Optimize TCP
    TcpOptimizer::OptimizeBasic(basicSimulation);

    // [ns-3.48 port] Optional pacing: config key tcp_enable_pacing (default false).
    // ns-3's TcpBbr expects pacing to be enabled.
    if (parse_boolean(basicSimulation->GetConfigParamOrDefault("tcp_enable_pacing", "false"))) {
        Config::SetDefault("ns3::TcpSocketState::EnablePacing", BooleanValue(true));
        printf("  > Pacing..................... enabled\n");
    }

    // Read topology, and install routing arbiters
    Ptr<TopologySatelliteNetwork> topology = CreateObject<TopologySatelliteNetwork>(basicSimulation, Ipv4ArbiterRoutingHelper());
    ArbiterSingleForwardHelper arbiterHelper(basicSimulation, topology->GetNodes());
    GslIfBandwidthHelper gslIfBandwidthHelper(basicSimulation, topology->GetNodes());

    // Optional independent packet loss on ISLs only (GSL devices are not touched).
    const double islPacketLossProbability = std::stod(
            basicSimulation->GetConfigParamOrDefault("isl_packet_loss_probability", "0.0"));
    if (!std::isfinite(islPacketLossProbability) || islPacketLossProbability < 0.0 || islPacketLossProbability > 1.0) {
        throw std::runtime_error("isl_packet_loss_probability must be between 0.0 and 1.0");
    }
    if (islPacketLossProbability > 0.0) {
        const int64_t baseStream = parse_positive_int64(basicSimulation->GetConfigParamOrFail("simulation_seed"));
        uint32_t attachedIslDevices = 0;
        const NodeContainer& satelliteNodes = topology->GetSatelliteNodes();
        for (uint32_t n = 0; n < satelliteNodes.GetN(); ++n) {
            Ptr<Node> node = satelliteNodes.Get(n);
            for (uint32_t d = 0; d < node->GetNDevices(); ++d) {
                Ptr<PointToPointLaserNetDevice> islDevice =
                        DynamicCast<PointToPointLaserNetDevice>(node->GetDevice(d));
                if (islDevice == nullptr) {
                    continue;
                }

                Ptr<RateErrorModel> errorModel = CreateObject<RateErrorModel>();
                errorModel->SetAttribute("ErrorRate", DoubleValue(islPacketLossProbability));
                errorModel->SetAttribute("ErrorUnit", EnumValue(RateErrorModel::ERROR_UNIT_PACKET));
                Ptr<UniformRandomVariable> randomVariable = CreateObject<UniformRandomVariable>();
                randomVariable->SetStream(baseStream + static_cast<int64_t>(node->GetId()) * 10000 + d);
                errorModel->SetAttribute("RanVar", PointerValue(randomVariable));
                islDevice->SetReceiveErrorModel(errorModel);
                ++attachedIslDevices;
            }
        }
        std::cout << "  > ISL packet loss............ " << (islPacketLossProbability * 100.0)
                  << "% per packet (" << attachedIslDevices << " directions)" << std::endl;
    }

    // Schedule flows
    TcpFlowScheduler tcpFlowScheduler(basicSimulation, topology); // Requires enable_tcp_flow_scheduler=true

    // Schedule UDP bursts
    UdpBurstScheduler udpBurstScheduler(basicSimulation, topology); // Requires enable_udp_burst_scheduler=true

    // Schedule pings
    PingmeshScheduler pingmeshScheduler(basicSimulation, topology); // Requires enable_pingmesh_scheduler=true

    // Run simulation
    basicSimulation->Run();

    // Write flow results
    tcpFlowScheduler.WriteResults();

    // Write UDP burst results
    udpBurstScheduler.WriteResults();

    // Write pingmesh results
    pingmeshScheduler.WriteResults();

    // Collect utilization statistics
    topology->CollectUtilizationStatistics();

    // Finalize the simulation
    basicSimulation->Finalize();

    return 0;

}
