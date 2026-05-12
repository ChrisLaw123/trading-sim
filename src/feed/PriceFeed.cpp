#include "feed/PriceFeed.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <map>
#include <string>
#include <sstream>

PriceFeed::PriceFeed(const std::string& api_key, const std::string& secret_key, const std::string& base_url)
    : api_key_(api_key), secret_key_(secret_key), base_url_(base_url) {}

size_t PriceFeed::write_callback(void* contents, size_t size, size_t nmemb, std::string* output) {
    output->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}
std::map<std::string, double> PriceFeed::fetch_prices() {
    std::string symbols_param;
    for(size_t i{}; i < SYMBOLS.size(); i++) {
        symbols_param += SYMBOLS[i];
        if(i < SYMBOLS.size() - 1) {
            symbols_param += ",";
        }
    }

    std::string url = base_url_ + "/v2/stocks/trades/latest?symbols=" + symbols_param + "&feed=iex";

    std::string response_body;
    std::map<std::string, double> prices;

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize curl\n";
        return prices;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("APCA-API-KEY-ID: " + api_key_).c_str());
    headers = curl_slist_append(headers, ("APCA-API-SECRET-KEY: " + secret_key_).c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "curl error: " << curl_easy_strerror(res) << "\n";
        return prices;
    }

    try {
        auto data = nlohmann::json::parse(response_body);

        for (const auto& symbol : SYMBOLS) {
            if (data["trades"].contains(symbol)) {
                double price = data["trades"][symbol]["p"].get<double>();
                prices[symbol] = price;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "JSON parse error: " << e.what() << "\n";
        std::cerr << "Response was: " << response_body << "\n";
    }

    return prices;
}

