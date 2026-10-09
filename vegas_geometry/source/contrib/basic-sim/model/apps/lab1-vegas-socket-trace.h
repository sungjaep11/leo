// LAB1_VEGAS_DIAG: sender socket observations only.
#pragma once
#include "ns3/tcp-socket-base.h"
#include "ns3/tcp-header.h"
#include "ns3/tcp-vegas.h"
#include "ns3/simulator.h"
#include <fstream>
#include <iomanip>
#include <memory>
#include <vector>
#include <cstdlib>

namespace Lab1VegasSocketTrace {
using namespace ns3;
struct Record {
    std::ofstream events;
    uint32_t lastAck = 0;
    bool seenAck = false;
};
inline std::vector<std::unique_ptr<Record>> records;
inline void State(Record* r, TcpSocketState::TcpCongState_t oldState,
                  TcpSocketState::TcpCongState_t newState) {
    r->events << Simulator::Now().GetSeconds() << ",state," << int(oldState)
              << ',' << int(newState) << ",-1,0\n";
}
inline void Retx(Record* r, Ptr<const Packet> packet, const TcpHeader& h,
                 const Address&, const Address&, Ptr<const TcpSocketBase>) {
    r->events << Simulator::Now().GetSeconds() << ",retransmission,-1,-1,"
              << h.GetSequenceNumber().GetValue() << ',' << packet->GetSize() << '\n';
}
inline void Rx(Record* r, Ptr<const Packet>, const TcpHeader& h, Ptr<const TcpSocketBase>) {
    if (!(h.GetFlags() & TcpHeader::ACK)) return;
    const uint32_t ack = h.GetAckNumber().GetValue();
    if (r->seenAck && ack == r->lastAck) {
        r->events << Simulator::Now().GetSeconds() << ",repeated_ack,-1,-1,"
                  << ack << ",0\n";
    }
    r->lastAck = ack;
    r->seenAck = true;
}
inline void Attach(Ptr<Socket> sock, const std::string& prefix) {
    const char* flag = std::getenv("LAB1_VEGAS_TRACE");
    if (!flag || std::string(flag) != "1") return;
    auto socket = DynamicCast<TcpSocketBase>(sock);
    auto vegas = socket ? DynamicCast<TcpVegas>(socket->Lab1VegasGetCongestionControl()) : nullptr;
    NS_ABORT_MSG_IF(!vegas, "LAB1_VEGAS_TRACE requires TcpVegas");
    vegas->Lab1SetLog(prefix + "_vegas.csv");
    auto r = std::make_unique<Record>();
    r->events.open(prefix + "_socket_events.csv");
    NS_ABORT_MSG_IF(!r->events, "Cannot open Vegas socket event log");
    r->events << "time_s,kind,old_state,new_state,sequence,bytes\n" << std::setprecision(15);
    const bool a = socket->TraceConnectWithoutContext("CongState", MakeBoundCallback(&State, r.get()));
    const bool b = socket->TraceConnectWithoutContext("Retransmission", MakeBoundCallback(&Retx, r.get()));
    const bool c = socket->TraceConnectWithoutContext("Rx", MakeBoundCallback(&Rx, r.get()));
    NS_ABORT_MSG_IF(!a || !b || !c, "Could not attach Vegas socket traces");
    records.push_back(std::move(r));
}
}
