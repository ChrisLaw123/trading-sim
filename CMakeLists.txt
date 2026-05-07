cmake_minimum_required(VERSION 3.20)
project(TradingSim VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_BINARY_DIR}")

find_package(libpqxx REQUIRED)
find_package(Crow REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(CURL REQUIRED)
find_package(GTest REQUIRED)

file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(trading_sim ${SOURCES})

target_include_directories(trading_sim PRIVATE src)

target_link_libraries(trading_sim PRIVATE
    libpqxx::pqxx
    Crow::Crow
    nlohmann_json::nlohmann_json
    CURL::libcurl
)

file(GLOB TEST_SOURCES "tests/*.cpp")
if(TEST_SOURCES)
    add_executable(run_tests ${TEST_SOURCES})
    target_include_directories(run_tests PRIVATE src)
    target_link_libraries(run_tests PRIVATE
        libpqxx::pqxx
        nlohmann_json::nlohmann_json
        CURL::libcurl
        GTest::gtest_main
    )
    enable_testing()
    add_test(NAME unit_tests COMMAND run_tests)
endif()