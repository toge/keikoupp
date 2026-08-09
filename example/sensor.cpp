#include <cstdio>
#include <keikoupp/keikoupp.hpp>

int main() {
    using keikoupp::TimeMode;
    static constexpr auto sensor_cfg = keikoupp::Config{0.3, 0.5, 3.0, 0.3, 2.0, 60, 20};
    keikoupp::analyzer<TimeMode::fixed, sensor_cfg> temp;
    temp.on_event([](keikoupp::event e, double, double) {
        std::printf("event: %d\n", static_cast<int>(e));
    });
    for (int i = 0; i < 100; ++i)
        temp.push(22.0);            // 安定状態
    temp.push(45.0);                 // 異常値 (1点)
    temp.push(22.0);                 // 元に戻る → ノイズとして不検出
    for (int i = 0; i < 30; ++i)
        temp.push(22.0 + 0.1 * i);   // 緩やかな上昇トレンド
    std::printf("ema=%.2f slope=%.3f trend=%d\n",
                temp.ema(), temp.slope(), static_cast<int>(temp.trend()));
}
