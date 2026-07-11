#include "lifeguard/alerter.hpp"

#include <cstdio>
#include <ctime>

namespace lifeguard {

Alerter::Alerter(const Config& cfg) : cfg_(cfg) {}

Alerter::~Alerter() {
    if (log_.is_open()) log_.close();
    if (gpio_fd_ >= 0) fireGpio(false);
}

bool Alerter::init() {
    if (cfg_.alert_log) {
        log_.open(cfg_.log_path, std::ios::app);
        if (!log_.is_open()) {
            std::fprintf(stderr, "[alerter] cannot open log %s\n",
                         cfg_.log_path.c_str());
        }
    }

    if (cfg_.alert_gpio) {
        // TODO(bring-up): open the GPIO line for the buzzer/LED on QNX.
        // On the Pi 5 the GPIO block lives behind the RP1; use the QNX GPIO
        // resource manager / memory-mapped access for the pin and drive it
        // in fireGpio().
        std::fprintf(stderr, "[alerter] GPIO alerts requested (pin %d) — "
                             "TODO: wire up QNX GPIO\n",
                     cfg_.alert_gpio_pin);
    }
    return true;
}

void Alerter::handle(const DistressAssessment& a, uint64_t timestamp_ns) {
    if (!a.alerting) return;

    // Per-track cooldown so one incident does not spam the outputs.
    const auto it = last_alert_ns_.find(a.track_id);
    if (it != last_alert_ns_.end() &&
        timestamp_ns - it->second < cooldown_ns_) {
        return;
    }
    last_alert_ns_[a.track_id] = timestamp_ns;

    if (cfg_.alert_log) fireLog(a, timestamp_ns);
    if (cfg_.alert_gpio) fireGpio(true);

    std::fprintf(stderr,
                 "*** DISTRESS ALERT *** track=%d score=%.2f [%s]\n",
                 a.track_id, a.score, a.reason.c_str());
}

void Alerter::fireLog(const DistressAssessment& a, uint64_t ts) {
    if (!log_.is_open()) return;
    std::time_t now = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::localtime(&now));
    log_ << buf << " ALERT track=" << a.track_id << " score=" << a.score
         << " ts_ns=" << ts << " reason=\"" << a.reason << "\"\n";
    log_.flush();
}

void Alerter::fireGpio(bool on) {
    // TODO(bring-up): drive the GPIO line high/low for the buzzer/LED.
    (void)on;
}

}  // namespace lifeguard
