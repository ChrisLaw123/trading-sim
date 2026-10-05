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

    void add_shares(pqxx::work& txn, const std::string& user_id, const std::string& symbol, double shares) {
        txn.exec_params(
            "INSERT INTO holdings (user_id, symbol, shares) "
            "VALUES ($1, $2, $3::numeric) "
            "ON CONFLICT (user_id, symbol) "
            "DO UPDATE SET shares = holdings.shares + EXCLUDED.shares",
            user_id, symbol, shares
        );
    }

    // Guarded relative decrement: the "do they hold enough" test is part of the
    // write, so two concurrent sells cannot both pass a check that was true for
    // only one of them. Returns the remaining shares, or nullopt if rejected.
    std::optional<double> remove_shares(pqxx::work& txn, const std::string& user_id,
                                        const std::string& symbol, double shares) {
        auto result = txn.exec_params(
            "UPDATE holdings SET shares = shares - $1::numeric "
            "WHERE user_id = $2 AND symbol = $3 AND shares >= $1::numeric "
            "RETURNING shares::float8",
            shares, user_id, symbol
        );

        if (result.empty()) {
            return std::nullopt;
        }

        return result[0][0].as<double>();
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

        return read_holding(result[0]);
    }

    std::vector<Holding> get_by_user(const std::string& user_id) {
        pqxx::work txn(db_.conn());

        auto result = txn.exec_params(
            "SELECT id, user_id, symbol, shares::float8 "
            "FROM holdings "
            "WHERE user_id = $1 AND shares > 0 "
            "ORDER BY symbol",
            user_id
        );

        txn.commit();

        std::vector<Holding> holdings;

        for (auto row : result) {
            holdings.push_back(read_holding(row));
        }

        return holdings;
    }

private:
    static Holding read_holding(const pqxx::row& row) {
        Holding h;
        h.id      = row[0].as<std::string>();
        h.user_id = row[1].as<std::string>();
        h.symbol  = row[2].as<std::string>();
        h.shares  = row[3].as<double>();
        return h;
    }

    Database& db_;
};
