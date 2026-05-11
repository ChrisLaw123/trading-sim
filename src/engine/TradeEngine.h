#pragma once

#include "db/Database.h"
#include "db/UserRepository.h"
#include "db/PriceRepository.h"
#include "db/HoldingRepository.h"
#include "db/TradeRepository.h"
#include "api/Validator.h"
#include <string>

struct TradeResult {
    bool success;
    std::string message;
    double fill_price;
    double total_cost;
    double new_balance;
};

class TradeEngine {
public:
    explicit TradeEngine(Database& db) :
    db_(db), users_(db), prices_(db), holdings_(db), trades_(db) {}

    TradeResult execute(const std::string& user_id, const std::string& symbol, const std::string& side, double shares) {
        //Validate inputs
        if (!Validator::is_valid_uuid(user_id))     return {false, "Invalid User ID", 0, 0, 0};
        if (!Validator::is_valid_symbol(symbol))    return {false, "Invalid Symbol", 0, 0, 0};
        if (!Validator::is_valid_side(side))        return {false, "Invalid Side", 0, 0, 0};
        if (!Validator::is_valid_shares(shares))    return {false, "Invalid Shares", 0, 0, 0};

        //Get current price
        auto price_opt = prices_.get(symbol);
        if(!price_opt) return {false, "Price not available", 0, 0, 0};
        double price = price_opt->price;

        //Get user
        auto user_opt = users_.find_by_id(user_id);
        if(!user_opt) return {false, "User not found", 0, 0, 0};
        auto user = *user_opt;

        double total = shares * price;

        if (side == "BUY") {
            if(user.balance < total) return {false, "Insufficient funds", 0, 0, 0};

            users_.update_cash(user_id, user.balance - total);
            holdings_.upsert(user_id, symbol, shares);
            trades_.create(user_id, symbol, "BUY", shares, price, total);

            return {true, "BUY executed", price, total, user.balance - total};
        }   

        if (side == "SELL") {
            auto holding_opt = holdings_.get(user_id, symbol);
            
            if(!holding_opt || holding_opt->shares < shares) return {false, "Insufficient shares", 0, 0, 0};

            users_.update_cash(user_id, user.balance + total);
            holdings_.upsert(user_id, symbol, -shares);
            trades_.create(user_id, symbol, "SELL", shares, price, total);

            return {true, "SELL executed", price, total, user.balance + total};
        }

        return {false, "Trade Failed", 0, 0, 0};
    }

private:
    Database& db_;
    UserRepository users_;
    PriceRepository prices_;
    HoldingRepository holdings_;
    TradeRepository trades_;
};