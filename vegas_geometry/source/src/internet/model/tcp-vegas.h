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

#ifndef TCPVEGAS_H
#define TCPVEGAS_H

#include "tcp-congestion-ops.h"
#include "rtt-estimator.h"
#include <functional>
// LAB1_VEGAS_RECENT
// Exact sliding-window minimum; timestamps and RTTs are integer nanoseconds.
#include <cstdint>
#include <deque>
#include <utility>
#include <cassert>
namespace ns3 {
class Lab1RollingRtt {
public:
    void Add(int64_t now, int64_t rtt, int64_t window) {
        assert(window > 0 && rtt > 0);
        assert(m_samples.empty() || now >= m_samples.back().first);
        while (!m_samples.empty() && m_samples.front().first < now - window)
            m_samples.pop_front();
        // A newer equal minimum survives longer than an older one.
        while (!m_samples.empty() && m_samples.back().second >= rtt)
            m_samples.pop_back();
        m_samples.emplace_back(now, rtt);
    }
    int64_t Minimum() const { assert(!m_samples.empty()); return m_samples.front().second; }
    int64_t SourceTime() const { assert(!m_samples.empty()); return m_samples.front().first; }
private:
    std::deque<std::pair<int64_t, int64_t>> m_samples;
};
}


namespace ns3
{

class TcpSocketState;

/**
 * @ingroup congestionOps
 *
 * @brief An implementation of TCP Vegas
 *
 * TCP Vegas is a pure delay-based congestion control algorithm implementing a proactive
 * scheme that tries to prevent packet drops by maintaining a small backlog at the
 * bottleneck queue.
 *
 * Vegas continuously measures the actual throughput a connection achieves as shown in
 * Equation (1) and compares it with the expected throughput calculated in Equation (2).
 * The difference between these 2 sending rates in Equation (3) reflects the amount of
 * extra packets being queued at the bottleneck.
 *
 *              actual = cwnd / RTT        (1)
 *              expected = cwnd / BaseRTT  (2)
 *              diff = expected - actual   (3)
 *
 * To avoid congestion, Vegas linearly increases/decreases its congestion window to ensure
 * the diff value fall between the 2 predefined thresholds, alpha and beta.
 * diff and another threshold, gamma, are used to determine when Vegas should change from
 * its slow-start mode to linear increase/decrease mode.
 *
 * Following the implementation of Vegas in Linux, we use 2, 4, and 1 as the default values
 * of alpha, beta, and gamma, respectively.
 *
 * More information: http://dx.doi.org/10.1109/49.464716
 */

class TcpVegas : public TcpNewReno
{
  public:
    // LAB1_VEGAS_GEOMETRY: network-assisted, shared prediction service.
    struct GeometryPrediction {
        Time computed;
        Time available;
        Time rtt;
        uint64_t route = 0;
    };
    using GeometryRegister = std::function<void(uint32_t, uint32_t)>;
    using GeometryLookup = std::function<bool(uint32_t, uint32_t, Time, GeometryPrediction&)>;
    static bool Lab1GeometryEnabled();
    static void Lab1SetGeometryService(GeometryRegister reg, GeometryLookup lookup);
    void Lab1GeometryEndpoints(uint32_t source, uint32_t destination);
    void Lab1GeometrySample(Time rawRtt, Ptr<RttEstimator> estimator);
    // LAB1_VEGAS_DIAG: observer attachment, unused by congestion control.
    void Lab1SetLog(const std::string& path) { m_lab1LogPath = path; }
    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    /**
     * Create an unbound tcp socket.
     */
    TcpVegas();

    /**
     * @brief Copy constructor
     * @param sock the object to copy
     */
    TcpVegas(const TcpVegas& sock);
    ~TcpVegas() override;

    std::string GetName() const override;

    /**
     * @brief Compute RTTs needed to execute Vegas algorithm
     *
     * The function filters RTT samples from the last RTT to find
     * the current smallest propagation delay + queueing delay (minRtt).
     * We take the minimum to avoid the effects of delayed ACKs.
     *
     * The function also min-filters all RTT measurements seen to find the
     * propagation delay (baseRtt).
     *
     * @param tcb internal congestion state
     * @param segmentsAcked count of segments ACKed
     * @param rtt last RTT
     *
     */
    void PktsAcked(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt) override;

    /**
     * @brief Enable/disable Vegas algorithm depending on the congestion state
     *
     * We only start a Vegas cycle when we are in normal congestion state (CA_OPEN state).
     *
     * @param tcb internal congestion state
     * @param newState new congestion state to which the TCP is going to switch
     */
    void CongestionStateSet(Ptr<TcpSocketState> tcb,
                            const TcpSocketState::TcpCongState_t newState) override;

    /**
     * @brief Adjust cwnd following Vegas linear increase/decrease algorithm
     *
     * @param tcb internal congestion state
     * @param segmentsAcked count of segments ACKed
     */
    void IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked) override;

    /**
     * @brief Get slow start threshold following Vegas principle
     *
     * @param tcb internal congestion state
     * @param bytesInFlight bytes in flight
     *
     * @return the slow start threshold value
     */
    uint32_t GetSsThresh(Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight) override;

    Ptr<TcpCongestionOps> Fork() override;

  private:
    /**
     * @brief Enable Vegas algorithm to start taking Vegas samples
     *
     * Vegas algorithm is enabled in the following situations:
     * 1. at the establishment of a connection
     * 2. after an RTO
     * 3. after fast recovery
     * 4. when an idle connection is restarted
     *
     * @param tcb internal congestion state
     */
    void EnableVegas(Ptr<TcpSocketState> tcb);

    /**
     * @brief Stop taking Vegas samples
     */
    void DisableVegas();

  private:
    // LAB1_VEGAS_ORACLE: opt-in diagnostic.
    void Lab1ApplyOracle(Ptr<const TcpSocketState> tcb);
    static int64_t Lab1RecentWindowNs();
    void Lab1ApplyRecent(Ptr<const TcpSocketState> tcb);
    std::string m_lab1LogPath;
    void Lab1Log(Ptr<const TcpSocketState> tcb, const char* kind, uint32_t before,
                 int64_t diff = -1, int64_t target = -1, int nextState = -1);
    uint32_t m_alpha;             //!< Alpha threshold, lower bound of packets in network
    uint32_t m_beta;              //!< Beta threshold, upper bound of packets in network
    uint32_t m_gamma;             //!< Gamma threshold, limit on increase
    Time m_baseRtt;               //!< Minimum of all Vegas RTT measurements seen during connection
    Time m_minRtt;                //!< Minimum of all RTT measurements within last RTT
    uint32_t m_cntRtt;            //!< Number of RTT measurements during last RTT
    bool m_doingVegasNow;         //!< If true, do Vegas for this RTT
    void Lab1ApplyGeometry(Ptr<const TcpSocketState> tcb);
    uint32_t m_geoSource = 0, m_geoDestination = 0;
    bool m_geoAttached = false, m_geoReady = false, m_geoCycleReady = false;
    Ptr<RttEstimator> m_geoEstimator;
    GeometryPrediction m_geoLast, m_geoCycleLast;
    Time m_geoSent, m_geoCycleSent, m_geoCycleEstimate;
    Lab1RollingRtt m_lab1Recent; // Per-connection state; no global RTT history
    SequenceNumber32 m_begSndNxt; //!< Right edge during last RTT
};

} // namespace ns3

#endif // TCPVEGAS_H
