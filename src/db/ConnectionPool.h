#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "db/Database.h"

// A fixed set of reusable Postgres connections. Opening a connection per HTTP
// request costs a TCP handshake plus authentication every time and will exhaust
// max_connections under even light polling, so handlers borrow from here instead.
//
// Every slot is kept for the lifetime of the pool. A slot may hold a null or a
// dead connection; acquire() reconnects it, which is the one place where a
// connection failure can throw somewhere a handler's try/catch will see it.
class ConnectionPool {
public:
    ConnectionPool(std::string connection_string, std::size_t size)
        : connection_string_(std::move(connection_string)) {
        if (size == 0) {
            throw std::invalid_argument("Connection pool size must be at least 1");
        }

        slots_.reserve(size);

        // Open one eagerly so a bad connection string fails at startup rather
        // than on the first request. The rest connect on demand.
        slots_.push_back(std::make_unique<Database>(connection_string_));

        for (std::size_t i = 1; i < size; i++) {
            slots_.push_back(nullptr);
        }
    }

    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    // Borrows a connection and returns it to the pool on destruction.
    class Handle {
    public:
        Handle(ConnectionPool& pool, std::unique_ptr<Database> db)
            : pool_(&pool), db_(std::move(db)) {}

        Handle(Handle&& other) noexcept
            : pool_(other.pool_), db_(std::move(other.db_)) {
            other.pool_ = nullptr;
        }

        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;
        Handle& operator=(Handle&&) = delete;

        ~Handle() {
            if (pool_) {
                pool_->give_back(std::move(db_));
            }
        }

        Database& db()        { return *db_; }
        Database* operator->() { return db_.get(); }

    private:
        ConnectionPool* pool_;
        std::unique_ptr<Database> db_;
    };

    Handle acquire() {
        std::unique_ptr<Database> db;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !slots_.empty(); });
            db = std::move(slots_.back());
            slots_.pop_back();
        }

        if (!db || !db->is_open()) {
            try {
                db = std::make_unique<Database>(connection_string_);
            } catch (...) {
                give_back(nullptr);  // never lose the slot
                throw;
            }
        }

        return Handle(*this, std::move(db));
    }

private:
    void give_back(std::unique_ptr<Database> db) noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            slots_.push_back(std::move(db));  // reserved up front, cannot reallocate
        }
        cv_.notify_one();
    }

    std::string connection_string_;
    std::vector<std::unique_ptr<Database>> slots_;
    std::mutex mutex_;
    std::condition_variable cv_;
};
