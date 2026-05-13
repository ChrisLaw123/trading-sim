#include "Config.h"
#include "db/Database.h"
#include "db/PriceRepository.h"
#include "feed/PriceFeed.h"
#include "api/Router.h"
#include <crow.h>
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    try {
        Config config(".env");
        config.validate();

        Database db(config.db_connection_string());

        PriceFeed feed(
            config.get("ALPACA_API_KEY"),
            config.get("ALPACA_SECRET_KEY"),
            config.get("ALPACA_BASE_URL")
        );

        PriceRepository prices(db);
        auto initial = feed.fetch_prices();
        for (auto& [symbol, price] : initial) {
            prices.upsert(symbol, price);
            std::cout << symbol << ": $" << price << "\n";
        }
        std::cout << "Initial prices loaded\n";

        std::thread price_thread([&]() {
            Database thread_db(config.db_connection_string());
            PriceRepository thread_prices(thread_db);
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(60));
                auto updated = feed.fetch_prices();
                for (auto& [symbol, price] : updated) {
                    thread_prices.upsert(symbol, price);
                }
                std::cout << "Prices refreshed\n";
            }
        });

        price_thread.detach();

        crow::SimpleApp app;
        setup_routes(app, db, feed);

        std::cout << "Starting up\n";

        app.port(8080).multithreaded().run();
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Config error: " << e.what() << "\n";
        return 1;
    }
}