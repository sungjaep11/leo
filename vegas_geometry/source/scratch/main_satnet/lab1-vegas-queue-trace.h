// LAB1_VEGAS_DIAG: all device queues, event-integrated occupancy and queue waiting time.
#pragma once
#include "ns3/node-container.h"
#include "ns3/channel.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/point-to-point-laser-net-device.h"
#include "ns3/gsl-net-device.h"
#include "ns3/simulator.h"
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <memory>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <deque>

namespace Lab1VegasQueueTrace {
using namespace ns3;
struct Record {
    uint32_t node, device, count = 0, peak = 0;
    uint64_t drops = 0, dequeued = 0;
    std::string type;
    double area = 0, last = 0, waitSum = 0, waitMax = 0;
    std::unordered_map<uint64_t, std::deque<Time>> entered;
    void Touch() {
        const double now = Simulator::Now().GetSeconds();
        area += count * (now-last); last = now;
    }
};
inline std::vector<std::unique_ptr<Record>> records;
inline std::ofstream output, dropOutput;
inline double previous = 0;
inline bool enabled = false;
inline void Occupancy(Record* r, uint32_t, uint32_t value) {
    r->Touch(); r->count=value; r->peak=std::max(r->peak,value);
}
inline void Enqueue(Record* r, Ptr<const Packet> p) {
    r->entered[p->GetUid()].push_back(Simulator::Now());
}
inline void Dequeue(Record* r, Ptr<const Packet> p) {
    auto it=r->entered.find(p->GetUid());
    NS_ABORT_MSG_IF(it==r->entered.end() || it->second.empty(), "Unmatched queue dequeue");
    const double wait=(Simulator::Now()-it->second.front()).GetSeconds();
    it->second.pop_front(); if(it->second.empty()) r->entered.erase(it);
    r->waitSum+=wait; r->waitMax=std::max(r->waitMax,wait); ++r->dequeued;
}
inline void Drop(Record* r, Ptr<const Packet> p) {
    ++r->drops;
    dropOutput << Simulator::Now().GetSeconds() << ',' << r->node << ',' << r->device
               << ',' << r->type << ',' << p->GetUid() << ',' << p->GetSize() << '\n';
}
inline void Flush() {
    const double now=Simulator::Now().GetSeconds(), dt=now-previous;
    if(!enabled || dt<=0) return;
    for(auto& r:records) {
        r->Touch();
        if(r->area>0 || r->peak>0 || r->drops>0 || r->count>0 || r->dequeued>0) {
            output << now << ',' << dt << ',' << r->node << ',' << r->device << ','
                   << r->type << ',' << r->area/dt << ',' << r->peak << ',' << r->count
                   << ',' << r->drops << ',' << r->dequeued << ',' << r->waitSum*1000 << ','
                   << (r->dequeued ? r->waitSum*1000/r->dequeued : 0) << ',' << r->waitMax*1000 << '\n';
        }
        r->area=0; r->peak=r->count; r->drops=0; r->dequeued=0; r->waitSum=0; r->waitMax=0;
    }
    previous=now;
}
inline void Tick() { Flush(); Simulator::Schedule(MilliSeconds(10),&Tick); }
inline void Start(NodeContainer nodes,const std::string& logs) {
    const char* flag=std::getenv("LAB1_VEGAS_TRACE");
    if(!flag || std::string(flag)!="1") return;
    enabled=true;
    output.open(logs+"/vegas_queue_10ms.csv");
    dropOutput.open(logs+"/vegas_queue_drops.csv");
    std::ofstream inventory(logs+"/vegas_queue_inventory.csv");
    NS_ABORT_MSG_IF(!output || !dropOutput || !inventory,"Cannot open queue traces");
    output << "time_s,window_s,node,device,type,mean_packets,peak_packets,end_packets,drops,dequeued,wait_sum_ms,mean_wait_ms,max_wait_ms\n" << std::setprecision(15);
    dropOutput << "time_s,node,device,type,packet_uid,bytes\n" << std::setprecision(15);
    inventory << "node,device,type,channel_id,max_queue\n";
    uint32_t islCount=0,gslCount=0;
    for(uint32_t n=0;n<nodes.GetN();++n) {
        auto node=nodes.Get(n);
        for(uint32_t d=0;d<node->GetNDevices();++d) {
            auto dev=node->GetDevice(d); Ptr<Queue<Packet>> queue; std::string type;
            if(auto p=DynamicCast<PointToPointLaserNetDevice>(dev)) {queue=p->GetQueue();type="ISL";++islCount;}
            else if(auto p=DynamicCast<PointToPointNetDevice>(dev)) {queue=p->GetQueue();type="P2P";}
            else if(auto p=DynamicCast<GSLNetDevice>(dev)) {queue=p->GetQueue();type="GSL";++gslCount;}
            if(!queue) continue;
            auto r=std::make_unique<Record>();r->node=node->GetId();r->device=d;r->type=type;
            r->count=queue->GetNPackets();r->peak=r->count;r->last=Simulator::Now().GetSeconds();
            NS_ABORT_MSG_IF(r->count!=0,"Queue trace attached after traffic began");
            const bool a=queue->TraceConnectWithoutContext("PacketsInQueue",MakeBoundCallback(&Occupancy,r.get()));
            const bool b=queue->TraceConnectWithoutContext("Drop",MakeBoundCallback(&Drop,r.get()));
            const bool c=queue->TraceConnectWithoutContext("Enqueue",MakeBoundCallback(&Enqueue,r.get()));
            const bool e=queue->TraceConnectWithoutContext("Dequeue",MakeBoundCallback(&Dequeue,r.get()));
            NS_ABORT_MSG_IF(!a || !b || !c || !e,"Queue trace connection failed");
            inventory << r->node << ',' << d << ',' << type << ','
                      << (dev->GetChannel()?dev->GetChannel()->GetId():0) << ',' << queue->GetMaxSize() << '\n';
            records.push_back(std::move(r));
        }
    }
    NS_ABORT_MSG_IF(islCount==0 || gslCount==0,"Incomplete satellite queue coverage");
    Simulator::Schedule(MilliSeconds(10),&Tick);
}
inline void Finish() {if(enabled){Flush();output.flush();dropOutput.flush();}}
}
