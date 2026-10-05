#include "feed/PriceFeed.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <map>
#include <stdexcept>
#include <string>

namespace {

// Keeps the easy handle and header list tied to a scope so an exception on the
// way out cannot leak them.
struct CurlRequest {
    CURL* handle = nullptr;
    curl_slist* headers = nullptr;

    CurlRequest() : handle(curl_easy_init()) {
        if (!handle) {
            throw std::runtime_error("Failed to initialize curl");
        }
    }

    ~CurlRequest() {
        if (headers) curl_slist_free_all(headers);
        if (handle)  curl_easy_cleanup(handle);
    }

    CurlRequest(const CurlRequest&) = delete;
    CurlRequest& operator=(const CurlRequest&) = delete;
};

} // namespace

PriceFeed::PriceFeed(const std::string& api_key,
                     const std::string& secret_key,
                     const std::string& data_url,
                     const std::string& trading_url)
    : api_key_(api_key), secret_key_(secret_key),
      data_url_(data_url), trading_url_(trading_url) {}

size_t PriceFeed::write_callback(void* contents, size_t size, size_t nmemb, std::string* output) {
    output->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

std::string PriceFeed::http_get(const std::string& url) {
    CurlRequest request;
    std::string response_body;

    request.headers = curl_slist_append(request.headers, ("APCA-API-KEY-ID: " + api_key_).c_str());
    request.headers = curl_slist_append(request.headers, ("APCA-API-SECRET-KEY: " + secret_key_).c_str());

    curl_easy_setopt(request.handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(request.handle, CURLOPT_HTTPHEADER, request.headers);
    curl_easy_setopt(request.handle, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(request.handle, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(request.handle, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(request.handle, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(request.handle, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(request.handle);

    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("Market data request failed: ") + curl_easy_strerror(res));
    }

    long status = 0;
    curl_easy_getinfo(request.handle, CURLINFO_RESPONSE_CODE, &status);

    if (status < 200 || status >= 300) {
        throw std::runtime_error("Market data request returned HTTP " + std::to_string(status) +
                                 ": " + response_body.substr(0, 200));
    }

    return response_body;
}

std::map<std::string, double> PriceFeed::fetch_prices() {
    std::map<std::string, double> prices;

    std::string symbols_param;
    for (size_t i{}; i < SYMBOLS.size(); i++) {
        symbols_param += SYMBOLS[i];
        if (i < SYMBOLS.size() - 1) {
            symbols_param += ",";
        }
    }

    const std::string url = data_url_ + "/v2/stocks/trades/latest?symbols=" + symbols_param + "&feed=iex";

    try {
        auto data = nlohmann::json::parse(http_get(url));

        if (!data.contains("trades") || !data["trades"].is_object()) {
            std::cerr << "Price feed: response contained no trades object\n";
            return prices;
        }

        const auto& trades = data["trades"];

        for (const auto& symbol : SYMBOLS) {
            if (trades.contains(symbol) && trades[symbol].contains("p")) {
                prices[symbol] = trades[symbol]["p"].get<double>();
            }
        }
    } catch (const std::exception& e) {
        // A bad poll is not fatal; the caller keeps serving the last known prices.
        std::cerr << "Price feed error: " << e.what() << "\n";
    }

    return prices;
}

// Alpaca dates its bar queries from a start day. Computed with the C++20
// calendar types rather than gmtime, which returns shared static storage and
// would race between the Crow worker threads that call this.
static std::string utc_date_days_ago(int days) {
    namespace chrono = std::chrono;

    const auto day = chrono::floor<chrono::days>(chrono::system_clock::now()) - chrono::days(days);
    const chrono::year_month_day ymd{day};

    std::ostringstream out;
    out << static_cast<int>(ymd.year()) << '-'
        << std::setw(2) << std::setfill('0') << static_cast<unsigned>(ymd.month()) << '-'
        << std::setw(2) << std::setfill('0') << static_cast<unsigned>(ymd.day());

    return out.str();
}

std::string PriceFeed::fetch_bars(const std::string& symbol, const std::string& timeframe, int limit) {
    // Three things this endpoint needs that are easy to get wrong:
    //   start   without it Alpaca returns {"bars":{}} and the chart stays empty
    //   sort    the default is ascending, which hands back the OLDEST bars in
    //           the window; desc gives the most recent `limit`, which is what a
    //           chart wants (the page re-sorts ascending before drawing)
    //   feed    omitted on purpose, so Alpaca serves the best the account is
    //           entitled to. Pinning iex yields hourly bars thin enough to be
    //           useless as candles.
    // 30 days comfortably covers 100 hourly bars of market time.
    const std::string url = data_url_ + "/v2/stocks/bars?symbols=" + symbol +
                            "&timeframe=" + timeframe +
                            "&limit=" + std::to_string(limit) +
                            "&sort=desc" +
                            "&start=" + utc_date_days_ago(30);

    // Unlike fetch_prices this propagates: the route turns a failure into a 502
    // rather than silently handing the browser an empty chart.
    return http_get(url);
}

MarketClock PriceFeed::fetch_clock() {
    MarketClock clock;

    try {
        auto data = nlohmann::json::parse(http_get(trading_url_ + "/v2/clock"));

        clock.is_open    = data.value("is_open", false);
        clock.next_open  = data.value("next_open", "");
        clock.next_close = data.value("next_close", "");
        clock.known      = true;
    } catch (const std::exception& e) {
        // Not fatal. The page simply will not claim to know the market state.
        std::cerr << "Market clock error: " << e.what() << "\n";
    }

    return clock;
}
