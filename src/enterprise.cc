#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/mobility-module.h"
#include "ns3/csma-module.h"
#include <cmath>
#include "ns3/netanim-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/bulk-send-helper.h"
#include "ns3/on-off-helper.h"
#include "ns3/rip-helper.h"
#include "ns3/rip.h"
#include "ns3/ipv4-list-routing-helper.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ipv4-static-routing.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("EnterpriseTopology");

AnimationInterface* g_anim = nullptr;

/* ROUTER FAILURE */
void RouterFailure(Ptr<Node> node)
{
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();

    for (uint32_t i = 1; i < ipv4->GetNInterfaces(); i++)
    {
        ipv4->SetDown(i);
    }

    if (g_anim) {
        g_anim->UpdateNodeColor(node, 255, 0, 0); // Color failed node RED
    }

    std::cout << "ROUTER FAILURE at "
              << Simulator::Now().GetSeconds()
              << "s NodeID="
              << node->GetId()
              << std::endl;
              
    // In Dynamic RIP, the network recovers autonomously via Hello/Update packets
}

/* CENTRALIZED TOPOLOGY (SPOF) */
void BuildCentralized(NodeContainer clients,
                      NodeContainer dist,
                      NodeContainer core,
                      NodeContainer server,
                      PointToPointHelper link,
                      std::vector<NetDeviceContainer> &links)
{
    // ALL Distribution routers connected to exactly 1 Core (SPOF)
    for (uint32_t i=0; i < dist.GetN(); i++) {
        links.push_back(link.Install(core.Get(0), dist.Get(i)));
    }

    uint32_t n_dist = dist.GetN();
    uint32_t c_per_dist = (clients.GetN() + n_dist - 1) / n_dist;
    for (uint32_t i = 0; i < clients.GetN(); i++) {
        uint32_t target = std::min(i / c_per_dist, n_dist - 1);
        links.push_back(link.Install(clients.Get(i), dist.Get(target)));
    }

    // Centralized: ALL Servers connect ONLY to Core 0 (SPOF)
    for (uint32_t i=0; i < server.GetN(); i++) {
        links.push_back(link.Install(core.Get(0), server.Get(i)));
    }
}

/* EXTERNAL PATH BUILDER (Common for all architectures) */
void BuildExternalPath(NodeContainer core, 
                       NodeContainer edge, 
                       NodeContainer firewall, 
                       NodeContainer internet,
                       PointToPointHelper link,
                       std::vector<NetDeviceContainer> &links,
                       std::string architecture)
{
    // 1. Internet <-> Firewall <-> Edge
    links.push_back(link.Install(internet.Get(0), firewall.Get(0)));
    links.push_back(link.Install(firewall.Get(0), edge.Get(0)));

    // 2. Edge <-> Core (Topology awareness)
    if (architecture == "centralized") {
        links.push_back(link.Install(edge.Get(0), core.Get(0)));
    } else {
        // Distributed or Segmented: Connect Edge to all active cores for shared egress
        for (uint32_t i=0; i < core.GetN(); i++) {
            links.push_back(link.Install(edge.Get(0), core.Get(i)));
        }
    }
}

/* DISTRIBUTED TOPOLOGY (REDUNDANCY) */
void BuildDistributed(NodeContainer clients,
                      NodeContainer dist,
                      NodeContainer core,
                      NodeContainer server,
                      PointToPointHelper link,
                      std::vector<NetDeviceContainer> &links)
{
    // 1. Full Mesh between Cores (Triangle for 3 nodes)
    for (uint32_t i=0; i < core.GetN(); i++) {
        for (uint32_t j=i+1; j < core.GetN(); j++) {
            links.push_back(link.Install(core.Get(i), core.Get(j)));
        }
    }

    // 2. High Redundancy: Every Dist connects to ALL Cores
    for (uint32_t i=0; i < dist.GetN(); i++) {
        for (uint32_t j=0; j < core.GetN(); j++) {
            links.push_back(link.Install(dist.Get(i), core.Get(j)));
        }
    }

    uint32_t n_dist = dist.GetN();
    uint32_t c_per_dist = (clients.GetN() + n_dist - 1) / n_dist;
    for (uint32_t i = 0; i < clients.GetN(); i++) {
        uint32_t target = std::min(i / c_per_dist, n_dist - 1);
        links.push_back(link.Install(clients.Get(i), dist.Get(target)));
    }

    // Distributed Redundancy: Every Server connects to ALL Cores
    for (uint32_t i=0; i < server.GetN(); i++) {
        for (uint32_t j=0; j < core.GetN(); j++) {
            links.push_back(link.Install(core.Get(j), server.Get(i)));
        }
    }
}

/* SEGMENTED TOPOLOGY (ISOLATION) */
void BuildSegmented(NodeContainer clients,
                    NodeContainer dist,
                    NodeContainer core,
                    NodeContainer server,
                    PointToPointHelper link,
                    std::vector<NetDeviceContainer> &links)
{
    // 3 Independent Segments (Tri-Island)
    // Segment 1: Dist 0 -> Core 0
    links.push_back(link.Install(dist.Get(0), core.Get(0)));
    // Segment 2: Dist 1 -> Core 1
    links.push_back(link.Install(dist.Get(1), core.Get(1)));
    // Segment 3: Dist 2 -> Core 2
    links.push_back(link.Install(dist.Get(2), core.Get(2)));

    // Clients divided into 3 groups of 10
    uint32_t n_dist = dist.GetN();
    uint32_t c_per_dist = clients.GetN() / n_dist;
    for (uint32_t i = 0; i < clients.GetN(); i++) {
        uint32_t target = std::min(i / c_per_dist, n_dist - 1);
        links.push_back(link.Install(clients.Get(i), dist.Get(target)));
    }

    // Shared Server Cluster connects to all isolated cores
    for (uint32_t i=0; i < server.GetN(); i++) {
        for (uint32_t j=0; j < core.GetN(); j++) {
            links.push_back(link.Install(core.Get(j), server.Get(i)));
        }
    }
}

int main(int argc, char *argv[])
{
    std::string architecture = "centralized";
    std::string failure = "baseline";
    uint32_t numClients = 30;
    uint32_t numDist = 3;
    uint32_t numCore = 3;
    bool enableAnim = false;
    bool animPackets = true;
    uint64_t animMaxPkts = 100000;
    double simTime = 20.0;
    double failureTime = 10.0;
    double trafficStart = 3.0;

    CommandLine cmd;
    cmd.AddValue("architecture", "centralized/distributed/segmented", architecture);
    cmd.AddValue("failure", "baseline/core_failure/distribution_failure", failure);
    cmd.AddValue("clients", "Number of client nodes", numClients);
    cmd.AddValue("dist", "Number of distribution routers", numDist);
    cmd.AddValue("core", "Number of core routers", numCore);
    cmd.AddValue("anim", "Enable NetAnim XML output", enableAnim);
    cmd.AddValue("animPackets", "Record packet flow in NetAnim (required for playback)", animPackets);
    cmd.AddValue("animMaxPkts", "Max packets written to NetAnim trace", animMaxPkts);
    cmd.AddValue("simTime", "Simulation duration in seconds", simTime);
    cmd.AddValue("failureTime", "Time (s) to inject router failure", failureTime);
    cmd.AddValue("trafficStart", "Time (s) when application traffic starts", trafficStart);
    cmd.Parse(argc, argv);

    if (trafficStart >= simTime - 2.0)
    {
        trafficStart = std::max(1.0, simTime * 0.15);
    }
    if (failureTime >= simTime - 1.0)
    {
        failureTime = simTime * 0.5;
    }

    NodeContainer clients;
    clients.Create(numClients);

    NodeContainer dist;
    dist.Create(numDist);

    NodeContainer core;
    core.Create(numCore);

    NodeContainer server;
    server.Create(3); // S0: Web, S1: Mail, S2: DB

    NodeContainer edge, firewall, internet;
    edge.Create(1);
    firewall.Create(1);
    internet.Create(1);

    if (enableAnim)
    {
        MobilityHelper mobility;
        mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        mobility.Install(clients);
        mobility.Install(dist);
        mobility.Install(core);
        mobility.Install(server);
        mobility.Install(edge);
        mobility.Install(firewall);
        mobility.Install(internet);
    }

    /* ROUTING: RIP on routers; static default route on clients */
    RipHelper ripRouting;
    Ipv4StaticRoutingHelper staticRoutingHelper;
    Ipv4ListRoutingHelper routerListRH;
    routerListRH.Add(staticRoutingHelper, 0);
    routerListRH.Add(ripRouting, 10);

    InternetStackHelper routerStack;
    routerStack.SetRoutingHelper(routerListRH);
    routerStack.Install(core);
    routerStack.Install(dist);
    routerStack.Install(server);
    routerStack.Install(edge);
    routerStack.Install(firewall);
    routerStack.Install(internet);

    InternetStackHelper clientStack;
    clientStack.SetRoutingHelper(staticRoutingHelper);
    clientStack.Install(clients);

    PointToPointHelper link;
    link.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    link.SetChannelAttribute("Delay", StringValue("2ms"));

    std::vector<NetDeviceContainer> links;

    if (architecture == "centralized")
        BuildCentralized(clients, dist, core, server, link, links);
    else if (architecture == "distributed")
        BuildDistributed(clients, dist, core, server, link, links);
    else if (architecture == "segmented")
        BuildSegmented(clients, dist, core, server, link, links);
    
    // BUILD EXTERNAL PATH (Edge, Firewall, Internet)
    BuildExternalPath(core, edge, firewall, internet, link, links, architecture);

    Ipv4AddressHelper address;
    uint32_t subnet = 1;

    for (auto &l : links)
    {
        std::ostringstream subnetAddr;
        // Two-octet format to avoid overflow beyond 255 links
        subnetAddr << "10." << ((subnet / 250) + 1) << "." << ((subnet % 250) + 1) << ".0";
        subnet++;
        address.SetBase(subnetAddr.str().c_str(), "255.255.255.0");
        address.Assign(l);
    }

    // Default route for each client toward its distribution router (peer on the /24 link)
    for (uint32_t i = 0; i < clients.GetN(); i++)
    {
        Ptr<Ipv4> ipv4 = clients.Get(i)->GetObject<Ipv4>();
        Ipv4Address clientAddr = ipv4->GetAddress(1, 0).GetLocal();
        Ipv4Address gateway(clientAddr.Get() + 1);
        Ptr<Ipv4StaticRouting> clientRt =
            staticRoutingHelper.GetStaticRouting(clients.Get(i)->GetObject<Ipv4>());
        clientRt->SetDefaultRoute(gateway, 1);
    }

    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> monitor = flowHelper.InstallAll();

    // MULTI-SERVER SINK SETUP (Aşama 2)
    uint16_t portWeb = 80, portMail = 25, portDB = 3306;
    
    // Server 0 (Web), Server 1 (Mail), Server 2 (DB)
    std::vector<uint16_t> serverPorts = {portWeb, portMail, portDB};
    for (uint32_t s = 0; s < server.GetN(); s++) {
        PacketSinkHelper sink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), serverPorts[s]));
        ApplicationContainer apps = sink.Install(server.Get(s));
        apps.Start(Seconds(1));
        apps.Stop(Seconds(simTime));
    }

    // TRAFFIC MATRIX SETUP (Office Based)
    // Office 1 (0-9): 70% Web (S0), 30% Mail (S1)
    // Office 2 (10-19): 60% DB (S2), 40% Web (S0)
    // HQ (20-29): 50% Mail (S1), 50% DB (S2)

    for (uint32_t i = 0; i < clients.GetN(); i++) {
        uint32_t sIdx;
        uint16_t tPort;
        
        if (i < 10) { // Office 1
            sIdx = (i < 7) ? 0 : 1; 
            tPort = (i < 7) ? portWeb : portMail;
        } else if (i < 20) { // Office 2
            sIdx = (i < 16) ? 2 : 0;
            tPort = (i < 16) ? portDB : portWeb;
        } else { // HQ
            sIdx = (i < 25) ? 1 : 2;
            tPort = (i < 25) ? portMail : portDB;
        }

        Ptr<Ipv4> sIpv4 = server.Get(sIdx)->GetObject<Ipv4>();
        Ipv4Address targetAddr = sIpv4->GetAddress(1, 0).GetLocal();

        OnOffHelper onoff("ns3::UdpSocketFactory", InetSocketAddress(targetAddr, tPort));
        onoff.SetAttribute("OnTime", StringValue("ns3::ExponentialRandomVariable[Mean=1]"));
        onoff.SetAttribute("OffTime", StringValue("ns3::ExponentialRandomVariable[Mean=0.5]"));
        onoff.SetAttribute("DataRate", StringValue(enableAnim ? "500kbps" : "2Mbps"));
        onoff.SetAttribute("PacketSize", UintegerValue(512));

        ApplicationContainer app = onoff.Install(clients.Get(i));
        app.Start(Seconds(trafficStart + (i * 0.01)));
        app.Stop(Seconds(simTime));
    }

    // CLOUD BACKUP TRAFFIC (Servers to Internet - Port 8080)
    uint16_t portBackup = 8080;
    PacketSinkHelper backupSink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), portBackup));
    backupSink.Install(internet.Get(0)).Start(Seconds(1));

    Ipv4Address internetAddr = internet.Get(0)->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();
    OnOffHelper backup("ns3::UdpSocketFactory", InetSocketAddress(internetAddr, portBackup));
    backup.SetAttribute("DataRate", StringValue(enableAnim ? "500kbps" : "2Mbps"));
    backup.SetAttribute("PacketSize", UintegerValue(1024));
    for (uint32_t i = 0; i < server.GetN(); i++) {
        backup.Install(server.Get(i)).Start(Seconds(trafficStart + 2 + (i * 0.2))); 
        backup.Install(server.Get(i)).Stop(Seconds(simTime));
    }

    // INTERNET TRAFFIC (HTTPS) - HQ users only (20-29)
    uint16_t portHttps = 443;
    PacketSinkHelper internetSink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), portHttps));
    internetSink.Install(internet.Get(0)).Start(Seconds(1));
    
    OnOffHelper https("ns3::UdpSocketFactory", InetSocketAddress(internetAddr, portHttps));
    https.SetAttribute("DataRate", StringValue(enableAnim ? "300kbps" : "1Mbps"));
    https.SetAttribute("PacketSize", UintegerValue(1024));
    for (uint32_t i = 20; i < 30; i++) {
        https.Install(clients.Get(i)).Start(Seconds(trafficStart + 1 + (i * 0.01))); 
        https.Install(clients.Get(i)).Stop(Seconds(simTime));
    }

    /* FAILURE EVENTS (default failureTime=30s) */
    Ptr<Node> failNode = nullptr;
    if (failure == "core_failure")
    {
        failNode = core.Get(0);
    }
    else if (failure == "distribution_failure")
    {
        failNode = dist.Get(0);
    }
    if (failNode)
    {
        Simulator::Schedule(Seconds(failureTime), &RouterFailure, failNode);
    }

    // Execution Naming Convention
    std::string test_name = "enterprise_" + architecture + "_" + failure;

    // NetAnim: use 0-100 coordinates (NetAnim default viewport); packet trace required for playback
    AnimationInterface* anim = nullptr;
    if (enableAnim)
    {
        anim = new AnimationInterface("animations/" + test_name + ".xml");
        anim->SetStartTime(Seconds(0));
        anim->SetStopTime(Seconds(simTime));
        anim->EnablePacketMetadata(false);
        if (animPackets)
        {
            anim->SetMaxPktsPerTraceFile(animMaxPkts);
        }
        else
        {
            anim->SkipPacketTracing();
        }
        g_anim = anim;

        // Layout in NetAnim-friendly 0-100 space (see ns-3 dumbbell-animation example)
        const double cx = 50.0;
        const double yInt = 8.0;
        const double yFw = 18.0;
        const double yEdge = 28.0;
        const double ySrv = 40.0;
        const double yCor = 55.0;
        const double yDst = 70.0;
        const double yCli = 88.0;

        anim->SetConstantPosition(internet.Get(0), cx, yInt);
        anim->UpdateNodeSize(internet.Get(0)->GetId(), 8, 8);
        anim->UpdateNodeDescription(internet.Get(0), "INTERNET");
        anim->UpdateNodeColor(internet.Get(0), 255, 255, 255);

        anim->SetConstantPosition(firewall.Get(0), cx, yFw);
        anim->UpdateNodeSize(firewall.Get(0)->GetId(), 7, 7);
        anim->UpdateNodeDescription(firewall.Get(0), "FW");
        anim->UpdateNodeColor(firewall.Get(0), 255, 0, 0);

        anim->SetConstantPosition(edge.Get(0), cx, yEdge);
        anim->UpdateNodeSize(edge.Get(0)->GetId(), 7, 7);
        anim->UpdateNodeDescription(edge.Get(0), "EDGE");
        anim->UpdateNodeColor(edge.Get(0), 255, 165, 0);

        const char* sNames[] = {"Srv:Web", "Srv:Mail", "Srv:DB"};
        for (uint32_t i = 0; i < server.GetN(); i++)
        {
            double x = 62.0 + (i * 12.0);
            anim->SetConstantPosition(server.Get(i), x, ySrv);
            anim->UpdateNodeSize(server.Get(i)->GetId(), 6, 6);
            anim->UpdateNodeDescription(server.Get(i), sNames[i]);
            anim->UpdateNodeColor(server.Get(i), 255, 255, 0);
        }

        if (architecture == "centralized")
        {
            anim->SetConstantPosition(core.Get(0), cx, yCor);
            for (uint32_t i = 0; i < dist.GetN(); i++)
            {
                double x = 20.0 + (60.0 / (dist.GetN() + 1)) * (i + 1);
                anim->SetConstantPosition(dist.Get(i), x, yDst);
            }
        }
        else if (architecture == "segmented")
        {
            anim->SetConstantPosition(core.Get(0), 20.0, yCor);
            anim->SetConstantPosition(core.Get(1), cx, yCor);
            anim->SetConstantPosition(core.Get(2), 80.0, yCor);
            anim->SetConstantPosition(dist.Get(0), 20.0, yDst);
            anim->SetConstantPosition(dist.Get(1), cx, yDst);
            anim->SetConstantPosition(dist.Get(2), 80.0, yDst);
        }
        else
        {
            anim->SetConstantPosition(core.Get(0), 25.0, yCor);
            anim->SetConstantPosition(core.Get(1), cx, yCor);
            anim->SetConstantPosition(core.Get(2), 75.0, yCor);
            anim->SetConstantPosition(dist.Get(0), 15.0, yDst);
            anim->SetConstantPosition(dist.Get(1), cx, yDst);
            anim->SetConstantPosition(dist.Get(2), 85.0, yDst);
        }

        for (uint32_t i = 0; i < core.GetN(); i++)
        {
            anim->UpdateNodeSize(core.Get(i)->GetId(), 6, 6);
            anim->UpdateNodeDescription(core.Get(i), "Core");
            anim->UpdateNodeColor(core.Get(i), 0, 255, 255);
        }
        for (uint32_t i = 0; i < dist.GetN(); i++)
        {
            anim->UpdateNodeSize(dist.Get(i)->GetId(), 6, 6);
            anim->UpdateNodeDescription(dist.Get(i), "Dist");
            anim->UpdateNodeColor(dist.Get(i), 0, 255, 0);
        }

        const char* oNames[] = {"Office1", "Office2", "HQ"};
        for (uint32_t i = 0; i < clients.GetN(); i++)
        {
            double x;
            if (architecture == "segmented")
            {
                if (i < 10)
                {
                    x = 5.0 + (i * 3.5);
                }
                else if (i < 20)
                {
                    x = 38.0 + ((i - 10) * 3.5);
                }
                else
                {
                    x = 72.0 + ((i - 20) * 3.5);
                }
            }
            else
            {
                x = 5.0 + ((90.0 / (clients.GetN() + 1)) * (i + 1));
            }
            anim->SetConstantPosition(clients.Get(i), x, yCli);
            anim->UpdateNodeSize(clients.Get(i)->GetId(), 4, 4);
            anim->UpdateNodeColor(clients.Get(i), 0, 0, 255);
            if (i % 10 == 0)
            {
                anim->UpdateNodeDescription(clients.Get(i), oNames[i / 10]);
            }
            else
            {
                anim->UpdateNodeDescription(clients.Get(i), "C");
            }
        }
    }

    std::cout << "Config: simTime=" << simTime << "s trafficStart=" << trafficStart
              << "s failure=" << failure;
    if (failNode)
    {
        std::cout << " failureTime=" << failureTime << "s failNodeId=" << failNode->GetId();
    }
    if (enableAnim)
    {
        std::cout << " anim=1 animMaxPkts=" << animMaxPkts;
    }
    std::cout << std::endl;

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    if (anim)
    {
        delete anim;
        g_anim = nullptr;
    }

    monitor->CheckForLostPackets();
    monitor->SerializeToXmlFile("results/" + test_name + "_flowmon.xml", true, true);

    std::cout << "Done. FlowMonitor: results/" << test_name << "_flowmon.xml";
    if (enableAnim)
    {
        std::cout << " NetAnim: animations/" << test_name << ".xml";
    }
    std::cout << std::endl;

    Simulator::Destroy();
    return 0;
}
