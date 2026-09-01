#include <cstdio>
#include <keikoupp/keikoupp.hpp>

int main()
{
    using keikoupp::TimeMode;
    static constexpr auto bw_cfg = keikoupp::Config{0.1, 0.01, 0.05, 0.01, 0.05, 120, 2};
    auto cb = [](keikoupp::event e, const double ema, const double v) {
        std::printf("t+ event=%d ema=%.0f v=%.0f\n", static_cast<int>(e), ema, v);
    };
    keikoupp::analyzer<TimeMode::realtime, bw_cfg, decltype(cb)> bw{cb};
    double t = 1'700'000'000.0;
    for (int i = 0; i < 120; ++i, t += 60.0) {   // 60秒ごとの測定
        bw.push(t, 950e6 - 1e6 * i);             // 1Gbps → 約120Mbps 漸減
    }
    std::printf("ema=%.0f slope=%.0f/s\n", bw.ema(), bw.slope());
    std::printf("forecast(now+60)=%.0f\n", bw.forecast(t + 60.0));
}
