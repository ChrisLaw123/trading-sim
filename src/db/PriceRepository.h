#pragma once

#include <pqxx/pqxx>
#include <string>
#include <vector>
#include <optional>
#include <stdexcept>
#include "db/Database.h"
#include "models/Price.h"

class PriceRepository {
public:
    explicit PriceRepository(Database& db) : db_(db) {}

    void upsert(const std::string& symbol, double price) {
        pqxx::work txn(db_.conn());

        txn.exec_params( 
            "INSERT INTO prices (symbol, price, updated_at) "
            "VALUES ($1, $2, now()) "
            "ON CONFLICT (symbol) "
            "DO UPDATE SET price = EXCLUDED.price, updated_at = now()",
            symbol, price
        );

        txn.commit();
    }

    std::vector<Price> get_all() {
        pqxx::work txn(db_.conn());

        auto result = txn.exec_params(
            "SELECT symbol, price::float8, updated_at::text "
            "FROM prices "
            "ORDER BY symbol"
        );

        txn.commit();

        std::vector<Price> stock_prices;

        for (auto row : result) {
            Price p;
            p.symbol        = row[0].as<std::string>();
            p.price         = row[1].as<double>();
            p.updated_at    = row[2].as<std::string>();
            stock_prices.push_back(p);
        }

        return stock_prices;
    }

    std::optional<Price> get(const std::string& symbol) {
        pqxx::work txn(db_.conn());

        auto result = txn.exec_params(
            "SELECT symbol, price::float8, updated_at::text "
            "FROM prices "
            "WHERE symbol = $1",
            symbol
        );

        txn.commit();

        if(result.empty()) {
            return std::nullopt;
        }

        Price p;
        p.symbol        = result[0][0].as<std::string>();
        p.price         = result[0][1].as<double>();
        p.updated_at    = result[0][2].as<std::string>();
        
        return p;
    }

private:
    Database& db_;
};