#pragma once

namespace keikoupp {

enum class event {
    none,         ///< 変化なし (既定値)
    spike,        ///< スパイク (一時的急変)
    shift_up,     ///< 水準シフト (上向き)
    shift_down,   ///< 水準シフト (下向き)
    trend_up,     ///< トレンド上昇
    trend_down,   ///< トレンド下降
};

}