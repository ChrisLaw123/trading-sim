#pragma once
#include <string>
#include <map>
#include <fstream>
#include <stdexcept>

class Config {
public:
    Config(const std::string& path = ".env") {
        std::ifstream file(path);

        if(!file.is_open()){
            throw std::runtime_error("Cannot open config file: " + path);
        }

        std::string line;

        while(std::getline(file, line)){
            // A .env written on Windows arrives with a trailing \r, which would
            // otherwise end up inside every value and corrupt the connection string.
            line = trim(line);

            if(line.empty() || line[0] == '#'){
                continue;
            }

            if(line.rfind("export ", 0) == 0){
                line = trim(line.substr(7));
            }

            auto pos = line.find('=');

            if(pos == std::string::npos){
                continue;
            }

            std::string key   = trim(line.substr(0, pos));
            std::string value = unquote(trim(line.substr(pos + 1)));

            if(key.empty()){
                continue;
            }

            data_[key] = value;
        }
    }

    const std::string& get(const std::string& key) const {
        auto it = data_.find(key);

        if(it == data_.end() || it->second.empty()){
            throw std::runtime_error("Missing config key: " + key);
        }

        return it->second;
    }

    // For keys that are genuinely optional, so callers do not have to catch.
    std::string get_or(const std::string& key, const std::string& fallback) const {
        auto it = data_.find(key);

        if(it == data_.end() || it->second.empty()){
            return fallback;
        }

        return it->second;
    }

    std::string db_connection_string() const {
        return  "host=" + get("DB_HOST")    +
                " port=" + get("DB_PORT")   +
                " dbname=" + get("DB_NAME") +
                " user=" + get("DB_USER")   +
                " password=" + get("DB_PASSWORD");
    }

    double starting_balance() const {
        const std::string& raw = get("STARTING_BALANCE");

        std::size_t consumed = 0;
        double value = 0.0;

        try {
            value = std::stod(raw, &consumed);
        } catch (const std::exception&) {
            throw std::runtime_error("STARTING_BALANCE is not a number: " + raw);
        }

        if(consumed != raw.size() || value <= 0.0){
            throw std::runtime_error("STARTING_BALANCE must be a positive number: " + raw);
        }

        return value;
    }

    void validate() const {
        get("DB_HOST");
        get("DB_PORT");
        get("DB_NAME");
        get("DB_USER");
        get("DB_PASSWORD");
        get("ALPACA_API_KEY");
        get("ALPACA_SECRET_KEY");
        get("ALPACA_BASE_URL");
        starting_balance();
    }

private:
    static std::string trim(const std::string& text) {
        const char* whitespace = " \t\r\n";

        auto first = text.find_first_not_of(whitespace);
        if(first == std::string::npos){
            return "";
        }

        auto last = text.find_last_not_of(whitespace);
        return text.substr(first, last - first + 1);
    }

    static std::string unquote(const std::string& text) {
        if(text.size() >= 2 && (text.front() == '"' || text.front() == '\'') && text.back() == text.front()){
            return text.substr(1, text.size() - 2);
        }

        return text;
    }

    std::map<std::string, std::string> data_;
};
