#pragma once

#include <crow.h>
#include <nlohmann/json.hpp>
#include "db/Database.h"
#include "db/UserRepository.h"
#include "db/PriceRepository.h"
#include "db/HoldingRepository.h"
#include "db/TradeRepository.h"
#include "engine/TradeEngine.h"
#include "feed/PriceFeed.h"
#include "api/Validator.h"

inline void setup_routes(crow::SimpleApp& app, Database& db, PriceFeed& feed) {

    UserRepository users(db);
    PriceRepository prices(db);
    HoldingRepository holdings(db);
    TradeRepository trades(db);
    TradeEngine engine(db);

    app.after_handle([](crow::request&, crow::response& res) {
        res.add_header("Access-Control-Allow-Origin", "*");
    });

    CROW_ROUTE(app, "/api/users/register").methods("POST"_method)
    ([&](const crow::request& req) {
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded()) {
            return crow::response(400, R"({"error":"Invalid JSON"})");
        }

        std::string username = body.value("username", "");

        if (username.empty()) {
            return crow::response(400, R"({"error":"Username required"})");
        }

        if (!Validator::is_valid_username(username)) {
            return crow::response(400, R"({"error":"Invalid username"})");
        }

        try {
            User user = users.create(username);

            nlohmann::json res;
            res["id"]       = user.id;
            res["username"] = user.username;

            return crow::response(201, res.dump());

        } catch (const std::runtime_error& e) {
            return crow::response(409, R"({"error":"Username already taken"})");

        } catch (const std::exception& e) {
            std::cerr << "Register error: " << e.what() << "\n";
            return crow::response(500, R"({error":"Internal server error"})");
        }
    });

    CROW_ROUTE(app, "/api/prices")
    ([&]() {
        try {
            auto fetched = feed.fetch_prices();

            for(auto& [symbol, price] : fetched) {
                prices.upsert(symbol, price);
            }

            auto all = prices.get_all();

            nlohmann::json res = nlohmann::json::array();
            for (auto& p : all) {
                nlohmann::json item;
                item["symbol"]      = p.symbol;
                item["price"]       = p.price;
                item["updated_at"]  = p.updated_at;
                res.push_back(item);
            }
        
            return crow::response(200, res.dump());
        } catch (const std::exception& e) {
            std::cerr << "Prices error: " << e.what() << "\n";
            return crow::response(500, R"({"error":"Internal server error"})");
        }
    });

    CROW_ROUTE(app, "/api/trade").methods("POST"_method)
    ([&](const crow::request& req) {
        try {
            auto body = nlohmann::json::parse(req.body, nullptr, false);

            if (body.is_discarded()) {
                return crow::response(400, R"({"error":"Invalid JSON"})");
            }

            std::string user_id = body.value("user_id","");
            std::string symbol = body.value("symbol","");
            std::string side = body.value("side","");
            double shares = body.value("shares",0.0);

            if (user_id.empty() || symbol.empty() || side.empty() || shares == 0) {
                return crow::response(400, R"({"error":"Missing inputs"})");
            }

            auto result = engine.execute(user_id, symbol, side, shares);

            if (result.success) {
                nlohmann::json res;
                res["success"]      = result.success;
                res["message"]      = result.message;
                res["fill_price"]   = result.fill_price;
                res["total_cost"]   = result.total_cost;
                res["new_balance"]  = result.new_balance;

                return crow::response(200, res.dump());
            } else {
                nlohmann::json err;
                err["success"] = result.success;
                err["message"] = result.message;
                return crow::response(400, err.dump()); 
            }
        } catch (const std::exception& e) {
            std::cerr << "Trade error: " << e.what() << "\n";
            return crow::response(500, R"({"error":"Internal server error"})"); 
        }
    });

    CROW_ROUTE(app, "/api/portfolio/<string>")
    ([&](const crow::request& req, const std::string& user_id) {
        try {
            if (!Validator::is_valid_uuid(user_id)) {
                return crow::response(400, R"({"error":"Invalid user id"})");
            }

            auto user_opt = users.find_by_id(user_id);
            if (!user_opt) {
                return crow::response(404, R"({"error":"User not found"})");
            }
            auto user = *user_opt;

            auto user_holdings = holdings.get_by_user(user_id);

            double equity = 0.0;
            nlohmann::json holdings_array = nlohmann::json::array();

            for (auto& holding : user_holdings) {
                auto price_opt = prices.get(holding.symbol);
                double current_price = 0.0;
                double value = 0.0;

                if (price_opt) {
                    current_price = price_opt->price;
                    value = holding.shares * current_price;
                }

                equity += value;
                
                nlohmann::json item;
                item["symbol"]          = holding.symbol;
                item["shares"]          = holding.shares;
                item["current_price"]   = current_price;
                item["value"]           = value;
                holdings_array.push_back(item);
            }

            nlohmann::json res;
            res["user_id"]      = user_id;
            res["username"]     = user.username;
            res["balance"]      = user.balance;
            res["equity"]       = equity;
            res["total_value"]  = user.balance + equity;
            res["pnl"]          = user.balance + equity - 100000.0;
            res["holdings"]     = holdings_array;

            return crow::response(200, res.dump());

        } catch (const std::exception& e) {
            std::cerr << "Portfolio error: " << e.what() << "\n";
            return crow::response(500, R"({"error":"Internal server error"})");
        }
    });

    CROW_ROUTE(app, "/api/history/<string>")
    ([&](const crow::request& req, const std::string& user_id) {
        try {
            if (!Validator::is_valid_uuid(user_id)) {
                return crow::response(400, R"({"error":"Invalid user id"})");
            }

            auto trade_history = trades.get_by_user(user_id);

            nlohmann::json history_array = nlohmann::json::array();

            for (auto trade : trade_history) {
                nlohmann::json item;
                item["id"]          = trade.id;
                item["user_id"]     = trade.user_id;
                item["symbol"]      = trade.symbol;
                item["side"]        = trade.side;
                item["shares"]      = trade.shares;
                item["price"]       = trade.price;
                item["total"]       = trade.total;
                item["executed_at"] = trade.executed_at;

                history_array.push_back(item);
            }

            return crow::response(200, history_array.dump());

        } catch (const std::exception& e) {
            std::cerr << "History error: " << e.what() << "\n";
            return crow::response(500, R"({"error":"Internal server error"})");
        }
    });

    CROW_ROUTE(app, "/api/leaderboard")
    ([&]() {
        try {
            auto board = users.leaderboard();

            int rank = 1;
            nlohmann::json res = nlohmann::json::array();

            for(auto row : board) {
                nlohmann::json item;
                std::string username = std::get<0>(row);
                double cash          = std::get<1>(row);
                double equity        = std::get<2>(row);

                double total_value = cash + equity;
                double pnl         = total_value - 100000.0;
                item["rank"]        = rank;
                item["username"]    = username;
                item["total_value"] = total_value;
                item["pnl"]         = pnl;

                res.push_back(item);
                rank++;
            }

            return crow::response(200, res.dump());

        } catch (const std::exception& e) {
            std::cerr << "Leaderboard error: " << e.what() << "\n";
            return crow::response(500, R"({"error":"Internal server error"})");
        }
    });
}