// push() 1点あたりのコストを計測するベンチマーク。
// window の異なる確率で定常+ノイズ系列を投入し、ns/op を出力する。
// イベントコールバックで volatile な集計をし、最適化による消滅を防ぐ。
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <keikoupp/keikoupp.hpp>

namespace {

constexpr std::size_t kDEFAULT_N = 1'000'000;

// テストと同じ LCG (シード変更で結果が変わらないよう固定)
static uint32_t lcg_state = 12345u;
static double lcg01() {
    lcg_state = lcg_state * 1103515245u + 12345u;
    return static_cast<double>(lcg_state >> 8) / 16777216.0;
}

// 定常ベースに ±2 交互ノイズ + 時々スパイクを混ぜた系列 (MAD が実スケールになる)
// 系列を格納せずに i 番目の値をその場で生成する
static double series_value(const std::size_t i) {
    double base = (i % 512 < 16) ? 50.0 : 10.0;  // 周期的な急変
    return base + 2.0 * (i % 3 ? 1.0 : -1.0) + (lcg01() - 0.5);
}

template <std::size_t W>
static double bench_window(const std::size_t n, const char* const name) {
    static constexpr auto C = keikoupp::Config{0.2, 2.0, 8.0, 2.0, 6.0, W, 3};
    volatile std::size_t nevt = 0;  // イベント集計 (最適化防止)
    volatile double nema = 0.0;
    auto cb = [&](keikoupp::event, double e, double v) {
        nevt += 1;
        nema += e + v;
    };
    keikoupp::analyzer<keikoupp::TimeMode::fixed, C, decltype(cb)> a{cb};

    lcg_state = 12345u;  // 再現性のためリセット
    const auto t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < n; ++i) a.push(series_value(i));
    const auto t1 = std::chrono::steady_clock::now();

    const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    const double per = ns / static_cast<double>(n);
    std::printf("%-12s %6zu %12.1f ns/op (%10.2f Mop/s)  events=%lu\n",
                name, W, per, 1e3 / per, static_cast<unsigned long>(nevt));
    return per;
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t n = argc > 1 ? static_cast<std::size_t>(std::atoll(argv[1])) : kDEFAULT_N;

    std::printf("n = %zu samples / %s\n", n, "push() 1点あたりコスト");
    bench_window<20>(n, "window=20");
    bench_window<120>(n, "window=120");
    bench_window<1000>(n, "window=1000");
    return 0;
}