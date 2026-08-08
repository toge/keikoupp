#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <vector>

#include <keikoupp/analyzer.hpp>

using keikoupp::TimeMode;
using Catch::Approx;

// 使用する Config
constexpr auto CFG = keikoupp::Config{0.2, 3.0, 10.0, 0.0, 3.0, 20, 3};
// イベント経路テスト用: 対象以外の検知器を無効化 (k を大きくして飽和させない)
constexpr auto SPIKE_CFG = keikoupp::Config{0.2, 2.0, 8.0, 99.0, 99.0, 20, 3};
constexpr auto SHIFT_CFG = keikoupp::Config{0.2, 10.0, 50.0, 2.0, 6.0, 20, 3};
constexpr auto TREND_CFG = keikoupp::Config{0.2, 20.0, 100.0, 20.0, 200.0, 20, 3};

// 決定的 LCG ノイズ (シード変更で期待値が変わるため固定)
static uint32_t lcg_state = 12345u;
static double lcg01() {
    lcg_state = lcg_state * 1103515245u + 12345u;
    return static_cast<double>(lcg_state >> 8) / 16777216.0;
}

// ±2 交互ノイズ (median が d=±2 に張り付く決定論的パターン)
static std::vector<double> alt_noise(int n) {
    std::vector<double> v;
    for (int i = 0; i < n; ++i) v.push_back(10.0 + 2.0 * (i % 2 ? 1.0 : -1.0));
    return v;
}

template <keikoupp::Config C>
static std::vector<keikoupp::event> run_series(const std::vector<double>& seq) {
    keikoupp::analyzer<TimeMode::fixed, C> a;
    std::vector<keikoupp::event> evs;
    a.on_event([&](keikoupp::event e, double, double) { evs.push_back(e); });
    for (double v : seq) a.push(v);
    return evs;
}

static size_t count(const std::vector<keikoupp::event>& evs, keikoupp::event want) {
    return static_cast<size_t>(std::count(evs.begin(), evs.end(), want));
}

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

TEST_CASE("spike does not fire on a single isolated outlier") {
    // ノイズ中に +40 (20σ) を 1 点だけ挿入: 連続超過が spike_confirm=3 に届かない
    auto seq = alt_noise(40);
    seq.push_back(50.0);
    auto tail = alt_noise(25);
    seq.insert(seq.end(), tail.begin(), tail.end());
    REQUIRE(count(run_series<SPIKE_CFG>(seq), keikoupp::event::spike) == 0);
}

TEST_CASE("spike fires on sustained outlier and re-arms after reset") {
    // 50/10 交互の持続外れを 2 回 (間は通常ノイズ) → 各ストレッチで 1 回ずつ発火
    std::vector<double> seq = alt_noise(40);
    for (int i = 0; i < 10; ++i) seq.push_back(i % 2 ? 50.0 : 10.0);
    for (double v : alt_noise(20)) seq.push_back(v);
    for (int i = 0; i < 10; ++i) seq.push_back(i % 2 ? 50.0 : 10.0);
    for (double v : alt_noise(20)) seq.push_back(v);
    // 発火ごとに CUSUM と連続カウンタが 0 に戻るため、2 ストレッチで 2 回独立に発火
    REQUIRE(count(run_series<SPIKE_CFG>(seq), keikoupp::event::spike) == 2);
}

TEST_CASE("shift fires up on level rise and down on level fall") {
    std::vector<double> seq;
    for (int i = 0; i < 40; ++i) seq.push_back(10.0 + 4.0 * lcg01());       // ノイズ ±2
    for (int i = 0; i < 30; ++i) seq.push_back(33.0 + 4.0 * lcg01());       // 水準 +25
    for (int i = 0; i < 20; ++i) seq.push_back(10.0 + 4.0 * lcg01());       // 復帰
    for (int i = 0; i < 30; ++i) seq.push_back(-27.0 + 4.0 * lcg01());      // 水準 -35
    auto evs = run_series<SHIFT_CFG>(seq);
    REQUIRE(count(evs, keikoupp::event::shift_up) > 0);
    REQUIRE(count(evs, keikoupp::event::shift_down) > 0);
    REQUIRE(count(evs, keikoupp::event::spike) == 0);  // spike 側は無効化
}

TEST_CASE("trend fires only when classification changes") {
    // 定常 → +1.2/サンプル上昇 → 定常 → -1.2/サンプル下降: 変化時のみ計 2 回
    lcg_state = 7;  // 期待値を固定するためシードを明示
    std::vector<double> seq;
    for (int i = 0; i < 50; ++i) seq.push_back(10.0 + (lcg01() - 0.5));
    for (int i = 0; i < 40; ++i) seq.push_back(9.5 + 1.2 * (i + 1) + lcg01());
    for (int i = 0; i < 50; ++i) seq.push_back(58.0 + (lcg01() - 0.5));
    for (int i = 0; i < 40; ++i) seq.push_back(57.5 - 1.2 * (i + 1) + lcg01());
    auto evs = run_series<TREND_CFG>(seq);
    REQUIRE(count(evs, keikoupp::event::trend_up) == 1);
    REQUIRE(count(evs, keikoupp::event::trend_down) == 1);
    REQUIRE(evs.size() == 2);  // 分類変化時のみ発火 (フラット維持中は追加発火なし)
}

TEST_CASE("realtime mode: push(t, v) EMA, slope, forecast") {
    keikoupp::analyzer<TimeMode::realtime, CFG> a;
    for (int i = 0; i < 30; ++i) a.push(2.0 * i, 10.0);
    REQUIRE(a.ema() == Approx(10.0).margin(1e-9));
    REQUIRE(a.slope() == Approx(0.0).margin(1e-9));
    REQUIRE(a.forecast(20.0) == Approx(10.0).margin(1e-9));
}

// モード誤用のコンパイル時拒否の静的検証用コンセプト
// (非テンプレート文脈の requires-expr は ill-formed 式で診断されるため、
//  テンプレート化して置換失敗として扱わせる)
template <typename A>
concept has_fixed_push = requires(A& a) { a.push(1.0); };
template <typename A>
concept has_realtime_push = requires(A& a) { a.push(1.0, 2.0); };
template <typename A>
concept has_next = requires(A& a) { a.next(); };
template <typename A>
concept has_forecast = requires(A& a) { a.forecast(1.0); };

TEST_CASE("wrong-mode member calls are rejected at compile time") {
    using fixed_an = keikoupp::analyzer<TimeMode::fixed, CFG>;
    using rt_an = keikoupp::analyzer<TimeMode::realtime, CFG>;
    // 正しいモードの呼び出しは有効
    static_assert(has_fixed_push<fixed_an>);
    static_assert(has_next<fixed_an>);
    static_assert(has_realtime_push<rt_an>);
    static_assert(has_forecast<rt_an>);
    // 誤ったモードの呼び出しは ill-formed (コンパイル時に排除)
    static_assert(!has_realtime_push<fixed_an>);
    static_assert(!has_forecast<fixed_an>);
    static_assert(!has_fixed_push<rt_an>);
    static_assert(!has_next<rt_an>);
    SUCCEED();
}