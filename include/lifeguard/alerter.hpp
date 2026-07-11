#pragma once

#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>

#include "lifeguard/config.hpp"
#include "lifeguard/distress_analyzer.hpp"

namespace lifeguard {

// Emits alerts when a swimmer is assessed as distressed. Outputs are
// configurable and independent: a log line, a GPIO buzzer/LED, and (later)
// a network notification. Includes simple rate-limiting so a single incident
// does not spam.
class Alerter {
public:
    explicit Alerter(const Config& cfg);
    ~Alerter();

    bool init();

    // Called for every assessment; only fires outputs when `alerting` is true
    // and the per-track cooldown has elapsed.
    void handle(const DistressAssessment& assessment, uint64_t timestamp_ns);

private:
    void fireGpio(bool on);
    void fireLog(const DistressAssessment& a, uint64_t ts);

    const Config& cfg_;
    std::ofstream log_;
    int gpio_fd_ = -1;
    uint64_t cooldown_ns_ = 10ULL * 1000 * 1000 * 1000;  // 10s per track
    std::unordered_map<int, uint64_t> last_alert_ns_;
};

}  // namespace lifeguard
