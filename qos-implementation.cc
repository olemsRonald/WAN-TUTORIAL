/*
 * Exercise 2: Quality of Service Implementation for Mixed Traffic
 * Implements: Traffic Differentiation (Q1), Priority Queueing (Q2), 
 * Performance Measurement (Q3), and Congestion Scenario (Q4).
 * Topology: Triangular Mesh (n0, n1, n2) | Bottleneck link is n0 <-> n2 (5Mbps).
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h" 
#include "ns3/flow-monitor-module.h"    
#include <iomanip>                      // Required for std::setprecision

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("QoSImplementation");

const std::string LINK_DATA_RATE = "5Mbps"; // Link Capacity (Bottleneck)
const double SIMULATION_TIME = 15.0;       // Total Simulation Time

// --- Q2: Function to Configure and Install PfifoFastQueueDisc ---
void InstallQoS(Ptr<NetDevice> device)
{
    TrafficControlHelper tcHelper;

    // Use SetRootQueueDisc and configure via Attributes
    tcHelper.SetRootQueueDisc("ns3::PfifoFastQueueDisc",
                              "Bands", UintegerValue(3)); 
    
    // Install the queue disc on the device's output queue (TX side)
    tcHelper.Install(device);
    NS_LOG_INFO("QoS: PfifoFast installed on device " << device->GetNode()->GetId() << ":" << device->GetIfIndex());
}

// --- Metrics Collection using FlowMonitor ---
// FIX: The FlowMonitorHelper object (flowHelper) must be passed to retrieve the classifier
void CheckMetrics(Ptr<FlowMonitor> fm, FlowMonitorHelper* flowHelper) 
{
    std::cout << "\n--- Q3: QoS Performance Verification ---\n";

    double totalTxVoIP = 0, totalRxVoIP = 0, totalDelayVoIP = 0, totalJitterVoIP = 0;
    double totalTxFTP = 0, totalRxFTP = 0, totalDelayFTP = 0;

    // FIX: Retrieve the classifier directly from the FlowMonitorHelper object.
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper->GetClassifier());
    
    std::map<FlowId, FlowMonitor::FlowStats> stats = fm->GetFlowStats();

    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin(); i != stats.end(); ++i)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);

        // Classify the flows based on the source port (9=VoIP, 10=FTP)
        if (t.sourcePort == 9) // VoIP Flow
        {
            totalTxVoIP += i->second.txPackets;
            totalRxVoIP += i->second.rxPackets;
            totalDelayVoIP += i->second.delaySum.GetSeconds();
            totalJitterVoIP += i->second.jitterSum.GetSeconds();
        }
        else if (t.sourcePort == 10) // FTP Flow
        {
            totalTxFTP += i->second.txPackets;
            totalRxFTP += i->second.rxPackets;
            totalDelayFTP += i->second.delaySum.GetSeconds();
        }
    }

    // --- Metrics for VoIP (High Priority - DSCP EF) ---
    if (totalRxVoIP > 0)
    {
        double lossVoIP = (totalTxVoIP - totalRxVoIP) / totalTxVoIP * 100.0;
        double avgDelayVoIP = totalDelayVoIP / totalRxVoIP * 1000.0;
        double avgJitterVoIP = totalJitterVoIP / totalRxVoIP * 1000.0;

        std::cout << "VoIP (High Priority / DSCP EF):\n";
        std::cout << "  Packet Loss: " << std::fixed << std::setprecision(2) << lossVoIP << " % [Expected: Near 0%]\n";
        std::cout << "  Avg Latency: " << std::fixed << std::setprecision(2) << avgDelayVoIP << " ms [Expected: Low]\n";
        std::cout << "  Avg Jitter:  " << std::fixed << std::setprecision(2) << avgJitterVoIP << " ms [Expected: Low]\n";
    }

    // --- Metrics for FTP (Low Priority - DSCP BE) ---
    if (totalRxFTP > 0)
    {
        double lossFTP = (totalTxFTP - totalRxFTP) / totalTxFTP * 100.0;
        double throughputFTP = (totalRxFTP * 1500.0 * 8.0) / ((SIMULATION_TIME - 3.0) * 1000000.0);
        double avgDelayFTP = totalDelayFTP / totalRxFTP * 1000.0;

        std::cout << "\nFTP (Low Priority / DSCP BE):\n";
        std::cout << "  Packet Loss: " << std::fixed << std::setprecision(2) << lossFTP << " % [Expected: High]\n";
        std::cout << "  Avg Latency: " << std::fixed << std::setprecision(2) << avgDelayFTP << " ms [Expected: High]\n";
        std::cout << "  Throughput:  " << std::fixed << std::setprecision(2) << throughputFTP << " Mbps [Expected: Bottlenecked]\n";
    }
}

int main(int argc, char *argv[])
{
    // Setup logging
    LogComponentEnable("QoSImplementation", LOG_LEVEL_INFO);
    LogComponentEnable("OnOffApplication", LOG_LEVEL_INFO);
    LogComponentEnable("PfifoFastQueueDisc", LOG_LEVEL_INFO);
    
    // 1. Create Nodes (n0, n1, n2)
    NodeContainer nodes;
    nodes.Create(3);
    Ptr<Node> n0 = nodes.Get(0); 
    Ptr<Node> n2 = nodes.Get(2); // Destination

    // 2. Setup Links (Triangular Mesh)
    PointToPointHelper p2p;
    p2p.SetQueue("ns3::DropTailQueue", "MaxPackets", UintegerValue(100)); // Base Queue

    // Link 1 (HQ <-> Branch)
    p2p.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("1ms"));
    NodeContainer link1Nodes(n0, nodes.Get(1));
    NetDeviceContainer link1Devices = p2p.Install(link1Nodes);

    // Link 2 (Branch <-> DC)
    NodeContainer link2Nodes(nodes.Get(1), n2);
    NetDeviceContainer link2Devices = p2p.Install(link2Nodes);

    // Link 3 (HQ <-> DC) - THE BOTTLENECK LINK (Q4)
    p2p.SetDeviceAttribute("DataRate", StringValue(LINK_DATA_RATE)); 
    p2p.SetChannelAttribute("Delay", StringValue("10ms")); // High delay for congestion
    NodeContainer link3Nodes(n0, n2);
    NetDeviceContainer bottleneckDevices = p2p.Install(link3Nodes);

    // 3. Install Internet Stack
    InternetStackHelper stack;
    stack.Install(nodes);
    
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0"); address.Assign(link1Devices);
    address.SetBase("10.1.2.0", "255.255.255.0"); address.Assign(link2Devices);
    address.SetBase("10.1.3.0", "255.255.255.0"); 
    Ipv4InterfaceContainer interfaces3 = address.Assign(bottleneckDevices);

    // 4. Q2: Install QoS on the Bottleneck Link (HQ side - n0)
    InstallQoS(bottleneckDevices.Get(0));

    // 5. Setup Static Routing (Forces traffic through the bottleneck)
    Ipv4GlobalRoutingHelper::PopulateRoutingTables(); 
    
    // Correct call to GetRouting<T> 
    Ptr<Ipv4StaticRouting> n0Routing = Ipv4GlobalRoutingHelper::GetRouting<Ipv4StaticRouting>(n0->GetObject<Ipv4>()->GetObject<Ipv4RoutingProtocol>());
    
    // Add a route on n0: to reach 10.1.2.0/24 (via DC, next-hop 10.1.3.2)
    n0Routing->AddNetworkRouteTo(Ipv4Address("10.1.2.0"), Ipv4Mask("255.255.255.0"), 
                                 interfaces3.GetAddress(1), 3, 0); 

    // 6. Application Setup (VoIP/FTP)
    Ipv4Address sinkAddress = interfaces3.GetAddress(1); // 10.1.3.2 (DC's direct link IP)
    uint16_t voipPort = 9;
    uint16_t ftpPort = 10;
    
    PacketSinkHelper sink("ns3::UdpSocketFactory", InetSocketAddress(sinkAddress, voipPort));
    sink.SetAttribute("Protocol", TypeIdValue(UdpSocketFactory::GetTypeId()));
    sink.Install(n2).Start(Seconds(0.0));
    
    PacketSinkHelper sink2("ns3::UdpSocketFactory", InetSocketAddress(sinkAddress, ftpPort));
    sink2.SetAttribute("Protocol", TypeIdValue(UdpSocketFactory::GetTypeId()));
    sink2.Install(n2).Start(Seconds(0.0));

    // A. VoIP Traffic (High Priority - DSCP EF)
    OnOffHelper voipApp("ns3::UdpSocketFactory", InetSocketAddress(sinkAddress, voipPort));
    voipApp.SetAttribute("PacketSize", UintegerValue(200)); 
    voipApp.SetAttribute("DataRate", StringValue("2Mbps")); 
    voipApp.SetAttribute("ToS", UintegerValue(0x2e << 2)); // DSCP EF (101110)
    ApplicationContainer voipApps = voipApp.Install(n0);
    voipApps.Start(Seconds(1.0));
    
    // B. FTP Traffic (Low Priority - DSCP BE) - CONGESTION CAUSE
    OnOffHelper ftpApp("ns3::UdpSocketFactory", InetSocketAddress(sinkAddress, ftpPort));
    ftpApp.SetAttribute("PacketSize", UintegerValue(1500));
    ftpApp.SetAttribute("DataRate", StringValue("4Mbps")); 
    ftpApp.SetAttribute("ToS", UintegerValue(0x00)); // DSCP BE (000000)
    ApplicationContainer ftpApps = ftpApp.Install(n0);
    ftpApps.Start(Seconds(1.0));

    // FINAL FIX: Use SetStopTime on the specific application to schedule its termination.
    // This is the public method to control the running time of an application.
    voipApps.Get(0)->SetStopTime(Seconds(SIMULATION_TIME - 3.0));
    ftpApps.Get(0)->SetStopTime(Seconds(SIMULATION_TIME - 3.0));

    // 7. Q3: Flow Monitor Setup
    Ptr<FlowMonitor> flowMonitor;
    FlowMonitorHelper flowHelper;
    flowMonitor = flowHelper.InstallAll();
    
    // Schedule periodic check of metrics (Q3 Verification)
    Simulator::Schedule(Seconds(SIMULATION_TIME - 2.0), &CheckMetrics, flowMonitor, &flowHelper);

    // 8. Run Simulation
    Simulator::Stop(Seconds(SIMULATION_TIME));
    Simulator::Run();
    
    flowMonitor->CheckForLostPackets();
    Simulator::Destroy();
    return 0;
}
