// keikoupp 自己診断 (demo): 各ステージを検証し、失敗で異常終了する。
// README の使用例相当のシナリオを実行し、EMA / spike / shift / trend /
// fixed-realtime 一致 / forecast を assert で確認する。
// 構成と系列は test_cusum / test_regression と同値 (検証済み) を再現する。
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <keikoupp/keikoupp.hpp>

using keikoupp::TimeMode;
using keikoupp::event;

// test と同値の検知感度 Config
constexpr auto SPIKE_CFG = keikoupp::Config{0.2, 2.0, 8.0, 99.0, 99.0, 20, 3};
constexpr auto SHIFT_CFG = keikoupp::Config{0.2, 99.0, 99.0, 2.0, 6.0, 20, 3};
constexpr auto BASE_CFG  = keikoupp::Config{0.2, 0.05, 1.0, 0.05, 1.0, 40, 2};

// 各ステージを検証する。失敗は stderr に出力して EXIT_FAILURE で終了する。
static void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "demo FAILED: %s\n", what);
        std::exit(EXIT_FAILURE);
    }
}
static void check_near(double a, double b, double tol, const char* what) {
    check(std::fabs(a - b) <= tol, what);
}

// 決定論的な base±2 交互ノイズ (MAD が実スケールになり、単発外れ分離が成立)
static std::vector<double> alt_noise(double base, int n) {
    std::vector<double> v;
    for (int i = 0; i < n; ++i) v.push_back(base + 2.0 * (i % 2 ? 1.0 : -1.0));
    return v;
}

// 決定論的 LCG ノイズ (test_analyzer と同系列。shift はこちらで検証する)
static unsigned lcg_state = 12345u;
static double lcg01() {
    lcg_state = lcg_state * 1103515245u + 12345u;
    return static_cast<double>(lcg_state >> 8) / 16777216.0;
}

template <keikoupp::Config C>
static std::vector<event> run_series(const std::vector<double>& seq) {
    keikoupp::analyzer<TimeMode::fixed, C> a;
    std::vector<event> evs;
    a.on_event([&](event e, double, double) { evs.push_back(e); });
    for (double v : seq) a.push(v);
    return evs;
}

static std::size_t count(const std::vector<event>& evs, event want) {
    return static_cast<std::size_t>(std::count(evs.begin(), evs.end(), want));
}

int main() {
    // 1. EMA 収束 / 定数系列で slope()==0 かつ trend()==flat
    {
        keikoupp::analyzer<TimeMode::fixed, BASE_CFG> a;
        for (int i = 0; i < 100; ++i) a.push(10.0);
        check_near(a.ema(), 10.0, 1e-9, "ema converges on constant series");
        check_near(a.slope(), 0.0, 1e-9, "slope is zero on flat series");
        check(a.trend() == keikoupp::analyzer<TimeMode::fixed, BASE_CFG>::trend::flat,
              "trend is flat on constant series");
    }
    // 2. spike: 単発外れは不発火、spike_confirm 連続超過で発火
    {
        // 単発 1 点の外れ → spike_confirm=3 に届かず不発火
        auto seq = alt_noise(10.0, 40);
        seq.push_back(50.0);
        auto tail = alt_noise(10.0, 25);
        seq.insert(seq.end(), tail.begin(), tail.end());
        check(count(run_series<SPIKE_CFG>(seq), event::spike) == 0,
              "single outlier does not fire spike");

        // 50/10 交互の持続外れ → 発火
        std::vector<double> seq2 = alt_noise(10.0, 40);
        for (int i = 0; i < 10; ++i) seq2.push_back(i % 2 ? 50.0 : 10.0);
        check(count(run_series<SPIKE_CFG>(seq2), event::spike) >= 1,
              "sustained outlier fires spike");
    }
    // 3. shift_up / shift_down 発火
    {
        // 水準 10±4 → 33±4 (+23) を 30 点継続 (test_analyzer と同構成)
        lcg_state = 12345u;
        std::vector<double> up;
        for (int i = 0; i < 40; ++i) up.push_back(10.0 + 4.0 * lcg01());
        for (int i = 0; i < 30; ++i) up.push_back(33.0 + 4.0 * lcg01());
        check(count(run_series<SHIFT_CFG>(up), event::shift_up) >= 1,
              "level rise fires shift_up");

        // 水準 10±4 → -27±4 (-37) を 30 点継続
        lcg_state = 12345u;
        std::vector<double> down;
        for (int i = 0; i < 40; ++i) down.push_back(10.0 + 4.0 * lcg01());
        for (int i = 0; i < 30; ++i) down.push_back(-27.0 + 4.0 * lcg01());
        check(count(run_series<SHIFT_CFG>(down), event::shift_down) >= 1,
              "level fall fires shift_down");
    }
    // 4. trend rising / falling / unknown
    {
        keikoupp::analyzer<TimeMode::fixed, BASE_CFG> up;
        for (int i = 0; i < 100; ++i) up.push(static_cast<double>(i));
        check(up.trend() == keikoupp::analyzer<TimeMode::fixed, BASE_CFG>::trend::rising,
              "monotonic increase gives rising");

        keikoupp::analyzer<TimeMode::fixed, BASE_CFG> down;
        for (int i = 0; i < 100; ++i) down.push(static_cast<double>(100 - i));
        check(down.trend() == keikoupp::analyzer<TimeMode::fixed, BASE_CFG>::trend::falling,
              "monotonic decrease gives falling");

        keikoupp::analyzer<TimeMode::fixed, BASE_CFG> u;
        u.push(1.0);
        u.push(2.0);
        check(u.trend() == keikoupp::analyzer<TimeMode::fixed, BASE_CFG>::trend::unknown,
              "trend is unknown before window fills");
    }
    // 5. fixed / realtime の slope が等間隔 (Δt=1) 入力で一致
    {
        keikoupp::analyzer<TimeMode::fixed, BASE_CFG> fa;
        keikoupp::analyzer<TimeMode::realtime, BASE_CFG> ra;
        for (int i = 0; i < 100; ++i) {
            fa.push(static_cast<double>(i));
            ra.push(static_cast<double>(i), static_cast<double>(i));
        }
        check_near(ra.slope(), fa.slope(), 0.05, "fixed and realtime slope agree");
    }
    // 6. forecast 内挿 (直線 v=5t の窓内時刻)
    {
        keikoupp::analyzer<TimeMode::realtime, BASE_CFG> a;
        for (int i = 0; i < 40; ++i) a.push(static_cast<double>(i), 5.0 * i);
        check_near(a.forecast(5.0), 5.0, 0.5, "forecast interpolates along the line");
    }

    std::puts("demo: all self-checks passed");
    return EXIT_SUCCESS;
}
