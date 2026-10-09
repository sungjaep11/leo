// Independent no-load RTT calibration. IPv4 packet sizes match full TCP data
// and timestamp-enabled ACKs: 1432 and 52 bytes before link-layer framing.
#pragma once
#include "ns3/node-container.h"
#include "ns3/ipv4.h"
#include "ns3/inet-socket-address.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/socket.h"
#include "ns3/simulator.h"
#include <array>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <vector>

namespace Lab1OracleProbe {
using namespace ns3;
inline Ptr<Socket> sender, receiver;
inline std::vector<Time> sent;
inline std::ofstream output;
inline bool enabled=false;
inline void ReceiveReply(Ptr<Socket> sock) {
    while(auto p=sock->Recv()) {
        NS_ABORT_MSG_IF(p->GetSize()!=24,"Unexpected calibration reply size");
        uint8_t bytes[24];p->CopyData(bytes,24);
        uint64_t id=0;for(int i=0;i<8;++i)id=(id<<8)|bytes[i];
        NS_ABORT_MSG_IF(id>=sent.size(),"Unknown calibration probe");
        const Time now=Simulator::Now();
        output << id << ',' << sent[id].GetSeconds() << ',' << now.GetSeconds() << ','
               << (now-sent[id]).GetNanoSeconds() << ",1404,24\n";
    }
}
inline void Echo(Ptr<Socket> sock) {
    Address from;
    while(auto p=sock->RecvFrom(from)) {
        NS_ABORT_MSG_IF(p->GetSize()!=1404,"Unexpected calibration request size");
        uint8_t bytes[24];p->CopyData(bytes,24);
        NS_ABORT_MSG_IF(sock->SendTo(Create<Packet>(bytes,24),0,from)!=24,"Calibration reply failed");
    }
}
inline void Send() {
    const uint64_t id=sent.size();std::array<uint8_t,1404> bytes{};
    for(int i=0;i<8;++i)bytes[i]=uint8_t(id>>(56-8*i));
    sent.push_back(Simulator::Now());
    NS_ABORT_MSG_IF(sender->Send(Create<Packet>(bytes.data(),bytes.size()))!=1404,"Calibration request failed");
    Simulator::Schedule(MilliSeconds(10),&Send);
}
inline void Start(NodeContainer nodes,const std::string& logs) {
    const char* flag=std::getenv("LAB1_ORACLE_CALIBRATE");
    if(!flag || std::string(flag)!="1")return;
    enabled=true;NS_ABORT_MSG_IF(nodes.GetN()!=1586,"Calibration expects the existing Seoul-London scenario");
    output.open(logs+"/oracle_probe.csv");
    NS_ABORT_MSG_IF(!output,"Cannot open oracle calibration log");
    output << "probe_id,send_s,receive_s,rtt_ns,request_payload_bytes,reply_payload_bytes\n" << std::setprecision(15);
    receiver=Socket::CreateSocket(nodes.Get(1585),UdpSocketFactory::GetTypeId());
    NS_ABORT_MSG_IF(receiver->Bind(InetSocketAddress(Ipv4Address::GetAny(),51000))!=0,"Cannot bind calibration receiver");
    receiver->SetRecvCallback(MakeCallback(&Echo));
    sender=Socket::CreateSocket(nodes.Get(1584),UdpSocketFactory::GetTypeId());
    NS_ABORT_MSG_IF(sender->Bind()!=0,"Cannot bind calibration sender");
    auto destination=nodes.Get(1585)->GetObject<Ipv4>()->GetAddress(1,0).GetLocal();
    NS_ABORT_MSG_IF(sender->Connect(InetSocketAddress(destination,51000))!=0,"Cannot connect calibration sender");
    sender->SetRecvCallback(MakeCallback(&ReceiveReply));
    Simulator::ScheduleNow(&Send);
}
inline void Finish(){if(enabled)output.flush();}
}
