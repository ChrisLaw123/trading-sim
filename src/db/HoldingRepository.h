#pragma once

#include <pqxx/pqxx>
#include <string>
#include <vector>
#include <optional>
#include <stdexcept>
#include "db/Database.h"
#include "models/Holding.h"

class HoldingRepository {
public:
    explicit HoldingRepository(Database& db) : db_(db) {}

    void upsert(const std::string& user_id, const std::string& symbol, double shares_delta) {
        pqxx::work txn(db_.conn());

        txn.exec_params(
            "INSERT INTO holdings (user_id, symbol, shares) "
            "VALUES ($1, $2, $3) "
            "ON CONFLICT (user_id, symbol) "
            "DO UPDATE SET shares = holdings.shares + EXCLUDED.shares",
            user_id, symbol, shares_delta
        );

        txn.commit();

    }

    std::optional<Holding> get(const std::string& user_id, const std::string& symbol) {
        pqxx::work txn(db_.conn());

        auto result = txn.exec_params(
            "SELECT id, user_id, symbol, shares::float8 "
            "FROM holdings "
            "WHERE user_id = $1 AND symbol = $2",
            user_id, symbol
        );

        txn.commit();

        if (result.empty()) {
            return std::nullopt;
        }

        Holding h;
        h.id        = result[0][0].as<std::string>();
        h.user_id   = result[0][1].as<std::string>();
        h.symbol    = result[0][2].as<std::string>();
        h.shares    = result[0][3].as<double>();

        return h;

    }

    std::vector<Holding> get_by_user(const std::string& user_id) {
        pqxx::work txn(db_.conn());

        auto result = txn.exec_params(
            "SELECT id, user_id, symbol, shares::float8 "
            "FROM holdings "
            "WHERE user_id = $1 AND shares > 0",
            user_id
        );

        txn.commit();

        std::vector<Holding> holdings;

        for (auto row : result) {
            Holding h;
            h.id        = row[0].as<std::string>();
            h.user_id   = row[1].as<std::string>();
            h.symbol    = row[2].as<std::string>();
            h.shares    = row[3].as<double>();

            holdings.push_back(h);
        }

        return holdings;

    }

private:
    Database& db_;
};