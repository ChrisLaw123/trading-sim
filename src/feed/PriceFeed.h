#pragma once

#include <string>
#include <map>
#include "Symbols.h"
#include "feed/MarketClock.h"

class PriceFeed {
public:
    // Alpaca splits its API across two hosts: market data on data.alpaca.markets
    // and account/clock endpoints on api.alpaca.markets.
    PriceFeed(const std::string& api_key,
              const std::string& secret_key,
              const std::string& data_url,
              const std::string& trading_url);

    std::map<std::string, double> fetch_prices();

    // Raw bars JSON for one symbol, proxied so the browser never sees our credentials.
    std::string fetch_bars(const std::string& symbol, const std::string& timeframe, int limit);

    // Returns known=false rather than throwing: a missing clock should dim one
    // line of the UI, not fail the request that asked for it.
    MarketClock fetch_clock();

private:
    static size_t write_callback(void* contents, size_t size, size_t nmemb, std::string* output);

    std::string http_get(const std::string& url);

    std::string api_key_;
    std::string secret_key_;
    std::string data_url_;
    std::string trading_url_;
};
