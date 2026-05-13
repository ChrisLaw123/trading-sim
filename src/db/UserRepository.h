#pragma once

#include <pqxx/pqxx>
#include <string>
#include <vector>
#include <optional>
#include <stdexcept>
#include <tuple>
#include "db/Database.h"
#include "models/User.h"

class UserRepository {
public:
    explicit UserRepository(Database& db) : db_(db) {}

    User create(const std::string& username) {
    std::cerr << "create: opening transaction\n";
    pqxx::work txn(db_.conn());
    std::cerr << "create: running query\n";
    try {
        auto result = txn.exec_params(
            "INSERT INTO users (username) VALUES ($1) RETURNING id, username, balance::float8, created_at::text",
            username
        );
        std::cerr << "create: query done, building user\n";
        User user;
        user.id         = result[0][0].as<std::string>();
        user.username   = result[0][1].as<std::string>();
        user.balance    = result[0][2].as<double>();
        user.created_at = result[0][3].as<std::string>();
        std::cerr << "create: committing\n";
        txn.commit();
        std::cerr << "create: done\n";
        return user;
    } catch (const pqxx::unique_violation&) {
        throw std::runtime_error("Username already taken");
    }
}

    std::optional<User> find_by_id(const std::string& id) {
        pqxx::work txn(db_.conn());

        auto result = txn.exec_params(
            "SELECT id, username, balance::float8, created_at::text FROM users WHERE id = $1",
            id
        );

        txn.commit(); 

        if (result.empty()) {
            return std::nullopt;
        }

        User user;
        user.id             = result[0][0].as<std::string>();
        user.username       = result[0][1].as<std::string>();
        user.balance        = result[0][2].as<double>();
        user.created_at     = result[0][3].as<std::string>();

        return user;
    }

    std::optional<User> find_by_username(const std::string& username) {
        pqxx::work txn(db_.conn());

        auto result = txn.exec_params(
            "SELECT id, username, balance::float8, created_at::text FROM users WHERE username= $1",
            username
        );

        txn.commit();

        if (result.empty()) {
            return std::nullopt;
        }

        User user;
        user.id             = result[0][0].as<std::string>();
        user.username       = result[0][1].as<std::string>();
        user.balance        = result[0][2].as<double>();
        user.created_at     = result[0][3].as<std::string>();

        return user;
    }

    void update_cash(const std::string& user_id, double new_balance) {
        pqxx::work txn(db_.conn());

        txn.exec_params(
            "UPDATE users SET balance = $1 WHERE id = $2",
            new_balance, user_id
        );

        txn.commit();
    }

    std::vector<std::tuple<std::string, double, double>> leaderboard() {
        
        try {
            pqxx::work txn(db_.conn());

            auto result = txn.exec_params(
                "SELECT u.username, u.balance::float8, "
                "COALESCE(SUM(h.shares * p.price), 0)::float8 AS equity "
                "FROM users u "
                "LEFT JOIN holdings h ON h.user_id = u.id "
                "LEFT JOIN prices p ON p.symbol = h.symbol "
                "GROUP BY u.id, u.username, u.balance "
                "ORDER BY (u.balance + COALESCE(SUM(h.shares * p.price), 0)) DESC "
            );

            txn.commit();

            std::vector<std::tuple<std::string, double, double>> board;

            for (auto row : result) {
                board.push_back(std::make_tuple(
                    row[0].as<std::string>(),   //username
                    row[1].as<double>(),        //cash balance
                    row[2].as<double>()         //equity
                ));
            }

            return board;
        } catch (const std::exception& e) {
            std::cerr << "Leaderboard query error: " << e.what() << "\n";
            return {};
        }
    }
    
private:
    Database& db_;
};