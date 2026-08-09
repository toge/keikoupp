#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <keikoupp/analyzer.hpp>

using keikoupp::TimeMode;
using Catch::Approx;

// 計画書どおりの CFG (ノイズ無し定常系 → MAD は 1e-12 下限。単調増加では
// 残差がほぼ一定で MAD が小さく、slope だけが大きくなるためトレンド分離は成立する)
constexpr auto CFG = keikoupp::Config{0.2, 0.05, 1.0, 0.05, 1.0, 40, 2};

TEST_CASE("monotonic increase gives rising trend and slope 1") {
    keikoupp::analyzer<TimeMode::fixed, CFG> a;
    for (int i = 0; i < 100; ++i) a.push(static_cast<double>(i));
    REQUIRE(a.slope() == Approx(1.0).margin(0.05));
    REQUIRE(a.trend() == keikoupp::analyzer<TimeMode::fixed, CFG>::trend::rising);
}

TEST_CASE("monotonic decrease gives falling trend and slope -1") {
    keikoupp::analyzer<TimeMode::fixed, CFG> a;
    for (int i = 0; i < 100; ++i) a.push(static_cast<double>(100 - i));
    REQUIRE(a.slope() == Approx(-1.0).margin(0.05));
    REQUIRE(a.trend() == keikoupp::analyzer<TimeMode::fixed, CFG>::trend::falling);
}

TEST_CASE("constant series stays flat") {
    keikoupp::analyzer<TimeMode::fixed, CFG> a;
    for (int i = 0; i < 100; ++i) a.push(10.0);
    REQUIRE(a.slope() == Approx(0.0).margin(1e-9));
    REQUIRE(a.trend() == keikoupp::analyzer<TimeMode::fixed, CFG>::trend::flat);
}

TEST_CASE("trend is unknown before window fills") {
    keikoupp::analyzer<TimeMode::fixed, CFG> a;
    a.push(1.0);
    a.push(2.0);
    REQUIRE(a.trend() == keikoupp::analyzer<TimeMode::fixed, CFG>::trend::unknown);
}

TEST_CASE("fixed and realtime agree for unit-spaced input") {
    keikoupp::analyzer<TimeMode::fixed, CFG> fa;
    keikoupp::analyzer<TimeMode::realtime, CFG> ra;
    for (int i = 0; i < 100; ++i) {
        fa.push(static_cast<double>(i));
        ra.push(static_cast<double>(i), static_cast<double>(i));  // Δt=1 で実時刻=点数
    }
    REQUIRE(ra.slope() == Approx(fa.slope()).margin(0.05));
}

TEST_CASE("forecast extrapolates and interpolates along the fitted line") {
    // 計画書の 2 点のみ版では回帰窓 (window=40) が満ちず slope が NaN になるため、
    // 同じ直線 (t, 5t) を 40 点投入して窓を満たす構成に変更した。
    // 直線 v = 5t なので forecast(5) ≈ 5.0 が期待される。
    keikoupp::analyzer<TimeMode::realtime, CFG> a;
    for (int i = 0; i < 40; ++i) a.push(static_cast<double>(i), 5.0 * i);
    REQUIRE(a.slope() == Approx(5.0).margin(0.05));
    REQUIRE(a.forecast(5.0) == Approx(5.0).margin(0.5));
}