#pragma once

#include <cmath>
#include <set>
#include <string>
#include <cctype>

class Validator {
public:
    static bool is_valid_uuid(const std::string& uuid) {
        if (uuid.length() != 36) {
            return false;
        }
        
        if (!((uuid[8] == '-') && (uuid[13] == '-') && (uuid[18] == '-') && (uuid[23] == '-'))) {
            return false;
        }
        
        for (int i{}; i < 8; i++) {
            if (!(std::isxdigit(static_cast<unsigned char>(uuid[i])))) {
                return false;
            }
        }

        for (int i{9}; i < 13; i++) {
            if (!(std::isxdigit(static_cast<unsigned char>(uuid[i])))) {
                return false;
            }
        }

        for (int i{14}; i < 18; i++) {
            if (!(std::isxdigit(static_cast<unsigned char>(uuid[i])))) {
                return false;
            }
        }

        for (int i{19}; i < 23; i++) {
            if (!(std::isxdigit(static_cast<unsigned char>(uuid[i])))) {
                return false;
            }
        }

        for (int i{24}; i < 36; i++) {
            if (!(std::isxdigit(static_cast<unsigned char>(uuid[i])))) {
                return false;
            }
        }

        return true;
    }

    static bool is_valid_symbol(const std::string& symbol) {
        static const std::set<std::string> valid_symbols = {
            "AAPL", "MSFT", "GOOGL", "AMZN", "TSLA", "META", "NVDA", "JPM", "BAC", "WMT"

        };

        return (valid_symbols.count(symbol) > 0);
    }

    static bool is_valid_side(const std::string& side) {
        return (side == "BUY" || side == "SELL");
    }

    static bool is_valid_shares(const double shares) {
        return shares > 0 && std::isfinite(shares) && !std::isnan(shares);
    }

    static bool is_valid_username(const std::string& username) {
        if (username.empty() || username.length() > 20) {
            return false;
        }

        for (int i{}; i < username.length(); i++) {
            if(!std::isalnum(static_cast<unsigned char>(username[i])) && username[i] != '_') {
                return false;
            }
        }

        return true;
    }
};