#pragma once

#include <cmath>
#include <string>
#include "Symbols.h"

class Validator {
public:
    static bool is_valid_symbol(const std::string& symbol) {
        return is_known_symbol(symbol);
    }

    static bool is_valid_side(const std::string& side) {
        return (side == "BUY" || side == "SELL");
    }

    static bool is_valid_shares(const double shares) {
        // The upper bound keeps a nonsense order from overflowing NUMERIC(18,6)
        // and surfacing as a 500 instead of a clean validation error.
        return std::isfinite(shares) && shares > 0.0 && shares <= 1e9;
    }
};
