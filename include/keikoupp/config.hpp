#pragma once
#include <cstddef>

namespace keikoupp {

/**
 * @brief 時間の扱い方。サンプルが等間隔か実時間を持つか。
 */
// NOLINTNEXTLINE(readability-identifier-naming) — design doc が定めた公開 API 名 (TimeMode::fixed/realtime)
enum class TimeMode { fixed, realtime };

/**
 * @brief analyzer のコンパイル時固定パラメータ。非型テンプレート用作業。
 */
struct Config {
    double alpha;            ///< EMA 平滑化係数 (0 < alpha <= 1)
    double spike_k;          ///< スパイク検知 CUSUM の感度 k (ノイズ SD 倍)
    double spike_h;          ///< スパイク検知 CUSUM の閾値 h
    double shift_k;          ///< 水準シフト検知 CUSUM の感度 k
    double shift_h;          ///< 水準シフト検知 CUSUM の閾値 h
    std::size_t window;      ///< 回帰窓幅 (点数)
    std::size_t spike_confirm; ///< spike 発火までに要求する連続超過点数
};

}