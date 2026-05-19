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
#include <fstream>

inline crow::response make_response(int status, const std::string& body) {
    crow::response res(status, body);
    res.add_header("Access-Control-Allow-Origin", "*");
    return res;
}

inline void setup_routes(crow::SimpleApp& app, const std::string& conn_string, PriceFeed& feed, const std::string& alpaca_key, const std::string& alpaca_secret) {

    CROW_ROUTE(app, "/api/config")
    ([alpaca_key, alpaca_secret]() {
        nlohmann::json res;
        res["alpaca_key"]    = alpaca_key;
        res["alpaca_secret"] = alpaca_secret;
        return make_response(200, res.dump());
    });

    CROW_ROUTE(app, "/")
    ([](const crow::request& req) {
        std::ifstream file("frontend/index.html");
        std::string content(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>()
        );
        crow::response res(200, content);
        res.add_header("Content-Type", "text/html");
        res.add_header("Access-Control-Allow-Origin", "*");
        return res;
    });

    CROW_ROUTE(app, "/api/users/register").methods("POST"_method)
    ([conn_string](const crow::request& req) {
        Database db(conn_string);
        UserRepository users(db);
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        std::string password = body.value("password","");
        if (body.is_discarded()) {
            return make_response(400, R"({"error":"Invalid JSON"})");
        }

        std::string username = body.value("username", "");

        if (username.empty() || password.empty()) {
            return make_response(400, R"({"error":"Username and password required"})");
        }

        if (!Validator::is_valid_username(username)) {
            return make_response(400, R"({"error":"Invalid username"})");
        }

        try {
            User user = users.create(username, password);

            nlohmann::json res;
            res["id"]       = user.id;
            res["username"] = user.username;

            return make_response(201, res.dump());

        } catch (const std::runtime_error& e) {
            return make_response(409, R"({"error":"Username already taken"})");

        } catch (const std::exception& e) {
            std::cerr << "Register error: " << e.what() << "\n";
            return make_response(500, R"({"error":"Internal server error"})");
        }
    });

    CROW_ROUTE(app, "/api/prices")
    ([conn_string, &feed]() {
        Database db(conn_string);
        PriceRepository prices(db);
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
        
            return make_response(200, res.dump());
        } catch (const std::exception& e) {
            std::cerr << "Prices error: " << e.what() << "\n";
            return make_response(500, R"({"error":"Internal server error"})");
        }
    });

    CROW_ROUTE(app, "/api/trade").methods("POST"_method)
    ([conn_string](const crow::request& req) {
        Database db(conn_string);
        TradeEngine engine(db);
        try {
            auto body = nlohmann::json::parse(req.body, nullptr, false);

            if (body.is_discarded()) {
                return make_response(400, R"({"error":"Invalid JSON"})");
            }

            std::string user_id = body.value("user_id","");
            std::string symbol = body.value("symbol","");
            std::string side = body.value("side","");
            double shares = body.value("shares",0.0);

            if (user_id.empty() || symbol.empty() || side.empty() || shares == 0) {
                return make_response(400, R"({"error":"Missing inputs"})");
            }

            auto result = engine.execute(user_id, symbol, side, shares);

            if (result.success) {
                nlohmann::json res;
                res["success"]      = result.success;
                res["message"]      = result.message;
                res["fill_price"]   = result.fill_price;
                res["total_cost"]   = result.total_cost;
                res["new_balance"]  = result.new_balance;

                return make_response(200, res.dump());
            } else {
                nlohmann::json err;
                err["success"] = result.success;
                err["message"] = result.message;
                return make_response(400, err.dump()); 
            }
        } catch (const std::exception& e) {
            std::cerr << "Trade error: " << e.what() << "\n";
            return make_response(500, R"({"error":"Internal server error"})"); 
        }
    });

    CROW_ROUTE(app, "/api/portfolio/<string>")
    ([conn_string](const crow::request& req, const std::string& user_id) {
        Database db(conn_string);
        UserRepository users(db);
        PriceRepository prices(db);
        HoldingRepository holdings(db);
        try {
            if (!Validator::is_valid_uuid(user_id)) {
                return make_response(400, R"({"error":"Invalid user id"})");
            }

            auto user_opt = users.find_by_id(user_id);
            if (!user_opt) {
                return make_response(404, R"({"error":"User not found"})");
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

            return make_response(200, res.dump());

        } catch (const std::exception& e) {
            std::cerr << "Portfolio error: " << e.what() << "\n";
            return make_response(500, R"({"error":"Internal server error"})");
        }
    });

    CROW_ROUTE(app, "/api/history/<string>")
    ([conn_string](const crow::request& req, const std::string& user_id) {
        Database db(conn_string);
        TradeRepository trades(db);
        try {
            if (!Validator::is_valid_uuid(user_id)) {
                return make_response(400, R"({"error":"Invalid user id"})");
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

            return make_response(200, history_array.dump());

        } catch (const std::exception& e) {
            std::cerr << "History error: " << e.what() << "\n";
            return make_response(500, R"({"error":"Internal server error"})");
        }
    });

    CROW_ROUTE(app, "/api/leaderboard")
    ([conn_string]() {
        Database db(conn_string);
        UserRepository users(db);
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

            return make_response(200, res.dump());

        } catch (const std::exception& e) {
            std::cerr << "Leaderboard error: " << e.what() << "\n";
            return make_response(500, R"({"error":"Internal server error"})");
        }
    });

    CROW_ROUTE(app, "/api/login").methods("POST"_method)
    ([conn_string](const crow::request& req) {
        try {
            auto body = nlohmann::json::parse(req.body, nullptr, false);
            if (body.is_discarded()) {
                return make_response(400, R"({"error":"Invalid JSON"})");
            }

            std::string username = body.value("username", "");
            std::string password = body.value("password", "");

            if (username.empty() || password.empty()) {
                return make_response(400, R"({"error":"Username and password required"})");
            }

            Database db(conn_string);
            UserRepository users(db);

            if (!users.verify_password(username, password)) {
                return make_response(401, R"({"error":"Invalid credentials"})");
            }

            auto user_opt = users.find_by_username(username);
            if (!user_opt) {
                return make_response(404, R"({"error":"User not found"})");
            }

            nlohmann::json res;
            res["id"]       = user_opt->id;
            res["username"] = user_opt->username;
            return make_response(200, res.dump());

        } catch (const std::exception& e) {
            std::cerr << "Login error: " << e.what() << "\n";
            return make_response(500, R"({"error":"Internal server error"})");
        }
    });
}