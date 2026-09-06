// keikoupp 自己診断 (demo): 各ステージを検証し、失敗で異常終了する。
// README の使用例相当のシナリオを実行し、EMA / spike / shift / trend /
// fixed-realtime 一致 / forecast を assert で確認する。
// 構成と系列は test_cusum / test_regression と同値 (検証済み) を再現する。
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

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

// 最大系列サイズ
constexpr std::size_t MAX_SEQ = 128;
constexpr std::size_t MAX_EVS = 128;

// 決定論的な base±2 交互ノイズ (MAD が実スケールになり、単発外れ分離が成立)
static void fill_alt_noise(double base, int n, std::array<double, MAX_SEQ>& out, std::size_t& out_n) {
    out_n = static_cast<std::size_t>(n);
    for (int i = 0; i < n; ++i) out[i] = base + 2.0 * (i % 2 ? 1.0 : -1.0);
}

// 決定論的 LCG ノイズ (test_analyzer と同系列。shift はこちらで検証する)
static unsigned lcg_state = 12345u;
static double lcg01() {
    lcg_state = lcg_state * 1103515245u + 12345u;
    return static_cast<double>(lcg_state >> 8) / 16777216.0;
}

template <keikoupp::Config C>
static std::size_t run_series(const double* seq, std::size_t n, std::array<event, MAX_EVS>& evs) {
    std::size_t evs_n = 0;
    auto cb = [&](event e, double, double) { evs[evs_n++] = e; };
    keikoupp::analyzer<TimeMode::fixed, C, decltype(cb)> a{cb};
    for (std::size_t i = 0; i < n; ++i) a.push(seq[i]);
    return evs_n;
}

static std::size_t count_events(const std::array<event, MAX_EVS>& evs, std::size_t n, event want) {
    std::size_t c = 0;
    for (std::size_t i = 0; i < n; ++i) if (evs[i] == want) ++c;
    return c;
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
        std::array<double, MAX_SEQ> seq;
        std::size_t seq_n;
        fill_alt_noise(10.0, 40, seq, seq_n);
        seq[seq_n++] = 50.0;
        std::array<double, MAX_SEQ> tail;
        std::size_t tail_n;
        fill_alt_noise(10.0, 25, tail, tail_n);
        for (std::size_t i = 0; i < tail_n; ++i) seq[seq_n++] = tail[i];
        std::array<event, MAX_EVS> evs;
        auto evs_n = run_series<SPIKE_CFG>(seq.data(), seq_n, evs);
        check(count_events(evs, evs_n, event::spike) == 0,
              "single outlier does not fire spike");

        // 50/10 交互の持続外れ → 発火
        std::array<double, MAX_SEQ> seq2;
        std::size_t seq2_n;
        fill_alt_noise(10.0, 40, seq2, seq2_n);
        for (int i = 0; i < 10; ++i) seq2[seq2_n++] = (i % 2 ? 50.0 : 10.0);
        std::array<event, MAX_EVS> evs2;
        auto evs2_n = run_series<SPIKE_CFG>(seq2.data(), seq2_n, evs2);
        check(count_events(evs2, evs2_n, event::spike) >= 1,
              "sustained outlier fires spike");
    }
    // 3. shift_up / shift_down 発火
    {
        // 水準 10±4 → 33±4 (+23) を 30 点継続 (test_analyzer と同構成)
        lcg_state = 12345u;
        std::array<double, MAX_SEQ> up;
        std::size_t up_n = 0;
        for (int i = 0; i < 40; ++i) up[up_n++] = 10.0 + 4.0 * lcg01();
        for (int i = 0; i < 30; ++i) up[up_n++] = 33.0 + 4.0 * lcg01();
        std::array<event, MAX_EVS> evs_up;
        auto evs_up_n = run_series<SHIFT_CFG>(up.data(), up_n, evs_up);
        check(count_events(evs_up, evs_up_n, event::shift_up) >= 1,
              "level rise fires shift_up");

        // 水準 10±4 → -27±4 (-37) を 30 点継続
        lcg_state = 12345u;
        std::array<double, MAX_SEQ> down;
        std::size_t down_n = 0;
        for (int i = 0; i < 40; ++i) down[down_n++] = 10.0 + 4.0 * lcg01();
        for (int i = 0; i < 30; ++i) down[down_n++] = -27.0 + 4.0 * lcg01();
        std::array<event, MAX_EVS> evs_down;
        auto evs_down_n = run_series<SHIFT_CFG>(down.data(), down_n, evs_down);
        check(count_events(evs_down, evs_down_n, event::shift_down) >= 1,
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
