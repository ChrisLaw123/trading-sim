#pragma once

#include <pqxx/pqxx>
#include <string>
#include <vector>
#include <optional>
#include <stdexcept>
#include "db/Database.h"
#include "models/Trade.h"

class TradeRepository {
public:
    explicit TradeRepository(Database& db) : db_(db) {}

    void create(const std::string& user_id, const std::string& symbol, const std::string& side, double shares, double price, double total) {
        pqxx::work txn(db_.conn());

        txn.exec_params(
            "INSERT INTO trades (user_id, symbol, side, shares, price, total) "
            "VALUES ($1, $2, $3, $4, $5, $6)",
            user_id, symbol, side, shares, price, total
        );

        txn.commit();

    }

    std::vector<Trade> get_by_user(const std::string& user_id, int limit = 50) {
        pqxx::work txn(db_.conn());

        auto result = txn.exec_params(
            "SELECT id, user_id, symbol, side, shares::float8, price::float8, total::float8, executed_at::text "
            "FROM trades "
            "WHERE user_id = $1 "
            "ORDER BY executed_at DESC "
            "LIMIT $2",
            user_id, limit
        );

        txn.commit();

        std::vector<Trade> trades;

        for (auto row : result) {
            Trade t;
            t.id            = row[0].as<std::string>();
            t.user_id       = row[1].as<std::string>();
            t.symbol        = row[2].as<std::string>();
            t.side          = row[3].as<std::string>();
            t.shares        = row[4].as<double>();
            t.price         = row[5].as<double>();
            t.total         = row[6].as<double>();
            t.executed_at   = row[7].as<std::string>();

            trades.push_back(t);
        }

        return trades;
    }

private:
    Database& db_;
};