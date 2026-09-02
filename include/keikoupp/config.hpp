#pragma once
/**
 * @file keikoupp/config.hpp
 * @brief ビルドモード設定と共通設定型。
 *
 * KEIKOUPP_WASI_MINIMAL が定義されると、ライブラリ内の全ての例外送出
 * (KEIKOUPP_THROW) が std::abort() に置き換わり、-fno-exceptions でも
 * ビルドできる「例外なしモード」になる。wasm32-wasip1 / wasm32-emscripten は
 * WASI/hosted とみなすため自動では有効にならず、WASI 上で
 * 最小構成を検証する場合は手動で `-DKEIKOUPP_WASI_MINIMAL` を指定する。
 * 本ライブラリの WASI 対応は wasi-sdk sysroot を用いた wasm32-wasip1 でのビルドを
 * 想定（wasm3 等で実行可能）。
 *
 * 例: clang++ --target=wasm32-wasip1 --sysroot=/opt/wasi-sdk/share/wasi-sysroot
 *       -fno-exceptions -DKEIKOUPP_WASI_MINIMAL=1 -I include -c src.cpp -o src.o
 */
#if !defined(KEIKOUPP_WASI_MINIMAL) && defined(__wasm__) && !defined(__wasi__) && !defined(__EMSCRIPTEN__)
#  define KEIKOUPP_WASI_MINIMAL 1
#endif

/**
 * @brief 例外送出の統一マクロ。
 *
 * hosted (既定) では `throw expr` に展開する。KEIKOUPP_WASI_MINIMAL 定義時は
 * expr を評価せず `detail::fail()` を呼ぶ。fail() は非 constexpr のため
 * コンパイル時評価では従来どおりコンパイルエラーになり、実行時は std::abort() する。
 * これにより -fno-exceptions でもライブラリ全体がビルドできる。
 * 現状 keikoupp 本体は例外を送出しないが、将来の拡張と frozenchars との統一のため用意する。
 */
#ifndef KEIKOUPP_WASI_MINIMAL
#  include <stdexcept>
#  define KEIKOUPP_THROW(expr) throw expr
#else
#  include <cstdlib>
namespace keikoupp::detail {
[[noreturn]] inline void fail() noexcept { std::abort(); }
} // namespace keikoupp::detail
#  define KEIKOUPP_THROW(expr) ::keikoupp::detail::fail()
#endif

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
