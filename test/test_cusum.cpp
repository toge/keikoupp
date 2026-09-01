#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

// 決定的 LCG ノイズ (test_analyzer と同じ系列; シード変更で期待値が変わるため固定)
static uint32_t lcg_state = 12345u;
static double lcg01() {
    lcg_state = lcg_state * 1103515245u + 12345u;
    return static_cast<double>(lcg_state >> 8) / 16777216.0;
}

#include <keikoupp/analyzer.hpp>

using keikoupp::TimeMode;

// 計画書の CFG{0.3, 0.05, 1.0, 0.05, 1.0, 40, 2} はノイズ無し定常系列を
// 前提にしていたが、MAD は 1e-12 下限のため無ノイズ系列では外れ値が
// 正規化残差 ~1e13 となり、単独外れでも spike / shift が必ず発火して
// 「単独外れは不発火」を満たせない。そこでノイズ基底 (alt_noise) を入れ
// MAD を実スケールにし、spike_confirm の連続確認で分離させる構成に調整した。
constexpr auto SPIKE_CFG = keikoupp::Config{0.2, 2.0, 8.0, 99.0, 99.0, 20, 3};
constexpr auto SHIFT_CFG = keikoupp::Config{0.2, 99.0, 99.0, 2.0, 6.0, 20, 3};

// 決定論的な ±2 交互ノイズ (MAD が実スケールになる)
static std::vector<double> alt_noise(int n) {
    std::vector<double> v;
    for (int i = 0; i < n; ++i) v.push_back(10.0 + 2.0 * (i % 2 ? 1.0 : -1.0));
    return v;
}

template <keikoupp::Config C>
static std::vector<keikoupp::event> run_series(const std::vector<double>& seq) {
    std::vector<keikoupp::event> evs;
    auto cb = [&](keikoupp::event e, double, double) { evs.push_back(e); };
    keikoupp::analyzer<TimeMode::fixed, C, decltype(cb)> a{cb};
    for (double v : seq) a.push(v);
    return evs;
}

static size_t count(const std::vector<keikoupp::event>& evs, keikoupp::event want) {
    return static_cast<size_t>(std::count(evs.begin(), evs.end(), want));
}

TEST_CASE("pure noise emits no events") {
    std::vector<double> seq = alt_noise(300);
    REQUIRE(run_series<SPIKE_CFG>(seq).empty());
}

TEST_CASE("single outlier does NOT fire spike (noise separation)") {
    // spike_confirm=3: 単独1点の外れ (10σ) は連続超過が持続せず不発火
    auto seq = alt_noise(40);
    seq.push_back(50.0);
    auto tail = alt_noise(25);
    seq.insert(seq.end(), tail.begin(), tail.end());
    auto evs = run_series<SPIKE_CFG>(seq);
    REQUIRE(count(evs, keikoupp::event::spike) == 0);
    REQUIRE(evs.empty());  // 不発火なら他イベントも無し
}

TEST_CASE("sustained spike fires spike event per stretch") {
    // 50/10 交互の持続外れを 2 回 → 各ストレッチでちょうど 1 回ずつ発火
    std::vector<double> seq = alt_noise(40);
    for (int i = 0; i < 10; ++i) seq.push_back(i % 2 ? 50.0 : 10.0);
    for (double v : alt_noise(20)) seq.push_back(v);
    for (int i = 0; i < 10; ++i) seq.push_back(i % 2 ? 50.0 : 10.0);
    for (double v : alt_noise(20)) seq.push_back(v);
    REQUIRE(count(run_series<SPIKE_CFG>(seq), keikoupp::event::spike) == 2);
}

TEST_CASE("persistent level shift fires shift_up") {
    // 水準 10 → 33 (+23 に対し MAD≈2.4) を持続: 連続プラス偏差が shift_h を超えて
    // shift_up が発火する。シフト後のノイズだけで su が誤発火しないことも確認する。
    std::vector<double> seq;
    for (int i = 0; i < 40; ++i) seq.push_back(10.0 + 4.0 * lcg01());
    for (int i = 0; i < 30; ++i) seq.push_back(33.0 + 4.0 * lcg01());
    auto evs = run_series<SHIFT_CFG>(seq);
    REQUIRE(count(evs, keikoupp::event::shift_up) >= 1);
    REQUIRE(count(evs, keikoupp::event::spike) == 0);  // spike 側は無効化
}