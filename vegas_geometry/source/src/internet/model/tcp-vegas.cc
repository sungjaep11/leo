/*
 * Copyright (c) 2016 ResiliNets, ITTC, University of Kansas
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Author: Truc Anh N. Nguyen <annguyen@ittc.ku.edu>
 *
 * James P.G. Sterbenz <jpgs@ittc.ku.edu>, director
 * ResiliNets Research Group  https://resilinets.org/
 * Information and Telecommunication Technology Center (ITTC)
 * and Department of Electrical Engineering and Computer Science
 * The University of Kansas Lawrence, KS USA.
 */

#include "tcp-vegas.h"
// LAB1_VEGAS_DIAG
#include "ns3/simulator.h"
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
// LAB1_VEGAS_ORACLE
#include <vector>
#include <sstream>
#include <cstdlib>
#include <cmath>

#include "tcp-socket-state.h"

#include "ns3/log.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("TcpVegas");
NS_OBJECT_ENSURE_REGISTERED(TcpVegas);

TypeId
TcpVegas::GetTypeId()
{
    static TypeId tid = TypeId("ns3::TcpVegas")
                            .SetParent<TcpNewReno>()
                            .AddConstructor<TcpVegas>()
                            .SetGroupName("Internet")
                            .AddAttribute("Alpha",
                                          "Lower bound of packets in network",
                                          UintegerValue(2),
                                          MakeUintegerAccessor(&TcpVegas::m_alpha),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("Beta",
                                          "Upper bound of packets in network",
                                          UintegerValue(4),
                                          MakeUintegerAccessor(&TcpVegas::m_beta),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("Gamma",
                                          "Limit on increase",
                                          UintegerValue(1),
                                          MakeUintegerAccessor(&TcpVegas::m_gamma),
                                          MakeUintegerChecker<uint32_t>());
    return tid;
}

TcpVegas::TcpVegas()
    : TcpNewReno(),
      m_alpha(2),
      m_beta(4),
      m_gamma(1),
      m_baseRtt(Time::Max()),
      m_minRtt(Time::Max()),
      m_cntRtt(0),
      m_doingVegasNow(true),
      m_begSndNxt(0)
{
    NS_LOG_FUNCTION(this);
}

TcpVegas::TcpVegas(const TcpVegas& sock)
    : TcpNewReno(sock),
      m_alpha(sock.m_alpha),
      m_beta(sock.m_beta),
      m_gamma(sock.m_gamma),
      m_baseRtt(sock.m_baseRtt),
      m_minRtt(sock.m_minRtt),
      m_cntRtt(sock.m_cntRtt),
      m_doingVegasNow(true),
      m_lab1Recent(sock.m_lab1Recent),
      m_begSndNxt(0)
{
    NS_LOG_FUNCTION(this);
}

TcpVegas::~TcpVegas()
{
    NS_LOG_FUNCTION(this);
}

Ptr<TcpCongestionOps>
TcpVegas::Fork()
{
    return CopyObject<TcpVegas>(this);
}

void
TcpVegas::PktsAcked(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt)
{
    NS_LOG_FUNCTION(this << tcb << segmentsAcked << rtt);

    if (rtt.IsZero())
    {
        return;
    }

    // Match the prediction to the observation selected by Vegas's cycle minimum.
    if (Lab1GeometryEnabled() && rtt <= m_minRtt) {
        m_geoCycleReady = m_geoReady;
        if (m_geoReady) {
            m_geoCycleEstimate = m_geoEstimator->GetEstimate();
            m_geoCycleLast = m_geoLast; m_geoCycleSent = m_geoSent;
        }
    }
    m_minRtt = std::min(m_minRtt, rtt);
    NS_LOG_DEBUG("Updated m_minRtt = " << m_minRtt);

    const Time lab1OldBase = m_baseRtt;
    m_baseRtt = std::min(m_baseRtt, rtt);
    // LAB1_VEGAS_RECENT: retain the same PktsAcked input (SRTT in this build).
    if (const int64_t window = Lab1RecentWindowNs(); window > 0) {
        m_lab1Recent.Add(Simulator::Now().GetNanoSeconds(), rtt.GetNanoSeconds(), window);
        m_baseRtt = NanoSeconds(m_lab1Recent.Minimum());
    }
    NS_LOG_DEBUG("Updated m_baseRtt = " << m_baseRtt);

    // Update RTT counter
    m_cntRtt++;
    if (m_baseRtt != lab1OldBase) Lab1Log(tcb, "base_update", tcb->m_cWnd.Get());
    NS_LOG_DEBUG("Updated m_cntRtt = " << m_cntRtt);
}

void
TcpVegas::EnableVegas(Ptr<TcpSocketState> tcb)
{
    NS_LOG_FUNCTION(this << tcb);

    m_doingVegasNow = true;
    m_begSndNxt = tcb->m_nextTxSequence;
    m_cntRtt = 0;
    m_minRtt = Time::Max();
}

void
TcpVegas::DisableVegas()
{
    NS_LOG_FUNCTION(this);

    m_doingVegasNow = false;
}

void
TcpVegas::CongestionStateSet(Ptr<TcpSocketState> tcb, const TcpSocketState::TcpCongState_t newState)
{
    NS_LOG_FUNCTION(this << tcb << newState);
    Lab1Log(tcb, "state_request", tcb->m_cWnd.Get(), -1, -1, int(newState));
    if (newState == TcpSocketState::CA_OPEN)
    {
        EnableVegas(tcb);
    }
    else
    {
        DisableVegas();
    }
}

void
TcpVegas::IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
    NS_LOG_FUNCTION(this << tcb << segmentsAcked);
    const uint32_t lab1Before = tcb->m_cWnd.Get();

    if (!m_doingVegasNow)
    {
        // If Vegas is not on, we follow NewReno algorithm
        NS_LOG_LOGIC("Vegas is not turned on, we follow NewReno algorithm.");
        TcpNewReno::IncreaseWindow(tcb, segmentsAcked);
        Lab1Log(tcb, "reno_disabled", lab1Before);
        return;
    }

    if (tcb->m_lastAckedSeq >= m_begSndNxt)
    { // A Vegas cycle has finished, we do Vegas cwnd adjustment every RTT.

        NS_LOG_LOGIC("A Vegas cycle has finished, we adjust cwnd once per RTT.");

        // Save the current right edge for next Vegas cycle
        m_begSndNxt = tcb->m_nextTxSequence;

        /*
         * We perform Vegas calculations only if we got enough RTT samples to
         * insure that at least 1 of those samples wasn't from a delayed ACK.
         */
        if (m_cntRtt <= 2)
        { // We do not have enough RTT samples, so we should behave like Reno
            NS_LOG_LOGIC(
                "We do not have enough RTT samples to do Vegas, so we behave like NewReno.");
            TcpNewReno::IncreaseWindow(tcb, segmentsAcked);
            Lab1Log(tcb, "reno_insufficient_samples", lab1Before);
        }
        else
        {
            NS_LOG_LOGIC("We have enough RTT samples to perform Vegas calculations");
            /*
             * We have enough RTT samples to perform Vegas algorithm.
             * Now we need to determine if cwnd should be increased or decreased
             * based on the calculated difference between the expected rate and actual sending
             * rate and the predefined thresholds (alpha, beta, and gamma).
             */
            const char* lab1Reason = "hold";
            uint32_t diff;
            uint32_t targetCwnd;
            uint32_t segCwnd = tcb->GetCwndInSegments();

            /*
             * Calculate the cwnd we should have. baseRtt is the minimum RTT
             * per-connection, minRtt is the minimum RTT in this window
             *
             * little trick:
             * desidered throughput is currentCwnd * baseRtt
             * target cwnd is throughput / minRtt
             */
            Lab1ApplyRecent(tcb);
            Lab1ApplyOracle(tcb);
            Lab1ApplyGeometry(tcb);
            double tmp = m_baseRtt.GetSeconds() / m_minRtt.GetSeconds();
            targetCwnd = static_cast<uint32_t>(segCwnd * tmp);
            NS_LOG_DEBUG("Calculated targetCwnd = " << targetCwnd);
            NS_ASSERT(segCwnd >= targetCwnd); // implies baseRtt <= minRtt

            /*
             * Calculate the difference between the expected cWnd and
             * the actual cWnd
             */
            diff = segCwnd - targetCwnd;
            NS_LOG_DEBUG("Calculated diff = " << diff);

            if (diff > m_gamma && (tcb->m_cWnd < tcb->m_ssThresh))
            {
                /*
                 * We are going too fast. We need to slow down and change from
                 * slow-start to linear increase/decrease mode by setting cwnd
                 * to target cwnd. We add 1 because of the integer truncation.
                 */
                NS_LOG_LOGIC("We are going too fast. We need to slow down and "
                             "change to linear increase/decrease mode.");
                lab1Reason = "slow_start_exit";
                segCwnd = std::min(segCwnd, targetCwnd + 1);
                tcb->m_cWnd = segCwnd * tcb->m_segmentSize;
                tcb->m_ssThresh = GetSsThresh(tcb, 0);
                NS_LOG_DEBUG("Updated cwnd = " << tcb->m_cWnd << " ssthresh=" << tcb->m_ssThresh);
            }
            else if (tcb->m_cWnd < tcb->m_ssThresh)
            { // Slow start mode
                NS_LOG_LOGIC("We are in slow start and diff < m_gamma, so we "
                             "follow NewReno slow start");
                lab1Reason = "slow_start";
                TcpNewReno::SlowStart(tcb, segmentsAcked);
            }
            else
            { // Linear increase/decrease mode
                NS_LOG_LOGIC("We are in linear increase/decrease mode");
                if (diff > m_beta)
                {
                    // We are going too fast, so we slow down
                    NS_LOG_LOGIC("We are going too fast, so we slow down by decrementing cwnd");
                    lab1Reason = "decrease_diff_gt_beta";
                    segCwnd--;
                    tcb->m_cWnd = segCwnd * tcb->m_segmentSize;
                    tcb->m_ssThresh = GetSsThresh(tcb, 0);
                    NS_LOG_DEBUG("Updated cwnd = " << tcb->m_cWnd
                                                   << " ssthresh=" << tcb->m_ssThresh);
                }
                else if (diff < m_alpha)
                {
                    // We are going too slow (having too little data in the network),
                    // so we speed up.
                    NS_LOG_LOGIC("We are going too slow, so we speed up by incrementing cwnd");
                    lab1Reason = "increase_diff_lt_alpha";
                    segCwnd++;
                    tcb->m_cWnd = segCwnd * tcb->m_segmentSize;
                    NS_LOG_DEBUG("Updated cwnd = " << tcb->m_cWnd
                                                   << " ssthresh=" << tcb->m_ssThresh);
                }
                else
                {
                    // We are going at the right speed
                    NS_LOG_LOGIC("We are sending at the right speed");
                }
            }
            tcb->m_ssThresh = std::max(tcb->m_ssThresh, 3 * tcb->m_cWnd / 4);
            NS_LOG_DEBUG("Updated ssThresh = " << tcb->m_ssThresh);
            Lab1Log(tcb, lab1Reason, lab1Before, diff, targetCwnd);
        }

        // Reset cntRtt & minRtt every RTT
        m_cntRtt = 0;
        m_minRtt = Time::Max();
    }
    else if (tcb->m_cWnd < tcb->m_ssThresh)
    {
        TcpNewReno::SlowStart(tcb, segmentsAcked);
        Lab1Log(tcb, "slow_start_between_cycles", lab1Before);
    }
}

std::string
TcpVegas::GetName() const
{
    return "TcpVegas";
}

uint32_t
TcpVegas::GetSsThresh(Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight)
{
    NS_LOG_FUNCTION(this << tcb << bytesInFlight);
    return std::max(std::min(tcb->m_ssThresh.Get(), tcb->m_cWnd.Get() - tcb->m_segmentSize),
                    2 * tcb->m_segmentSize);
}

// LAB1_VEGAS_DIAG: observation only; no protocol state is modified.
void
TcpVegas::Lab1Log(Ptr<const TcpSocketState> tcb, const char* kind, uint32_t before,
                 int64_t diff, int64_t target, int nextState)
{
    if (m_lab1LogPath.empty()) return;
    static std::map<std::string, std::unique_ptr<std::ofstream>> streams;
    auto& stream = streams[m_lab1LogPath];
    if (!stream) {
        stream = std::make_unique<std::ofstream>(m_lab1LogPath);
        NS_ABORT_MSG_IF(!*stream, "Cannot open Vegas diagnostic log");
        *stream << "time_s,kind,state,vegas_enabled,base_ms,min_ms,srtt_ms,last_ms,samples,cwnd_before_bytes,cwnd_after_bytes,ssthresh_bytes,diff,target,alpha,beta,gamma,next_state\n";
        *stream << std::setprecision(15);
    }
    *stream << Simulator::Now().GetSeconds() << ',' << kind << ','
            << int(tcb->m_congState.Get()) << ',' << m_doingVegasNow << ','
            << (m_baseRtt == Time::Max() ? -1 : m_baseRtt.GetSeconds()*1000) << ','
            << (m_minRtt == Time::Max() ? -1 : m_minRtt.GetSeconds()*1000) << ','
            << tcb->m_srtt.Get().GetSeconds()*1000 << ','
            << tcb->m_lastRtt.Get().GetSeconds()*1000 << ',' << m_cntRtt << ','
            << before << ',' << tcb->m_cWnd.Get() << ',' << tcb->m_ssThresh.Get() << ','
            << diff << ',' << target << ',' << m_alpha << ',' << m_beta << ','
            << m_gamma << ',' << nextState << '\n';
}

// LAB1_VEGAS_ORACLE: diagnostic intervention in baseRTT only.
// Independent unloaded-path samples are indexed by their reply arrival time.
// No simulator events, cwnd resets, RTT-sample changes, or threshold changes.
void
TcpVegas::Lab1ApplyOracle(Ptr<const TcpSocketState> tcb)
{
    const char* filename=std::getenv("LAB1_VEGAS_ORACLE_CSV");
    if(!filename || std::string(filename).empty())return;
    const char* startFlag=std::getenv("LAB1_VEGAS_ORACLE_START_S");
    const double start=startFlag?std::stod(startFlag):121.6;
    const double now=Simulator::Now().GetSeconds();
    if(now<start)return;
    static std::vector<std::pair<double,double>> samples;
    static std::ofstream output;
    if(samples.empty()) {
        std::ifstream input(filename);
        NS_ABORT_MSG_IF(!input,"Cannot open independent no-queue RTT oracle");
        std::string line;std::getline(input,line);
        while(std::getline(input,line)) {
            std::istringstream row(line);std::string when,rtt;
            if(!std::getline(row,when,',') || !std::getline(row,rtt,','))continue;
            const double t=std::stod(when),ms=std::stod(rtt);
            NS_ABORT_MSG_IF(!std::isfinite(t)||!std::isfinite(ms)||ms<=0,"Invalid oracle sample");
            NS_ABORT_MSG_IF(!samples.empty()&&t<=samples.back().first,"Oracle times must increase strictly");
            samples.emplace_back(t,ms);
        }
        NS_ABORT_MSG_IF(samples.empty(),"Empty oracle");
        NS_ABORT_MSG_IF(m_lab1LogPath.empty(),"Oracle experiment requires Vegas logging");
        output.open(m_lab1LogPath+".oracle.csv");
        NS_ABORT_MSG_IF(!output,"Cannot open oracle intervention log");
        output << "time_s,source_receive_s,oracle_ms,old_base_ms,effective_base_ms,cycle_min_ms,clamped\n" << std::setprecision(15);
    }
    auto it=std::upper_bound(samples.begin(),samples.end(),now,
        [](double t,const auto& sample){return t<sample.first;});
    NS_ABORT_MSG_IF(it==samples.begin(),"No past oracle sample available");--it;
    const Time requested=Seconds(it->second/1000.0);
    const Time effective=std::min(requested,m_minRtt);
    const Time previous=m_baseRtt;
    m_baseRtt=effective;
    output << now << ',' << it->first << ',' << it->second << ','
           << previous.GetSeconds()*1000 << ',' << effective.GetSeconds()*1000 << ','
           << m_minRtt.GetSeconds()*1000 << ',' << (effective<requested) << '\n';
    Lab1Log(tcb,"oracle_base",tcb->m_cWnd.Get());
}

// LAB1_VEGAS_RECENT: only sender-observed samples; no route/oracle input.
int64_t
TcpVegas::Lab1RecentWindowNs()
{
    static const int64_t window = []() -> int64_t {
        const char* flag = std::getenv("LAB1_VEGAS_RECENT_WINDOW_S");
        if (!flag || !*flag) return 0;
        char* end = nullptr;
        const double seconds = std::strtod(flag, &end);
        NS_ABORT_MSG_IF(end == flag || *end || !std::isfinite(seconds) || seconds < 0 || seconds > 3600,
                        "Recent RTT window must be a finite number from 0 to 3600 seconds");
        const char* oracle = std::getenv("LAB1_VEGAS_ORACLE_CSV");
        NS_ABORT_MSG_IF(seconds > 0 && oracle && *oracle, "Recent RTT and oracle modes are mutually exclusive");
        NS_ABORT_MSG_IF(seconds > 0 && Seconds(seconds).GetNanoSeconds() <= 0, "Window is below time resolution");
        return Seconds(seconds).GetNanoSeconds();
    }();
    return window;
}

void
TcpVegas::Lab1ApplyRecent(Ptr<const TcpSocketState> tcb)
{
    const int64_t window = Lab1RecentWindowNs();
    if (!window) return;
    const Time rolling = NanoSeconds(m_lab1Recent.Minimum());
    // Preserve Vegas's base <= cycle-min invariant if a cycle spans an expiry.
    // Only an observed sample can lower the estimate; there is no external RTT.
    m_baseRtt = std::min(rolling, m_minRtt);
    if (m_lab1LogPath.empty()) return;
    static std::map<std::string, std::unique_ptr<std::ofstream>> streams;
    auto& out = streams[m_lab1LogPath];
    if (!out) {
        out = std::make_unique<std::ofstream>(m_lab1LogPath + ".recent.csv");
        NS_ABORT_MSG_IF(!*out, "Cannot open recent RTT audit");
        *out << "time_s,window_s,source_sample_s,rolling_min_ms,cycle_min_ms,effective_base_ms,clamped\n";
        *out << std::setprecision(15);
    }
    *out << Simulator::Now().GetSeconds() << ',' << double(window)/1e9 << ','
         << double(m_lab1Recent.SourceTime())/1e9 << ',' << rolling.GetSeconds()*1000 << ','
         << m_minRtt.GetSeconds()*1000 << ',' << m_baseRtt.GetSeconds()*1000 << ','
         << (m_baseRtt < rolling) << '\n';
}

// LAB1_VEGAS_GEOMETRY. No queue/observed RTT is used by the geometry service.
namespace {
TcpVegas::GeometryRegister geoRegister;
TcpVegas::GeometryLookup geoLookup;
}
bool TcpVegas::Lab1GeometryEnabled() {
    static const bool enabled=[](){
        const char* p=std::getenv("LAB1_VEGAS_GEOMETRY");
        if (!p || std::string(p)!="1") return false;
        const char* oracle=std::getenv("LAB1_VEGAS_ORACLE_CSV");
        NS_ABORT_MSG_IF(oracle && *oracle,"Geometry cannot be combined with the oracle");
        NS_ABORT_MSG_IF(Lab1RecentWindowNs()>0,"Geometry cannot be combined with a recent minimum");
        return true;
    }();
    return enabled;
}
void TcpVegas::Lab1SetGeometryService(GeometryRegister reg, GeometryLookup lookup) {
    geoRegister=std::move(reg);geoLookup=std::move(lookup);
}
void TcpVegas::Lab1GeometryEndpoints(uint32_t source,uint32_t destination) {
    if (!Lab1GeometryEnabled()) return;
    NS_ABORT_MSG_IF(!geoRegister || !geoLookup,"Geometry service is not installed");
    m_geoSource=source;m_geoDestination=destination;m_geoAttached=true;
    geoRegister(source,destination);
}
void TcpVegas::Lab1GeometrySample(Time rawRtt,Ptr<RttEstimator> estimator) {
    if (!Lab1GeometryEnabled() || !m_geoAttached) return;
    // TCP timestamp echo has millisecond resolution in this implementation.
    const Time sent=std::max(Seconds(0),Simulator::Now()-rawRtt);
    GeometryPrediction p;
    if (!geoLookup(m_geoSource,m_geoDestination,sent,p)) {m_geoReady=false;return;}
    if (!m_geoEstimator) {m_geoEstimator=estimator->Copy();m_geoEstimator->Reset();}
    m_geoEstimator->Measurement(p.rtt);
    m_geoReady=true;m_geoLast=p;m_geoSent=sent;
}
void TcpVegas::Lab1ApplyGeometry(Ptr<const TcpSocketState> tcb) {
    if (!Lab1GeometryEnabled() || !m_geoCycleReady) return;
    const Time predicted=m_geoCycleEstimate;
    m_baseRtt=std::min(predicted,m_minRtt);
    if (m_lab1LogPath.empty()) return;
    static std::map<std::string,std::unique_ptr<std::ofstream>> streams;
    auto& out=streams[m_lab1LogPath];
    if (!out) {
        out=std::make_unique<std::ofstream>(m_lab1LogPath+".geometry.csv");
        NS_ABORT_MSG_IF(!*out,"Cannot open geometry audit");
        *out<<"time_s,estimated_send_s,snapshot_s,available_s,route_id,prediction_raw_ms,prediction_smoothed_ms,cycle_min_ms,effective_base_ms,clamped\n"<<std::setprecision(15);
    }
    *out<<Simulator::Now().GetSeconds()<<','<<m_geoCycleSent.GetSeconds()<<','
        <<m_geoCycleLast.computed.GetSeconds()<<','<<m_geoCycleLast.available.GetSeconds()<<','
        <<m_geoCycleLast.route<<','<<m_geoCycleLast.rtt.GetSeconds()*1000<<','
        <<predicted.GetSeconds()*1000<<','<<m_minRtt.GetSeconds()*1000<<','
        <<m_baseRtt.GetSeconds()*1000<<','<<(m_baseRtt<predicted)<<'\n';
}

} // namespace ns3
