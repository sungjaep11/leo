// Periodic online path cache. Uses only current routing, positions and link attributes.
#pragma once
#include "ns3/tcp-vegas.h"
#include "ns3/ipv4-arbiter-routing.h"
#include "ns3/arbiter-single-forward.h"
#include "ns3/mobility-model.h"
#include "ns3/data-rate.h"
#include "ns3/double.h"
#include "ns3/traffic-control-layer.h"
#include <map>
#include <deque>
#include <set>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <cmath>
namespace Lab1Geometry {
using namespace ns3;
using Clock=std::chrono::steady_clock;
using Key=std::pair<uint32_t,uint32_t>;
struct Entry {TcpVegas::GeometryPrediction p;double propagationMs=0,serializationMs=0;uint32_t hops=0;};
struct Pair {uint32_t destination;std::deque<Entry> history;};
inline NodeContainer nodes;
inline std::map<uint32_t,uint32_t> ipToNode;
inline std::map<Key,Pair> pairs;
inline std::ofstream predictions,drops;
inline std::string logs;
inline Time interval=MilliSeconds(100),deliveryDelay=Seconds(0);
inline uint64_t builds=0,lookups=0,misses=0,hops=0,buildNs=0,lookupNs=0,maxBuildNs=0,maxLookupNs=0,tcDrops=0,maxEntries=0,registrations=0;
inline bool enabled=false;
inline double EnvMs(const char* key,double fallback) {
    const char* p=std::getenv(key);if(!p || !*p)return fallback;
    char* end=nullptr;const double value=std::strtod(p,&end);
    NS_ABORT_MSG_IF(end==p || *end || !std::isfinite(value) || value<0,"Invalid geometry timing");
    return value;
}
inline void Walk(uint32_t source,uint32_t destination,uint32_t bytes,Entry& entry) {
    std::set<uint32_t> visited;uint32_t current=source;
    while(current!=destination) {
        NS_ABORT_MSG_IF(!visited.insert(current).second || visited.size()>nodes.GetN(),"Routing loop in prediction");
        auto node=nodes.Get(current);
        auto route=node->GetObject<Ipv4>()->GetRoutingProtocol()->GetObject<Ipv4ArbiterRouting>();
        auto arbiter=DynamicCast<ArbiterSingleForward>(route->GetArbiter());
        NS_ABORT_MSG_IF(!arbiter,"Expected single-forward routing");
        const auto state=arbiter->TopologySatelliteNetworkDecide(source,destination,nullptr,Ipv4Header(),true);
        const int32_t next=std::get<0>(state),iface=std::get<1>(state);
        NS_ABORT_MSG_IF(next<0 || iface<0,"No route in prediction");
        // The arbiter already translated fstate's index to the IPv4 interface index.
        auto ipv4=node->GetObject<Ipv4>();
        NS_ABORT_MSG_IF(uint32_t(iface)>=ipv4->GetNInterfaces(),"Invalid prediction interface");
        auto device=ipv4->GetNetDevice(iface);
        NS_ABORT_MSG_IF(!device || !device->GetChannel(),"Prediction route has no physical channel");
        DataRateValue rate;device->GetAttribute("DataRate",rate);
        DoubleValue speed;device->GetChannel()->GetAttribute("PropagationSpeed",speed);
        const auto from=node->GetObject<MobilityModel>()->GetPosition();
        const auto to=nodes.Get(next)->GetObject<MobilityModel>()->GetPosition();
        // Cheap snapshot approximation: positions and routing are held at computation time.
        entry.propagationMs+=CalculateDistance(from,to)/speed.Get()*1000;
        entry.serializationMs+=rate.Get().CalculateBytesTxTime(bytes).GetSeconds()*1000;
        entry.p.route^=uint64_t(current)*nodes.GetN()+next;entry.p.route*=1099511628211ULL;
        ++entry.hops;++hops;current=next;
    }
}
inline void Build(const Key& key,Pair& pair) {
    const auto start=Clock::now();Entry e;e.p.computed=Simulator::Now();
    e.p.available=e.p.computed+deliveryDelay;e.p.route=1469598103934665603ULL;
    // Normal MSS=1380, TCP timestamp header=32, IPv4=20, PPP=2.
    Walk(key.first,pair.destination,1434,e);Walk(pair.destination,key.first,54,e);
    e.p.rtt=Seconds((e.propagationMs+e.serializationMs)/1000);
    pair.history.push_back(e);
    // Bound per-path storage. Also retain one predecessor for boundary lookups.
    const Time oldest=Simulator::Now()-Seconds(10)-deliveryDelay;
    while(pair.history.size()>2 && pair.history[1].p.available<oldest)pair.history.pop_front();
    maxEntries=std::max(maxEntries,uint64_t(pair.history.size()));++builds;
    const uint64_t elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();
    buildNs+=elapsed;maxBuildNs=std::max(maxBuildNs,elapsed);
    predictions<<e.p.computed.GetSeconds()<<','<<e.p.available.GetSeconds()<<','<<key.first<<','<<pair.destination<<','
        <<e.p.route<<','<<e.hops<<','<<e.propagationMs<<','<<e.serializationMs<<','<<e.p.rtt.GetSeconds()*1000<<','<<elapsed<<'\n';
}
inline void Register(uint32_t source,uint32_t destinationIp) {
    ++registrations;const Key key{source,destinationIp};if(pairs.count(key))return;
    NS_ABORT_MSG_IF(!ipToNode.count(destinationIp),"Unknown destination IP");
    auto it=pairs.emplace(key,Pair{ipToNode.at(destinationIp),{}}).first;Build(key,it->second);
}
inline bool Lookup(uint32_t source,uint32_t destinationIp,Time sent,TcpVegas::GeometryPrediction& out) {
    const auto start=Clock::now();++lookups;bool ok=false;
    auto found=pairs.find({source,destinationIp});
    if(found!=pairs.end()) {
        auto& h=found->second.history;
        const auto it=std::upper_bound(h.begin(),h.end(),sent,[](Time t,const Entry& e){return t<e.p.available;});
        if(it!=h.begin()) {out=std::prev(it)->p;ok=true;}
        // Startup exception: zero-state prediction was already computed at connection start.
        // If delivery is delayed, do not use it until it has actually arrived.
    }
    if(!ok)++misses;
    const uint64_t elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();
    lookupNs+=elapsed;maxLookupNs=std::max(maxLookupNs,elapsed);
    return ok;
}
inline void Tick() {for(auto& item:pairs)Build(item.first,item.second);Simulator::Schedule(interval,&Tick);}
inline void TcDrop(uint32_t node,Ptr<const Packet> p) {
    ++tcDrops;drops<<Simulator::Now().GetSeconds()<<','<<node<<','<<p->GetUid()<<','<<p->GetSize()<<'\n';
}
inline void Start(NodeContainer all,const std::string& directory) {
    nodes=all;logs=directory;enabled=TcpVegas::Lab1GeometryEnabled();
    // Additional observation only; catches stopped-device drops missed by Queue::Drop.
    drops.open(logs+"/traffic_control_drops.csv");drops<<"time_s,node,packet_uid,bytes\n"<<std::setprecision(15);
    for(uint32_t n=0;n<nodes.GetN();++n) {
        if(auto tc=nodes.Get(n)->GetObject<TrafficControlLayer>())
            NS_ABORT_MSG_IF(!tc->TraceConnectWithoutContext("TcDrop",MakeBoundCallback(&TcDrop,n)),"Cannot attach TcDrop");
        auto ip=nodes.Get(n)->GetObject<Ipv4>();
        for(uint32_t i=1;i<ip->GetNInterfaces();++i)
            for(uint32_t a=0;a<ip->GetNAddresses(i);++a)ipToNode[ip->GetAddress(i,a).GetLocal().Get()]=n;
    }
    if(!enabled)return;
    interval=Seconds(EnvMs("LAB1_GEOMETRY_INTERVAL_MS",100)/1000);
    deliveryDelay=Seconds(EnvMs("LAB1_GEOMETRY_DELIVERY_MS",0)/1000);
    NS_ABORT_MSG_IF(interval<=Seconds(0) || interval>Seconds(1) || deliveryDelay>Seconds(5),"Invalid geometry interval/delay");
    predictions.open(logs+"/geometry_predictions.csv");
    predictions<<"computed_s,available_s,source,destination,route_id,roundtrip_hops,propagation_ms,serialization_ms,predicted_ms,compute_ns\n"<<std::setprecision(15);
    TcpVegas::Lab1SetGeometryService(&Register,&Lookup);
    // Read current routing after updates at the same nominal simulation timestamp.
    Simulator::Schedule(interval+NanoSeconds(1),&Tick);
}
inline void Finish() {
    drops.flush();predictions.flush();std::ofstream out(logs+"/geometry_cost.json");
    out<<std::setprecision(15)<<"{\"enabled\":"<<(enabled?"true":"false")
       <<",\"interval_ms\":"<<interval.GetSeconds()*1000<<",\"delivery_ms\":"<<deliveryDelay.GetSeconds()*1000
       <<",\"registered_connections\":"<<registrations<<",\"cached_paths\":"<<pairs.size()
       <<",\"prediction_builds\":"<<builds<<",\"link_visits\":"<<hops<<",\"cache_lookups\":"<<lookups
       <<",\"cache_misses\":"<<misses<<",\"max_history_entries_per_path\":"<<maxEntries
       <<",\"build_wall_ns\":"<<buildNs<<",\"lookup_wall_ns\":"<<lookupNs
       <<",\"max_build_wall_ns\":"<<maxBuildNs<<",\"max_lookup_wall_ns\":"<<maxLookupNs
       <<",\"traffic_control_drop_events\":"<<tcDrops<<"}\n";
}
}
