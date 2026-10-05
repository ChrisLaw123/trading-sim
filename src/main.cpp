#include "Config.h"
#include "db/ConnectionPool.h"
#include "db/PriceRepository.h"
#include "feed/PriceFeed.h"
#include "api/Router.h"
#include "feed/MarketClock.h"

#include <crow.h>
#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

constexpr int    SERVER_PORT        = 8080;
constexpr int    PRICE_REFRESH_SECS = 60;
constexpr size_t DEFAULT_POOL_SIZE  = 8;

size_t parse_pool_size(const Config& config) {
    const std::string raw = config.get_or("DB_POOL_SIZE", std::to_string(DEFAULT_POOL_SIZE));

    try {
        const int value = std::stoi(raw);
        if (value >= 1 && value <= 64) {
            return static_cast<size_t>(value);
        }
    } catch (const std::exception&) {
        // fall through to the default
    }

    std::cerr << "Ignoring invalid DB_POOL_SIZE (" << raw << "), using "
              << DEFAULT_POOL_SIZE << "\n";
    return DEFAULT_POOL_SIZE;
}

} // namespace

int main() {
    // libcurl's implicit global init is not thread-safe, and the price thread and
    // the bars proxy both reach for curl. Do it once, here, before any thread exists.
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        std::cerr << "Fatal error: failed to initialise libcurl\n";
        return 1;
    }

    int exit_code = 0;

    try {
        Config config(".env");
        config.validate();

        const std::string conn_string     = config.db_connection_string();
        const double      starting_balance = config.starting_balance();

        ConnectionPool pool(conn_string, parse_pool_size(config));

        PriceFeed feed(
            config.get("ALPACA_API_KEY"),
            config.get("ALPACA_SECRET_KEY"),
            config.get("ALPACA_BASE_URL"),
            // The clock lives on the trading host, not the market data host.
            config.get_or("ALPACA_TRADING_URL", "https://api.alpaca.markets")
        );

        MarketState market;

        {
            auto handle = pool.acquire();
            PriceRepository prices(handle.db());

            auto initial = feed.fetch_prices();

            if (initial.empty()) {
                std::cerr << "Warning: no prices returned on startup; serving whatever the table already holds\n";
            }

            for (auto& [symbol, price] : initial) {
                prices.upsert(symbol, price);
                std::cout << symbol << ": $" << price << "\n";
            }

            std::cout << "Initial prices loaded\n";
        }

        // Seed the clock before serving, so the first page load already knows
        // whether the market is open rather than waiting a refresh cycle.
        market.set(feed.fetch_clock());

        std::atomic<bool>       stopping{false};
        std::mutex              shutdown_mutex;
        std::condition_variable shutdown_cv;

        // The only writer of the prices table. Joined on shutdown rather than
        // detached, and every iteration is wrapped: a bad poll used to escape the
        // thread and take the whole process down with it.
        std::thread price_thread([&]() {
            while (true) {
                {
                    std::unique_lock<std::mutex> lock(shutdown_mutex);
                    shutdown_cv.wait_for(lock,
                                         std::chrono::seconds(PRICE_REFRESH_SECS),
                                         [&] { return stopping.load(); });
                }

                if (stopping.load()) {
                    break;
                }

                try {
                    // Refreshed first, so the page can still say why prices are
                    // standing still even on a cycle that returns none.
                    market.set(feed.fetch_clock());

                    auto updated = feed.fetch_prices();

                    if (updated.empty()) {
                        continue;
                    }

                    // Borrowed per cycle, not held for the whole run, so the
                    // slot stays available to request handlers between refreshes.
                    auto handle = pool.acquire();
                    PriceRepository prices(handle.db());

                    for (auto& [symbol, price] : updated) {
                        prices.upsert(symbol, price);
                    }

                    std::cout << "Prices refreshed\n";
                } catch (const std::exception& e) {
                    std::cerr << "Price refresh failed, will retry: " << e.what() << "\n";
                }
            }
        });

        // Signals and joins the price thread however we leave this scope. Without
        // this, a throw from app.run() (an already-bound port, say) would skip the
        // join and destroy a still-joinable thread, which calls std::terminate and
        // turns a clear error message into a crash.
        struct ThreadStopper {
            std::atomic<bool>&       stopping;
            std::condition_variable& cv;
            std::thread&             thread;

            ~ThreadStopper() {
                stopping.store(true);
                cv.notify_all();
                if (thread.joinable()) {
                    thread.join();
                }
            }
        } stopper{stopping, shutdown_cv, price_thread};

        AppContext ctx{pool, feed, market, starting_balance};

        crow::SimpleApp app;
        setup_routes(app, ctx);

        std::cout << "Listening on http://localhost:" << SERVER_PORT << "\n";

        app.port(SERVER_PORT).multithreaded().run();

        std::cout << "Shut down cleanly\n";

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        exit_code = 1;
    }

    curl_global_cleanup();
    return exit_code;
}
