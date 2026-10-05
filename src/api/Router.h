#pragma once

#include <crow.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "db/ConnectionPool.h"
#include "db/UserRepository.h"
#include "db/PriceRepository.h"
#include "db/HoldingRepository.h"
#include "db/TradeRepository.h"
#include "engine/TradeEngine.h"
#include "feed/PriceFeed.h"
#include "feed/MarketClock.h"
#include "api/Validator.h"

// Everything the handlers need. Owned by main, outlives every route.
struct AppContext {
    ConnectionPool& pool;
    PriceFeed&      feed;
    MarketState&    market;
    double          starting_balance;
};

inline crow::response json_response(int status, const nlohmann::json& body) {
    crow::response res(status, body.dump());
    res.set_header("Content-Type", "application/json");
    return res;
}

inline crow::response error_response(int status, const std::string& message) {
    nlohmann::json body;
    body["error"] = message;
    return json_response(status, body);
}

// The page sits at the project root but the binary is usually launched from
// build/. Check the likely spots rather than silently serving an empty body.
inline std::string find_frontend_file() {
    static const char* candidates[] = {
        "frontend/index.html",
        "../frontend/index.html",
        "../../frontend/index.html"
    };

    for (const char* candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec)) {
            return candidate;
        }
    }

    return "";
}

// Single-player. There is one account, so no route takes a player id and there is
// no authentication to perform. Adding multiple players later means putting an id
// back on the portfolio, history and trade routes and deciding how a caller proves
// which one they are.
inline void setup_routes(crow::SimpleApp& app, AppContext& context) {
    AppContext* ctx = &context;

    CROW_ROUTE(app, "/")
    ([]() {
        const std::string path = find_frontend_file();

        if (path.empty()) {
            return crow::response(500, "frontend/index.html not found - start the server from the project root");
        }

        std::ifstream file(path, std::ios::binary);
        std::string content(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>()
        );

        crow::response res(200, content);
        res.set_header("Content-Type", "text/html; charset=utf-8");
        return res;
    });

    // ---- market ----------------------------------------------------------

    // Served straight from the prices table. The background refresher in main is
    // the only thing that talks to Alpaca, so client polling costs nothing
    // upstream no matter how many browsers are open.
    CROW_ROUTE(app, "/api/prices")
    ([ctx]() {
        try {
            auto handle = ctx->pool.acquire();
            PriceRepository prices(handle.db());

            nlohmann::json res = nlohmann::json::array();

            for (auto& p : prices.get_all()) {
                nlohmann::json item;
                item["symbol"]     = p.symbol;
                item["price"]      = p.price;
                item["updated_at"] = p.updated_at;
                res.push_back(item);
            }

            return json_response(200, res);
        } catch (const std::exception& e) {
            std::cerr << "Prices error: " << e.what() << "\n";
            return error_response(500, "Internal server error");
        }
    });

    // Whether the market is trading. Read from the cached value the price
    // thread refreshes, so this costs nothing per request.
    CROW_ROUTE(app, "/api/market")
    ([ctx]() {
        const MarketClock clock = ctx->market.get();

        nlohmann::json res;
        res["known"] = clock.known;

        if (clock.known) {
            res["is_open"]    = clock.is_open;
            res["next_open"]  = clock.next_open;
            res["next_close"] = clock.next_close;
        }

        return json_response(200, res);
    });

    // Proxied so the Alpaca credentials stay on the server. This also sidesteps
    // the CORS wall a browser hits calling Alpaca's data API directly.
    CROW_ROUTE(app, "/api/bars/<string>")
    ([ctx](const crow::request&, const std::string& symbol) {
        if (!Validator::is_valid_symbol(symbol)) {
            return error_response(400, "Unknown symbol");
        }

        try {
            crow::response res(200, ctx->feed.fetch_bars(symbol, "1Hour", 100));
            res.set_header("Content-Type", "application/json");
            return res;
        } catch (const std::exception& e) {
            std::cerr << "Bars error: " << e.what() << "\n";
            return error_response(502, "Market data unavailable");
        }
    });

    // ---- the account -----------------------------------------------------

    CROW_ROUTE(app, "/api/trade").methods("POST"_method)
    ([ctx](const crow::request& req) {
        try {
            auto body = nlohmann::json::parse(req.body, nullptr, false);

            if (body.is_discarded() || !body.is_object()) {
                return error_response(400, "Invalid JSON");
            }

            const std::string symbol = body.value("symbol", "");
            const std::string side   = body.value("side", "");
            const double shares      = body.value("shares", 0.0);

            if (symbol.empty() || side.empty()) {
                return error_response(400, "Symbol and side are required");
            }

            auto handle = ctx->pool.acquire();
            TradeEngine engine(handle.db(), ctx->starting_balance);

            auto result = engine.execute(symbol, side, shares);

            nlohmann::json res;
            res["success"] = result.success;
            res["message"] = result.message;

            if (!result.success) {
                return json_response(400, res);
            }

            res["fill_price"]  = result.fill_price;
            res["total_cost"]  = result.total_cost;
            res["new_balance"] = result.new_balance;

            return json_response(200, res);

        } catch (const std::exception& e) {
            std::cerr << "Trade error: " << e.what() << "\n";
            return error_response(500, "Internal server error");
        }
    });

    CROW_ROUTE(app, "/api/portfolio")
    ([ctx]() {
        try {
            auto handle = ctx->pool.acquire();
            UserRepository users(handle.db());
            PriceRepository prices(handle.db());
            HoldingRepository holdings(handle.db());

            User account = users.get_or_create(ctx->starting_balance);

            double equity = 0.0;
            nlohmann::json holdings_array = nlohmann::json::array();

            for (auto& holding : holdings.get_by_user(account.id)) {
                auto price_opt = prices.get(holding.symbol);

                const double current_price = price_opt ? price_opt->price : 0.0;
                const double value         = holding.shares * current_price;

                equity += value;

                nlohmann::json item;
                item["symbol"]        = holding.symbol;
                item["shares"]        = holding.shares;
                item["current_price"] = current_price;
                item["value"]         = value;
                holdings_array.push_back(item);
            }

            nlohmann::json res;
            res["balance"]     = account.balance;
            res["equity"]      = equity;
            res["total_value"] = account.balance + equity;
            res["pnl"]         = account.balance + equity - ctx->starting_balance;
            res["holdings"]    = holdings_array;

            return json_response(200, res);

        } catch (const std::exception& e) {
            std::cerr << "Portfolio error: " << e.what() << "\n";
            return error_response(500, "Internal server error");
        }
    });

    CROW_ROUTE(app, "/api/history")
    ([ctx]() {
        try {
            auto handle = ctx->pool.acquire();
            UserRepository users(handle.db());
            TradeRepository trades(handle.db());

            User account = users.get_or_create(ctx->starting_balance);

            nlohmann::json res = nlohmann::json::array();

            for (auto& trade : trades.get_by_user(account.id)) {
                nlohmann::json item;
                item["id"]          = trade.id;
                item["symbol"]      = trade.symbol;
                item["side"]        = trade.side;
                item["shares"]      = trade.shares;
                item["price"]       = trade.price;
                item["total"]       = trade.total;
                item["executed_at"] = trade.executed_at;

                res.push_back(item);
            }

            return json_response(200, res);

        } catch (const std::exception& e) {
            std::cerr << "History error: " << e.what() << "\n";
            return error_response(500, "Internal server error");
        }
    });
}
