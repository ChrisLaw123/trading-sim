#pragma once

#include <string>
#include <map>
#include <vector>

inline const std::vector<std::string> SYMBOLS = {
    "AAPL", "MSFT", "GOOGL", "AMZN", "TSLA", "META", "NVDA", "JPM", "BAC", "WMT"

};

class PriceFeed {
public:
    PriceFeed(const std::string& api_key, const std::string& secret_key, const std::string& base_url);
    
    std::map<std::string, double> fetch_prices();
    
private:
    static size_t write_callback(void* contents, size_t size, size_t nmemb, std::string* output);

    std::string api_key_;
    std::string secret_key_;
    std::string base_url_;
};