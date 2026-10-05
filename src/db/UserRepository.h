#pragma once

#include <pqxx/pqxx>
#include <string>
#include <optional>
#include <stdexcept>
#include <iostream>
#include "db/Database.h"
#include "models/User.h"

// Single-player: there is exactly one account, created on first use.
//
// The users table is kept as-is rather than flattened away, so holdings and trades
// keep their foreign key and adding multiple players (and a leaderboard) later is a
// pure addition rather than a migration that has to backfill user_id.
class UserRepository {
public:
    // The sentinel name for the one account. Nothing displays it; it exists because
    // username is NOT NULL UNIQUE and gives us a stable key to upsert against.
    static constexpr const char* ACCOUNT_NAME = "player";

    explicit UserRepository(Database& db) : db_(db) {}

    // Fetches the account, creating it with the opening balance on first call.
    //
    // ON CONFLICT DO UPDATE performs a write, so this takes a row lock that is held
    // for the rest of the transaction. That is what serialises concurrent trades:
    // a second trade blocks here until the first commits, and then reads the balance
    // the first one left behind rather than a stale copy.
    User get_or_create(pqxx::work& txn, double starting_balance) {
        auto result = txn.exec_params(
            "INSERT INTO users (username, balance) VALUES ($1, $2::numeric) "
            "ON CONFLICT (username) DO UPDATE SET username = EXCLUDED.username "
            "RETURNING id, username, balance::float8, created_at::text",
            ACCOUNT_NAME, starting_balance
        );

        return read_user(result[0]);
    }

    User get_or_create(double starting_balance) {
        pqxx::work txn(db_.conn());
        User user = get_or_create(txn, starting_balance);
        txn.commit();
        return user;
    }

    // Relative, guarded, and atomic. Postgres does the arithmetic on the NUMERIC
    // column, and the WHERE clause makes "can they afford it" part of the write
    // rather than a separate read that another request could race.
    // Returns the new balance, or nullopt when the guard rejected the change.
    std::optional<double> apply_cash_delta(pqxx::work& txn, const std::string& user_id, double delta) {
        auto result = txn.exec_params(
            "UPDATE users SET balance = balance + $1::numeric "
            "WHERE id = $2 AND balance + $1::numeric >= 0 "
            "RETURNING balance::float8",
            delta, user_id
        );

        if (result.empty()) {
            return std::nullopt;
        }

        return result[0][0].as<double>();
    }

private:
    static User read_user(const pqxx::row& row) {
        User user;
        user.id         = row[0].as<std::string>();
        user.username   = row[1].as<std::string>();
        user.balance    = row[2].as<double>();
        user.created_at = row[3].as<std::string>();
        return user;
    }

    Database& db_;
};
