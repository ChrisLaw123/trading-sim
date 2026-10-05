#pragma once

#include <string>
#include <vector>
#include <algorithm>

// The one place the tradeable universe is defined. The price feed polls these and
// the validator accepts exactly these, so the two can never drift apart.
inline const std::vector<std::string> SYMBOLS = {
    "AAPL", "MSFT", "GOOGL", "AMZN", "TSLA", "META", "NVDA", "JPM", "BAC", "WMT"
};

inline bool is_known_symbol(const std::string& symbol) {
    return std::find(SYMBOLS.begin(), SYMBOLS.end(), symbol) != SYMBOLS.end();
}
