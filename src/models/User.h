#pragma once
#include <string>

struct User {
    std::string id;
    std::string username;
    double      balance;
    std::string created_at;
    std::string password_hash;
};