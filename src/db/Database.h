#pragma once

#include <pqxx/pqxx>
#include <string>
#include <stdexcept>
#include <memory>

class Database {
public:
    Database(const std::string& connection_string) {
        conn_ = std::make_unique<pqxx::connection>(connection_string);

        if(!conn_->is_open()) {
            throw std::runtime_error("Failed to open database");
        }
    }

    pqxx::connection& conn() {
        return *conn_;
    }

    bool is_open() const {
        return conn_->is_open();
    }

private:
    std::unique_ptr<pqxx::connection> conn_;
};