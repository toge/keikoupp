// freestanding モード検証。Catch2 を使わず -ffreestanding -fno-exceptions -fno-rtti
// -nostdlib++ (libstdc++ リンクなし) でビルド・実行できることを確認する。
// ライブラリが <algorithm> / <array> / <cmath> / <functional> や動的確保に
// 逆戻りしたらコンパイルエラーまたはリンクエラー (operator new 等) で失敗する。
#ifndef KEIKOUPP_FREESTANDING
#error "KEIKOUPP_FREESTANDING is not defined (build with -DENABLE_FREESTANDING=ON)"
#endif

#include <cstdio>

#include <keikoupp/keikoupp.hpp>

static int failed = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failed;                                                   \
        }                                                               \
    } while (0)

static int spike_count = 0;
static int shift_count = 0;

static void on_ev(keikoupp::event e, double, double) {
    if (e == keikoupp::event::spike) ++spike_count;
    if (e == keikoupp::event::shift_up) ++shift_count;
}

int main() {
    using keikoupp::TimeMode;
    static constexpr auto cfg = keikoupp::Config{0.3, 0.5, 3.0, 0.3, 2.0, 20, 1};

    // 1. 既定 (noop_event_callback) でも push が動く
    keikoupp::analyzer<TimeMode::fixed, cfg> plain;
    for (int i = 0; i < 30; ++i) plain.push(22.0);
    CHECK(plain.ema() == plain.ema());  // NaN でない (= 1 点以上投入済み)

    // 2. 関数ポインタコールバック + spike / shift_up 検知
    keikoupp::analyzer<TimeMode::fixed, cfg, void (*)(keikoupp::event, double, double)> a{on_ev};
    for (int i = 0; i < 30; ++i) a.push(22.0);
    a.push(45.0);  // スパイク (spike_confirm=1 で即発火)
    CHECK(spike_count >= 1);
    for (int i = 0; i < 25; ++i) a.push(30.0);  // 水準シフト (22 → 30)
    CHECK(shift_count >= 1);
    CHECK(a.ema() == a.ema());

    // 3. captureless ラムダコールバック + トレンド検知
    static int trend_events = 0;
    auto cb = [](keikoupp::event e, double, double) {
        if (e == keikoupp::event::trend_up) ++trend_events;
    };
    keikoupp::analyzer<TimeMode::fixed, cfg, decltype(cb)> b{cb};
    for (int i = 0; i < 40; ++i) b.push(22.0 + 0.2 * i);  // 上昇トレンド
    CHECK(trend_events >= 1);

    if (failed == 0) std::printf("freestanding_check: all ok\n");
    return failed == 0 ? 0 : 1;
}
