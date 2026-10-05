#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

#include "api/Validator.h"

TEST(IsValidSymbol, AcceptsOnlyTheTradeableUniverse) {
    EXPECT_TRUE(Validator::is_valid_symbol("AAPL"));
    EXPECT_TRUE(Validator::is_valid_symbol("WMT"));
    EXPECT_FALSE(Validator::is_valid_symbol("aapl"));
    EXPECT_FALSE(Validator::is_valid_symbol("FAKE"));
    EXPECT_FALSE(Validator::is_valid_symbol(""));
}

TEST(IsValidSide, AcceptsUppercaseBuyAndSellOnly) {
    EXPECT_TRUE(Validator::is_valid_side("BUY"));
    EXPECT_TRUE(Validator::is_valid_side("SELL"));
    EXPECT_FALSE(Validator::is_valid_side("buy"));
    EXPECT_FALSE(Validator::is_valid_side("HOLD"));
    EXPECT_FALSE(Validator::is_valid_side(""));
}

TEST(IsValidShares, AcceptsPositiveFiniteAmounts) {
    EXPECT_TRUE(Validator::is_valid_shares(1.0));
    EXPECT_TRUE(Validator::is_valid_shares(0.000001));
    EXPECT_TRUE(Validator::is_valid_shares(1e9));
}

TEST(IsValidShares, RejectsZeroAndNegative) {
    // A negative quantity previously slipped past the router, which only
    // checked for zero, and turned a buy into free money.
    EXPECT_FALSE(Validator::is_valid_shares(0.0));
    EXPECT_FALSE(Validator::is_valid_shares(-1.0));
    EXPECT_FALSE(Validator::is_valid_shares(-0.0001));
}

TEST(IsValidShares, RejectsNonFiniteAndOversized) {
    EXPECT_FALSE(Validator::is_valid_shares(std::numeric_limits<double>::quiet_NaN()));
    EXPECT_FALSE(Validator::is_valid_shares(std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(Validator::is_valid_shares(-std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(Validator::is_valid_shares(1e9 + 1));
}
