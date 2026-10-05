#pragma once

#include <mutex>
#include <string>

// Whether the US market is currently trading, as reported by Alpaca.
struct MarketClock {
    bool        is_open = false;
    std::string next_open;    // ISO 8601 with offset, as Alpaca sends it
    std::string next_close;
    bool        known = false;  // false until a fetch has actually succeeded
};

// Written once a minute by the price thread, read by request handlers.
// The clock changes a handful of times a day, so there is no reason to ask
// Alpaca about it on every page poll.
class MarketState {
public:
    void set(const MarketClock& clock) {
        std::lock_guard<std::mutex> lock(mutex_);
        clock_ = clock;
    }

    MarketClock get() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return clock_;
    }

private:
    mutable std::mutex mutex_;
    MarketClock clock_;
};
