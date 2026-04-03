#pragma once
#include <string>

struct Trade {
    std::string id;
    std::string user_id;
    std::string symbol;
    std::string side;
    double      shares;
    double      price;
    double      total;
    std::string executed_at;
};