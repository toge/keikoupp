#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include <keikoupp/analyzer.hpp>

using keikoupp::TimeMode;

// センサー(温度)の想定: 計画書の SENSOR_CFG。定常 → 急激な 12℃ ジャンプで spike。
constexpr auto SENSOR_CFG = keikoupp::Config{0.3, 0.5, 3.0, 0.3, 2.0, 60, 20};
// センサーの緩慢ドリフト (ゆっくり上昇) は spike でなく shift_up と解釈する。
// 計画書の spike_k=0.5 だと MAD 正規化の 1.0 単位のドリフトでも spike CUSUM が
// 積み上がってしまうため、spike_k を 1.0・spike_h を 3.0 に引き上げて
// 「単発・急速な変化だけが spike」という意味論を成立させた。
constexpr auto DRIFT_CFG = keikoupp::Config{0.3, 1.0, 3.0, 0.3, 2.0, 60, 20};
// 帯域 (bandwidth) の想定: 計画書の BW_CFG。1Gbps からの漸減で shift_down。
constexpr auto BW_CFG = keikoupp::Config{0.1, 0.01, 0.05, 0.01, 0.05, 120, 2};

TEST_CASE("sensor: stable temp then rapid jump fires spike") {
    keikoupp::analyzer<TimeMode::fixed, SENSOR_CFG> a;
    std::vector<keikoupp::event> events;
    a.on_event([&](keikoupp::event e, double, double) { events.push_back(e); });
    for (int i = 0; i < 100; ++i) a.push(22.0);
    for (int i = 0; i < 30; ++i) a.push(34.0);  // 急上昇 (12℃)
    REQUIRE(std::find(events.begin(), events.end(), keikoupp::event::spike) != events.end());
}

TEST_CASE("sensor: slow drift is shift_up, not spike") {
    // 100→500 を 200 サンプルで緩やかに上昇 (傾き +2, 振幅 ±5 の交互ノイズ):
    // 1 点ずつの変化は MAD 内で spike に届かず、累積する水準変化が shift_up として出る。
    keikoupp::analyzer<TimeMode::fixed, DRIFT_CFG> a;
    std::vector<keikoupp::event> events;
    a.on_event([&](keikoupp::event e, double, double) { events.push_back(e); });
    for (int i = 0; i < 100; ++i) a.push(100.0 + (i % 2 ? 5.0 : -5.0));
    for (int i = 0; i < 200; ++i) a.push(100.0 + 2.0 * i + (i % 2 ? 5.0 : -5.0));
    REQUIRE(std::find(events.begin(), events.end(), keikoupp::event::spike) == events.end());
    REQUIRE(std::find(events.begin(), events.end(), keikoupp::event::shift_up) != events.end());
}

TEST_CASE("bandwidth: gradual degradation fires shift_down and falling trend") {
    keikoupp::analyzer<TimeMode::realtime, BW_CFG> a;
    std::vector<keikoupp::event> events;
    a.on_event([&](keikoupp::event e, double, double) { events.push_back(e); });
    for (int i = 0; i < 120; ++i) {
        const double v = 1.0e9 - 1.0e6 * i;  // 1Gbps から毎回 1Mbps ずつ漸減
        a.push(static_cast<double>(i), v);
    }
    REQUIRE(std::find(events.begin(), events.end(), keikoupp::event::shift_down) != events.end());
    REQUIRE(a.trend() == keikoupp::analyzer<TimeMode::realtime, BW_CFG>::trend::falling);
}