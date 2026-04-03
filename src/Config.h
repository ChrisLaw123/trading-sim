#pragma once
#include <string>
#include <map>
#include <fstream>
#include <stdexcept>

class Config {
public: 
    Config(const std::string& path = ".env");
        
    const std::string& get(const std::string& key);
    std::string db_connection_string();
    validate();
    
private:
    std::map<std::string, std::string> data_;
};

Config::Config(const std::string& path){
std::ifstream file(path);

    if(!file.is_open()){
        throw std::runtime_error("Cannot open config file: " + path);
    }

    std::string line;

    while(std::getline(file, line)){
        if(line.empty() || line[0] == '#'){
            continue;
        }
            auto pos = line.find('=');

            if(pos == std::sting::npos){
            continue;
        }
        
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1); 
                
        data_[key] = value;
    }
};

const std::string& get(const std::string& key) {
    auto it = data_.find(key);

    if(it == data_.end()){
        throw std::runtime_error("Missing config key: " + key);
    }

    return it->second;
}

std::string db_connection_string() {
    std::string str = 
    "host=" + get("DB_HOST") +
    "port=" + get("DB_PORT") +
    "dbname=" + get("DB_NAME") +
    "user=" + get("DB_USER") +
    "password=" + get("DB_PASSWORD");

    return str;
}

void validate() {
get(DB_HOST);
get(DB_PORT);
get(DB_NAME);
get(DB_USER);
get(DB_PASSWORD);
get(ALPACA_API_KEY);
get(ALPACA_SECRET_KEY);
get(ALPACA_BASE_URL);
get(STARTING_BALANCE);
}