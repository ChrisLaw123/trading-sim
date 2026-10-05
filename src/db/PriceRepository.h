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
            "VALUES ($1, $2::numeric, now()) "
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
            stock_prices.push_back(read_price(row));
        }

        return stock_prices;
    }

    std::optional<Price> get(const std::string& symbol) {
        pqxx::work txn(db_.conn());
        auto price = get(txn, symbol);
        txn.commit();
        return price;
    }

    std::optional<Price> get(pqxx::work& txn, const std::string& symbol) {
        auto result = txn.exec_params(
            "SELECT symbol, price::float8, updated_at::text "
            "FROM prices "
            "WHERE symbol = $1",
            symbol
        );

        if (result.empty()) {
            return std::nullopt;
        }

        return read_price(result[0]);
    }

private:
    static Price read_price(const pqxx::row& row) {
        Price p;
        p.symbol     = row[0].as<std::string>();
        p.price      = row[1].as<double>();
        p.updated_at = row[2].as<std::string>();
        return p;
    }

    Database& db_;
};
