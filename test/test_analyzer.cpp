#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include <keikoupp/analyzer.hpp>

using keikoupp::TimeMode;
using Catch::Approx;

// 使用する Config
constexpr auto CFG = keikoupp::Config{0.2, 3.0, 10.0, 0.0, 3.0, 20, 3};

TEST_CASE("EMA converges on constant series") {
    keikoupp::analyzer<TimeMode::fixed, CFG> a;
    for (int i = 0; i < 100; ++i) a.push(10.0);
    REQUIRE(a.ema() == Approx(10.0).margin(1e-9));
}

TEST_CASE("slope is zero on constant series, trend flat") {
    keikoupp::analyzer<TimeMode::fixed, CFG> a;
    for (int i = 0; i < 100; ++i) a.push(10.0);
    REQUIRE(a.slope() == Approx(0.0).margin(1e-9));
    REQUIRE(a.trend() == keikoupp::analyzer<TimeMode::fixed, CFG>::trend::flat);
}

TEST_CASE("trend is unknown and slope NaN before window fills") {
    keikoupp::analyzer<TimeMode::fixed, CFG> a;
    for (int i = 0; i < 5; ++i) a.push(10.0);
    REQUIRE(a.trend() == keikoupp::analyzer<TimeMode::fixed, CFG>::trend::unknown);
    REQUIRE(std::isnan(a.slope()));
}