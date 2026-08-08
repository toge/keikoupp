#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <type_traits>

#include <keikoupp/config.hpp>
#include <keikoupp/event.hpp>

using namespace keikoupp;

// ponytail: analyzer<...> は Task 3 で定義されるため、ここでは型のみ検証する。
template <TimeMode M>
struct mode_tag {};

template <Config C>
struct config_tag {};

TEST_CASE("Config has expected default-construction and fields") {
    constexpr Config c{0.1, 1.0, 4.0, 0.5, 3.0, 20, 3};
    STATIC_REQUIRE(std::is_aggregate_v<Config>);
    STATIC_REQUIRE(c.alpha == 0.1);
    STATIC_REQUIRE(c.spike_k == 1.0);
    STATIC_REQUIRE(c.spike_h == 4.0);
    STATIC_REQUIRE(c.shift_k == 0.5);
    STATIC_REQUIRE(c.shift_h == 3.0);
    STATIC_REQUIRE(c.window == 20);
    STATIC_REQUIRE(c.spike_confirm == 3);
    STATIC_REQUIRE(Config{}.alpha == 0.0);
    STATIC_REQUIRE(Config{}.spike_confirm == 0);

    STATIC_REQUIRE(std::is_same_v<decltype(c.alpha), double>);
    STATIC_REQUIRE(std::is_same_v<decltype(c.spike_k), double>);
    STATIC_REQUIRE(std::is_same_v<decltype(c.spike_h), double>);
    STATIC_REQUIRE(std::is_same_v<decltype(c.shift_k), double>);
    STATIC_REQUIRE(std::is_same_v<decltype(c.shift_h), double>);
    STATIC_REQUIRE(std::is_same_v<decltype(c.window), std::size_t>);
    STATIC_REQUIRE(std::is_same_v<decltype(c.spike_confirm), std::size_t>);
    STATIC_REQUIRE(sizeof(Config) == 5 * sizeof(double) + 2 * sizeof(std::size_t));
}

TEST_CASE("time_mode and event are scoped enums usable in NTTP") {
    STATIC_REQUIRE(std::is_enum_v<TimeMode>);
    STATIC_REQUIRE(std::is_enum_v<event>);
    STATIC_REQUIRE(!std::is_convertible_v<int, TimeMode>);
    STATIC_REQUIRE(!std::is_convertible_v<int, event>);
    STATIC_REQUIRE(TimeMode::fixed != TimeMode::realtime);
    STATIC_REQUIRE(event::none != event::spike);
    STATIC_REQUIRE(event::spike != event::shift_up);
    STATIC_REQUIRE(event::shift_up != event::shift_down);
    STATIC_REQUIRE(event::shift_down != event::trend_up);
    STATIC_REQUIRE(event::trend_up != event::trend_down);

    STATIC_REQUIRE(std::is_same_v<mode_tag<TimeMode::fixed>, mode_tag<TimeMode::fixed>>);
    STATIC_REQUIRE(std::is_same_v<mode_tag<TimeMode::realtime>,
                                  decltype(mode_tag<TimeMode::realtime>{})>);

    // analyzer の代替: Config を NTTP として扱えることを検証する。
    STATIC_REQUIRE(std::is_same_v<config_tag<Config{0.1, 1, 4, 0.5, 3, 20, 3}>,
                                  config_tag<Config{0.1, 1, 4, 0.5, 3, 20, 3}>>);
}