#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "Config.h"

namespace {

// Writes a throwaway .env and cleans it up, so each case can describe exactly
// the file shape it cares about.
class ConfigFile {
public:
    explicit ConfigFile(const std::string& contents) {
        path_ = std::filesystem::temp_directory_path() /
                ("trading_sim_test_" + std::to_string(next_id()) + ".env");

        std::ofstream out(path_, std::ios::binary);
        out << contents;
    }

    ~ConfigFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    ConfigFile(const ConfigFile&) = delete;
    ConfigFile& operator=(const ConfigFile&) = delete;

    std::string path() const { return path_.string(); }

private:
    static int next_id() {
        static std::atomic<int> counter{0};
        return ++counter;
    }

    std::filesystem::path path_;
};

std::string complete_env() {
    return "DB_HOST=localhost\n"
           "DB_PORT=5432\n"
           "DB_NAME=trading_sim\n"
           "DB_USER=admin\n"
           "DB_PASSWORD=secret\n"
           "ALPACA_API_KEY=key\n"
           "ALPACA_SECRET_KEY=secret\n"
           "ALPACA_BASE_URL=https://data.alpaca.markets\n"
           "STARTING_BALANCE=100000.00\n";
}

} // namespace

TEST(ConfigParsing, ThrowsWhenFileMissing) {
    EXPECT_THROW(Config("definitely_not_a_real_file.env"), std::runtime_error);
}

TEST(ConfigParsing, StripsCarriageReturnsFromCrlfFiles) {
    // A .env saved on Windows used to leave \r on every value, which silently
    // corrupted the Postgres connection string.
    ConfigFile file("DB_HOST=localhost\r\nDB_PORT=5432\r\n");
    Config config(file.path());

    EXPECT_EQ(config.get("DB_HOST"), "localhost");
    EXPECT_EQ(config.get("DB_PORT"), "5432");
}

TEST(ConfigParsing, TrimsSurroundingWhitespace) {
    ConfigFile file("   DB_HOST   =   localhost   \n");
    Config config(file.path());

    EXPECT_EQ(config.get("DB_HOST"), "localhost");
}

TEST(ConfigParsing, StripsMatchingQuotes) {
    ConfigFile file("A=\"quoted value\"\nB='single quoted'\nC=\"unbalanced\n");
    Config config(file.path());

    EXPECT_EQ(config.get("A"), "quoted value");
    EXPECT_EQ(config.get("B"), "single quoted");
    EXPECT_EQ(config.get("C"), "\"unbalanced");
}

TEST(ConfigParsing, SkipsCommentsAndBlankLines) {
    ConfigFile file("# a comment\n\n   \nDB_HOST=localhost\n# trailing comment\n");
    Config config(file.path());

    EXPECT_EQ(config.get("DB_HOST"), "localhost");
    EXPECT_THROW(config.get("# a comment"), std::runtime_error);
}

TEST(ConfigParsing, AcceptsExportPrefix) {
    ConfigFile file("export DB_HOST=localhost\n");
    Config config(file.path());

    EXPECT_EQ(config.get("DB_HOST"), "localhost");
}

TEST(ConfigParsing, KeepsEqualsSignsInsideValues) {
    // Base64 secrets routinely end in padding.
    ConfigFile file("ALPACA_SECRET_KEY=abc==\n");
    Config config(file.path());

    EXPECT_EQ(config.get("ALPACA_SECRET_KEY"), "abc==");
}

TEST(ConfigParsing, IgnoresLinesWithoutSeparator) {
    ConfigFile file("garbage line\nDB_HOST=localhost\n");
    Config config(file.path());

    EXPECT_EQ(config.get("DB_HOST"), "localhost");
}

TEST(ConfigGet, ThrowsOnMissingKey) {
    ConfigFile file("DB_HOST=localhost\n");
    Config config(file.path());

    EXPECT_THROW(config.get("NOPE"), std::runtime_error);
}

TEST(ConfigGet, TreatsEmptyValueAsMissing) {
    // .env.example ships every key with an empty value; a half-filled copy
    // should fail loudly at startup rather than connect to "host= port=".
    ConfigFile file("DB_HOST=\n");
    Config config(file.path());

    EXPECT_THROW(config.get("DB_HOST"), std::runtime_error);
}

TEST(ConfigGetOr, FallsBackForMissingAndEmptyKeys) {
    ConfigFile file("SET=value\nEMPTY=\n");
    Config config(file.path());

    EXPECT_EQ(config.get_or("SET", "fallback"), "value");
    EXPECT_EQ(config.get_or("EMPTY", "fallback"), "fallback");
    EXPECT_EQ(config.get_or("ABSENT", "fallback"), "fallback");
}

TEST(ConfigConnectionString, ComposesEveryField) {
    ConfigFile file(complete_env());
    Config config(file.path());

    EXPECT_EQ(config.db_connection_string(),
              "host=localhost port=5432 dbname=trading_sim user=admin password=secret");
}

TEST(ConfigStartingBalance, ParsesDecimalValue) {
    ConfigFile file(complete_env());
    Config config(file.path());

    EXPECT_DOUBLE_EQ(config.starting_balance(), 100000.00);
}

TEST(ConfigStartingBalance, RejectsNonNumericAndNonPositive) {
    {
        ConfigFile file("STARTING_BALANCE=lots\n");
        EXPECT_THROW(Config(file.path()).starting_balance(), std::runtime_error);
    }
    {
        ConfigFile file("STARTING_BALANCE=100000abc\n");
        EXPECT_THROW(Config(file.path()).starting_balance(), std::runtime_error);
    }
    {
        ConfigFile file("STARTING_BALANCE=0\n");
        EXPECT_THROW(Config(file.path()).starting_balance(), std::runtime_error);
    }
    {
        ConfigFile file("STARTING_BALANCE=-5\n");
        EXPECT_THROW(Config(file.path()).starting_balance(), std::runtime_error);
    }
}

TEST(ConfigValidate, PassesOnCompleteFileAndFailsOnPartial) {
    {
        ConfigFile file(complete_env());
        EXPECT_NO_THROW(Config(file.path()).validate());
    }
    {
        ConfigFile file("DB_HOST=localhost\n");
        EXPECT_THROW(Config(file.path()).validate(), std::runtime_error);
    }
}
