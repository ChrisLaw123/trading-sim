#pragma once

#include <cmath>
#include <string>

#include "db/Database.h"
#include "db/UserRepository.h"
#include "db/PriceRepository.h"
#include "db/HoldingRepository.h"
#include "db/TradeRepository.h"
#include "api/Validator.h"

struct TradeResult {
    bool success;
    std::string message;
    double fill_price;
    double total_cost;
    double new_balance;
};

class TradeEngine {
public:
    TradeEngine(Database& db, double starting_balance) :
        db_(db), starting_balance_(starting_balance),
        users_(db), prices_(db), holdings_(db), trades_(db) {}

    // The whole trade is one transaction. Cash, holdings and the trade record
    // commit together or not at all, and the account row is locked for the
    // duration so a concurrent trade cannot read a balance that is about to change.
    TradeResult execute(const std::string& symbol, const std::string& side, double shares) {
        if (!Validator::is_valid_symbol(symbol))    return fail("Invalid Symbol");
        if (!Validator::is_valid_side(side))        return fail("Invalid Side");
        if (!Validator::is_valid_shares(shares))    return fail("Invalid Shares");

        // Match the column scale before computing anything, so the value we charge
        // for is exactly the value we store.
        shares = round_to(shares, 1e6);

        pqxx::work txn(db_.conn());

        // Also takes the row lock for this transaction; see UserRepository.
        User account = users_.get_or_create(txn, starting_balance_);

        auto price_opt = prices_.get(txn, symbol);
        if (!price_opt) return fail("Price not available");

        const double price = price_opt->price;
        const double total = round_to(shares * price, 1e2);

        if (total <= 0.0) return fail("Trade value too small");

        if (side == "BUY") {
            auto new_balance = users_.apply_cash_delta(txn, account.id, -total);
            if (!new_balance) return fail("Insufficient funds");

            holdings_.add_shares(txn, account.id, symbol, shares);
            trades_.create(txn, account.id, symbol, "BUY", shares, price, total);

            txn.commit();
            return {true, "BUY executed", price, total, *new_balance};
        }

        auto remaining = holdings_.remove_shares(txn, account.id, symbol, shares);
        if (!remaining) return fail("Insufficient shares");

        auto new_balance = users_.apply_cash_delta(txn, account.id, total);
        if (!new_balance) return fail("Account not found");

        trades_.create(txn, account.id, symbol, "SELL", shares, price, total);

        txn.commit();
        return {true, "SELL executed", price, total, *new_balance};
    }

private:
    // Returning without committing lets pqxx::work roll back on the way out.
    static TradeResult fail(const std::string& message) {
        return {false, message, 0, 0, 0};
    }

    static double round_to(double value, double scale) {
        return std::round(value * scale) / scale;
    }

    Database& db_;
    double starting_balance_;
    UserRepository users_;
    PriceRepository prices_;
    HoldingRepository holdings_;
    TradeRepository trades_;
};
