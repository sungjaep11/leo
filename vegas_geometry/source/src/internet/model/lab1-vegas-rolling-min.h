// Exact sliding-window minimum; timestamps and RTTs are integer nanoseconds.
#pragma once
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
