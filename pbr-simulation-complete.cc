/*
 * Exercise 5: Policy-Based Routing Main Script (Self-Contained)
 * Topology: Studio (n0) -> Router (n1) -> Cloud (n2) via two parallel links (Primary/Secondary).
 * Implements: PBR routing logic directly in this file.
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4-route.h"
#include "ns3/log.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("PbrSimulationComplete");

// =================================================================
// PbrRouting Class Definition (Self-Contained)
// =================================================================
class PbrRouting : public Ipv4RoutingProtocol
{
public:
    PbrRouting(Ipv4Address videoNextHop, Ipv4Address dataNextHop,
               uint32_t videoIfIndex, uint32_t dataIfIndex)
    : m_videoNextHop(videoNextHop), 
      m_dataNextHop(dataNextHop), 
      m_videoIfIndex(videoIfIndex), 
      m_dataIfIndex(dataIfIndex)
    {}

    virtual ~PbrRouting() {}

    static TypeId GetTypeId(void) {
        static TypeId tid = TypeId("ns3::PbrRouting")
            .SetParent<Ipv4RoutingProtocol>()
            .SetGroupName("Internet");
        return tid;
    }

    // Required overrides
    virtual void SetIpv4(Ptr<Ipv4> ipv4) override { m_ipv4 = ipv4; }
    virtual void NotifyInterfaceUp(uint32_t interface) override {}
    virtual void NotifyInterfaceDown(uint32_t interface) override {}
    virtual void NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address) override {}
    virtual void NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address) override {}
    
    // Q2: Core PBR logic - using 'sockerr' instead of 'errno'
    virtual Ptr<Ipv4Route> RouteOutput(Ptr<Packet> p, const Ipv4Header& header, 
                                       Ptr<NetDevice> oif, Socket::SocketErrno& sockerr) override;
    
    // Correct signature for the second pure virtual function
    virtual bool RouteInput(Ptr<const Packet> p, const Ipv4Header& header, Ptr<const NetDevice> idev, 
                            UnicastForwardCallback ucb, MulticastForwardCallback mcb, 
                            LocalDeliverCallback lcb, ErrorCallback ecb) override;
                            
    virtual void PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit = Time::S) const override {
        *stream->GetStream() << "PbrRouting Table: Policy-Based Routing Active (DSCP EF -> Primary, DSCP BE -> Secondary)" << std::endl;
    }

private:
    Ptr<Ipv4> m_ipv4;
    Ipv4Address m_videoNextHop; 
    Ipv4Address m_dataNextHop;  
    uint32_t m_videoIfIndex;    
    uint32_t m_dataIfIndex;     
};

// =================================================================
// PbrRouting Implementation
// =================================================================

Ptr<Ipv4Route> PbrRouting::RouteOutput(Ptr<Packet> p, const Ipv4Header& header, 
                                       Ptr<NetDevice> oif, Socket::SocketErrno& sockerr)
{
    const uint8_t DSCP_VIDEO_EF = 0x2e; // Expedited Forwarding (VoIP/Video)
    const uint8_t DSCP_DATA_BE = 0x00;  // Best Effort (Data/FTP)

    // 1. Classification based on DSCP/TOS field
    uint8_t dscp = header.GetDscp(); 

    if (dscp == DSCP_VIDEO_EF) {
        // Policy: Video traffic (EF) uses the Primary path (Net 2)
        NS_LOG_INFO("PBR: Video traffic (EF), routing via Primary (Net 2)");
        Ptr<Ipv4Route> route = Create<Ipv4Route>();
        route->SetDestination(header.GetDestination());
        route->SetSource(m_ipv4->GetAddress(m_videoIfIndex, 0).GetLocal());
        route->SetGateway(m_videoNextHop); 
        route->SetOutputDevice(m_ipv4->GetNetDevice(m_videoIfIndex));
        return route;
    }
    else if (dscp == DSCP_DATA_BE) {
        // Policy: Data traffic (BE) uses the Secondary path (Net 3)
        NS_LOG_INFO("PBR: Data traffic (BE), routing via Secondary (Net 3)");
        Ptr<Ipv4Route> route = Create<Ipv4Route>();
        route->SetDestination(header.GetDestination());
        route->SetSource(m_ipv4->GetAddress(m_dataIfIndex, 0).GetLocal());
        route->SetGateway(m_dataNextHop); 
        route->SetOutputDevice(m_ipv4->GetNetDevice(m_dataIfIndex));
        return route;
    }
    
    // Fallback: Use the default routing logic
    NS_LOG_INFO("PBR: No match, deferring to underlying routing protocol.");
    // FIX: Use GetRoutingProtocol() instead of GetRoutingServices() for NS-3.35 compatibility
    return m_ipv4->GetRoutingProtocol()->RouteOutput(p, header, oif, sockerr); 
}

bool PbrRouting::RouteInput(Ptr<const Packet> p, const Ipv4Header& header, Ptr<const NetDevice> idev, 
                           UnicastForwardCallback ucb, MulticastForwardCallback mcb, 
                           LocalDeliverCallback lcb, ErrorCallback ecb)
{
    // Defer input routing decisions to the underlying global routing system
    // FIX: Use GetRoutingProtocol() instead of GetRoutingServices() for NS-3.35 compatibility
    return m_ipv4->GetRoutingProtocol()->RouteInput(p, header, idev, ucb, mcb, lcb, ecb);
}

// =================================================================
// Main Simulation Script
// =================================================================

int main(int argc, char *argv[])
{
    // Enable Logs for PBR decisions
    LogComponentEnable("PbrRouting", LOG_LEVEL_INFO);
    LogComponentEnable("OnOffApplication", LOG_LEVEL_INFO);

    // Topology: Studio (n0) -> Router (n1) -> Cloud (n2)
    NodeContainer nodes;
    nodes.Create(3);
    Ptr<Node> studio = nodes.Get(0);
    Ptr<Node> router = nodes.Get(1);
    Ptr<Node> cloud = nodes.Get(2);

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));

    // Link 1: Studio -> Router (10.0.1.0/24)
    NetDeviceContainer d0 = p2p.Install(studio, router);
    // Link 2: Router -> Cloud (Primary/Video) (10.0.2.0/24)
    NetDeviceContainer d1 = p2p.Install(router, cloud);
    // Link 3: Router -> Cloud (Secondary/Data) (10.0.3.0/24)
    NetDeviceContainer d2 = p2p.Install(router, cloud);

    // Install Stack
    InternetStackHelper internet;
    internet.Install(nodes);

    // Assign IPs
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.0.1.0", "255.255.255.0"); ipv4.Assign(d0); 
    ipv4.SetBase("10.0.2.0", "255.255.255.0"); Ipv4InterfaceContainer i1 = ipv4.Assign(d1); // Primary Link
    ipv4.SetBase("10.0.3.0", "255.255.255.0"); Ipv4InterfaceContainer i2 = ipv4.Assign(d2); // Secondary Link

    // --- Install PBR on Router (n1) ---
    Ipv4Address videoNextHop = i1.GetAddress(1); // 10.0.2.2 (Cloud IP on Primary)
    Ipv4Address dataNextHop = i2.GetAddress(1);  // 10.0.3.2 (Cloud IP on Secondary)
    
    // Interface Index: Router has 3 devices (d0, d1, d2). Indices are 1, 2, 3.
    Ptr<Ipv4> ipv4Router = router->GetObject<Ipv4>();
    
    // Create and configure the custom PbrRouting instance
    Ptr<PbrRouting> pbr = CreateObject<PbrRouting>(
        videoNextHop, 
        dataNextHop,  
        2,            // Interface Index for Video path (Net 2)
        3             // Interface Index for Data path (Net 3)
    );
    pbr->SetIpv4(ipv4Router);
    ipv4Router->SetRoutingProtocol(pbr); // Replace default routing with PBR

    // --- Traffic Generation (Q2) ---
    uint16_t port = 9;
    
    // 1. Video Flow (DSCP EF = 0x2e, High Priority)
    OnOffHelper videoApp("ns3::UdpSocketFactory", InetSocketAddress(videoNextHop, port));
    videoApp.SetAttribute("PacketSize", UintegerValue(1024));
    videoApp.SetAttribute("DataRate", StringValue("1Mbps"));
    videoApp.SetAttribute("ToS", UintegerValue(0x2e << 2)); // Set ToS for DSCP EF
    videoApp.Install(studio).Start(Seconds(1.0));

    // 2. Data Flow (DSCP BE = 0x00, Low Priority)
    OnOffHelper dataApp("ns3::UdpSocketFactory", InetSocketAddress(dataNextHop, port));
    dataApp.SetAttribute("PacketSize", UintegerValue(1024));
    dataApp.SetAttribute("DataRate", StringValue("1Mbps"));
    dataApp.SetAttribute("ToS", UintegerValue(0x00)); // Set ToS for DSCP BE
    dataApp.Install(studio).Start(Seconds(1.0));

    // Sink on Cloud node (n2)
    PacketSinkHelper sink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
    sink.Install(cloud).Start(Seconds(0.0));

    Simulator::Stop(Seconds(10.0));
    Simulator::Run();
    Simulator::Destroy();
    return 0;
}
