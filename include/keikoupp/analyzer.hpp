#pragma once

// WASI minimal対応: 例外・動的確保を避けるため <algorithm> / <array> / <cmath> /
// <functional> は使わない (std::function は動的確保を伴う)。wasip1 でも hosted ヘッダは
// 利用可能だが、本ライブラリは最小構成でビルドできるよう自前実装を維持する。
#include <cstddef>
#include <cstring>
#include <limits>
#include <utility>

#include "config.hpp"
#include "event.hpp"

namespace keikoupp {

namespace detail {

/// @brief double の絶対値 (<cmath> を避け WASI minimal でもビルドできるよう自前実装)。
constexpr double fabs_d(double x) noexcept { return x < 0.0 ? -x : x; }

/// @brief double の max (<algorithm> を避け WASI minimal でもビルドできるよう自前実装)。
constexpr double max_d(double a, double b) noexcept { return a < b ? b : a; }

/// @brief NaN 判定 (std::isnan 代替。IEEE 754 では NaN だけが x != x)。
constexpr bool isnan_d(double x) noexcept { return x != x; }

/// @brief std::array の最小代替 (WASI minimal でも動的確保なしで動作するよう自前実装)。
template <typename T, std::size_t N>
struct array {
    T buf_[N]{};
    constexpr T& operator[](std::size_t i) noexcept { return buf_[i]; }
    constexpr T const& operator[](std::size_t i) const noexcept { return buf_[i]; }
};

/// @brief c[0..n-1] から k 番目に小さい値を選ぶ (quickselect, Hoare 分割)。
/// <algorithm> の nth_element を避け WASI minimal でもビルドできるよう自前実装。平均 O(W)。
/// 残差の定常時 (すべて同値) でも Hoare 分割により 1 回の分割で打ち切りとなる。
inline double select_kth(double* c, std::size_t n, std::size_t k) noexcept {
    std::ptrdiff_t lo = 0;
    std::ptrdiff_t hi = static_cast<std::ptrdiff_t>(n) - 1;
    while (lo < hi) {
        const double p = c[(lo + hi) / 2];
        std::ptrdiff_t i = lo, j = hi;
        while (i <= j) {
            while (c[i] < p) ++i;
            while (c[j] > p) --j;
            if (i <= j) {
                const double t = c[i];
                c[i++] = c[j];
                c[j--] = t;
            }
        }
        if (k <= static_cast<std::size_t>(j)) {
            hi = j;
        } else if (k >= static_cast<std::size_t>(i)) {
            lo = i;
        } else {
            break;  // j < k < i: 区間 [j+1, i-1] はすべて p と同値 → c[k] == p
        }
    }
    return c[k];
}

}  // namespace detail

/// @brief コールバック未指定時に使う、何もしないイベントハンドラ。
struct noop_event_callback {
    void operator()(event, double, double) const noexcept {}
};

/// @brief 固定長リングバッファ。満杯時の push_back は最古要素を上書きする。
// ponytail: 分岐で剰余を代替 (N は定数のため %N は magic mul に展開されるが、分岐の方が 1.7-10% 速い)
template <typename T, std::size_t N>
struct ring_buffer {
    detail::array<T, N> buf_{};
    std::size_t head_ = 0;
    std::size_t size_ = 0;

    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }

    T& front() noexcept { return buf_[head_]; }
    T& at(std::size_t i) noexcept {
        std::size_t idx = head_ + i;
        if (idx >= N) idx -= N;
        return buf_[idx];
    }
    T const& at(std::size_t i) const noexcept {
        std::size_t idx = head_ + i;
        if (idx >= N) idx -= N;
        return buf_[idx];
    }

    void push_back(T const& v) noexcept {
        if (size_ == N) {
            buf_[head_] = v;
            if (++head_ >= N) head_ -= N;
        } else {
            std::size_t idx = head_ + size_;
            if (idx >= N) idx -= N;
            buf_[idx] = v;
            ++size_;
        }
    }
    void pop_front() noexcept {
        --size_;
        if (++head_ >= N) head_ -= N;
    }
};

/// @brief スライディング窓の中央値。ソート済み配列+二分探索+memmove で O(W) だが
/// W<=120 典型で RB-tree (multiset) の確保/ポインタ追跡より 18% 速く、キャッシュ効率も高い。
// ponytail: 旧 sliding_median (multiset 2分割) は確保コストが高く、W=1000 でも 5% 遅い
template <std::size_t N>
struct sorted_median {
    detail::array<double, N> buf_{};
    std::size_t sz_ = 0;

    std::size_t size() const noexcept { return sz_; }

    void insert(double v) {
        std::size_t lo = 0, hi = sz_;
        while (lo < hi) {
            const std::size_t mid = (lo + hi) >> 1;
            if (buf_[mid] < v) lo = mid + 1;
            else hi = mid;
        }
        if (sz_ > lo) std::memmove(&buf_[lo + 1], &buf_[lo], (sz_ - lo) * sizeof(double));
        buf_[lo] = v;
        ++sz_;
    }

    void erase(double v) {
        std::size_t lo = 0, hi = sz_;
        while (lo < hi) {
            const std::size_t mid = (lo + hi) >> 1;
            if (buf_[mid] < v) lo = mid + 1;
            else hi = mid;
        }
        for (std::size_t i = lo; i < sz_; ++i) {
            if (buf_[i] == v) {
                if (i + 1 < sz_) std::memmove(&buf_[i], &buf_[i + 1], (sz_ - i - 1) * sizeof(double));
                --sz_;
                return;
            }
            if (buf_[i] > v) break;
        }
    }

    double median() const noexcept {
        if (sz_ == 0) return std::numeric_limits<double>::quiet_NaN();
        return buf_[sz_ / 2];
    }
};

/**
 * @brief 変化点検出の解析器本体。
 *
 * EMA による平滑、MAD 正規化した spike / shift 用 CUSUM 検知、窓回帰傾きからの
 * トレンド分類、イベントコールバック通知を一つにまとめた構造体。
 * 時系列の投入は TimeMode::fixed なら push(double v) (連番を x 軸とする)、
 * TimeMode::realtime なら push(double t, double v) (実時刻を x 軸とする)。
 *
 * @tparam M 時系列の時間モード (TimeMode)。
 * @tparam C コンパイル時固定パラメータ (Config)。
 * @tparam Callback イベントハンドラの型 (シグネチャ void(event, double, double))。
 *          WASI minimal対応のため型消去 (std::function・動的確保) はせず、
 *          テンプレート引数で型を固定する。既定は何もしない noop_event_callback。
 */
template <TimeMode M, Config C, typename Callback = noop_event_callback>
struct analyzer {
    /// @brief トレンド分類。
    enum class trend { rising, flat, falling, unknown };

private:
    using trend_t = trend;  ///< trend() と同名のため内部利用専用の別名

public:

    /**
     * @brief コールバックを渡して構築 (推奨)。
     * @param f 呼び出し先。引数は (イベント種別, その時点の ema, 直近の値)。
     */
    explicit analyzer(Callback f) : cb_(std::move(f)) {}

    /**
     * @brief 既定構築。ema は未初期化 (NaN)、CUSUM は 0 から開始。
     * @note Callback が既定構築できない場合 (キャプチャ付きラムダ等) は使用不可。
     */
    analyzer() = default;

    /**
     * @brief 固定サンプルモード用: サンプル番号を x 軸として値を 1 点投入する。
     * @param v 観測値。
     * @note TimeMode::realtime では宣言されない (誤用はコンパイル時拒否)。
     */
    void push(double v) requires (M == TimeMode::fixed) {
        push_impl(static_cast<double>(n_++), v);
    }

    /**
     * @brief 実時間モード用: 観測時刻と値を 1 点投入する。
     * @param t 観測時刻。
     * @param v 観測値。
     * @note TimeMode::fixed では宣言されない (誤用はコンパイル時拒否)。
     */
    void push(double t, double v) requires (M == TimeMode::realtime) {
        ++n_;
        push_impl(t, v);
    }

    /**
     * @brief 現在の EMA 値。1 点も投入していない場合は NaN。
     */
    double ema() const noexcept { return ema_; }

    /**
     * @brief スパイク CUSUM が閾値を超えたままになっているか (検出進行中)。
     */
    bool spike() const noexcept { return s_spike_ > C.spike_h; }

    /**
     * @brief 現在の傾向分類。
     * @details 窓が満ちる前、または傾きが未算出 (NaN) のうちは unknown。
     *          |slope| と MAD の粗い比較で rising / flat / falling を分岐する。
     */
    trend trend() const noexcept {
        if (pts_.size() < C.window) return trend_t::unknown;
        const double s = slope();
        if (detail::isnan_d(s)) return trend_t::unknown;
        if (s > mad_) return trend_t::rising;
        if (s < -mad_) return trend_t::falling;
        return trend_t::flat;
    }

    /**
     * @brief 回帰直線の傾き (窓未満・退化時は NaN)。
     * @details O(1) 差分更新の部分総和から
     *          slope = (n*Sxy - Sx*Sy) / (n*Sxx - Sx*Sx) を求める。
     */
    double slope() const noexcept {
        if (pts_.size() < C.window) return nan_;
        const double denom = static_cast<double>(pts_.size()) * sxx_ - sx_ * sx_;
        if (denom == 0.0) return nan_;
        return (static_cast<double>(pts_.size()) * sxy_ - sx_ * sy_) / denom;
    }

    /**
     * @brief 固定サンプルモード用: 次サンプルの予測値 = ema + slope * 1。
     * @note TimeMode::realtime では宣言されない (誤用はコンパイル時拒否)。
     */
    double next() const noexcept requires (M == TimeMode::fixed) {
        return ema_ + slope();
    }

    /**
     * @brief 実時間モード用: 時刻 t における予測値 = ema + slope * (t - t_last)。
     * @note TimeMode::fixed では宣言されない (誤用はコンパイル時拒否)。
     */
    double forecast(double t) const noexcept requires (M == TimeMode::realtime) {
        return ema_ + slope() * (t - last_t_);
    }

    /**
     * @brief イベント検知コールバックを差し替える。
     * @param f 呼び出し先。引数は (イベント種別, その時点の ema, 直近の値)。
     * @note 型はテンプレート引数 Callback と同一である必要がある
     *          (WASI minimal対応のため型消去を行わない)。
     */
    void on_event(Callback f) {
        cb_ = std::move(f);
    }

private:
    static constexpr double nan_ = std::numeric_limits<double>::quiet_NaN();

    double ema_ = nan_;                 ///< 指数移動平均 (第 1 点でシード)
    double last_v_ = nan_;              ///< 直近の値
    double last_t_ = 0.0;               ///< 直近の x 座標 (realtime は時刻)
    std::size_t n_ = 0;                 ///< 投入回数 (fixed の x 座標用)

    double s_spike_ = 0.0;              ///< spike 検知用 CUSUM
    std::size_t spike_above_ = 0;       ///< spike 閾値の連続超過回数
    double su_ = 0.0;                   ///< shift 検知用 CUSUM (上方向)
    double sd_ = 0.0;                   ///< shift 検知用 CUSUM (下方向)

    ring_buffer<std::pair<double, double>, C.window> pts_;  ///< 回帰窓 (x, v)
    ring_buffer<double, C.window> res_;                  ///< 残差 (v - ema) の窓
    sorted_median<C.window> res_med_;                    ///< 残差のスライディングメディアン
    double sx_ = 0.0, sy_ = 0.0, sxx_ = 0.0, sxy_ = 0.0;  ///< 回帰部分和
    double mad_ = 0.0;                               ///< MAD ノイズ尺度 (下限済み)

    trend_t last_trend_ = trend_t::unknown;  ///< 直前の傾向分類 (変化検知用)
    Callback cb_{};  ///< イベントコールバック (テンプレート引数で型固定)

    /**
     * @brief push 共通処理。x は固定モードでは連番、実時間モードでは時刻。
     * @details EMA 更新 → 残差・MAD 更新 → spike / shift CUSUM 検知 → 回帰窓
     *          更新 → 傾向変化検知、の順で処理し、検知したイベントは即座に
     *          コールバックへ通知する。
     */
    void push_impl(double x, double v) {
        const double prev = ema_;
        if (detail::isnan_d(prev)) {
            ema_ = v;  // 第 1 点でシード
        } else {
            ema_ = C.alpha * v + (1.0 - C.alpha) * prev;
        }
        last_v_ = v;
        last_t_ = x;

        // 残差 (v - ema) の更新と MAD ノイズ尺度の更新
        const double d = detail::isnan_d(prev) ? 0.0 : v - prev;
        add_residual(d);
        mad_ = detail::max_d(1e-12, residual_mad());  // 0 除算回避の下限

        const double ds = d / mad_;  // MAD 正規化

        // spike 検知: S = max(0, S + (|d| - k)) | S > h の連続超過で発火し S を 0 に
        s_spike_ = detail::max_d(0.0, s_spike_ + detail::fabs_d(ds) - C.spike_k);
        if (s_spike_ > C.spike_h) {
            if (++spike_above_ >= C.spike_confirm) {
                fire(event::spike);
                s_spike_ = 0.0;
                spike_above_ = 0;
            }
        } else {
            spike_above_ = 0;
        }

        // shift 検知: 上下方向を別々の CUSUM で持ち、超過で発火 & リセット
        su_ = detail::max_d(0.0, su_ + ds - C.shift_k);
        if (su_ > C.shift_h) {
            fire(event::shift_up);
            su_ = 0.0;
        }
        sd_ = detail::max_d(0.0, sd_ - ds - C.shift_k);
        if (sd_ > C.shift_h) {
            fire(event::shift_down);
            sd_ = 0.0;
        }

        // 回帰窓の O(1) 差分更新 (窓幅超過で最古点を押し出す)
        if (pts_.size() == C.window) {
            const auto [ox, ov] = pts_.front();
            pts_.pop_front();
            sx_ -= ox;
            sy_ -= ov;
            sxx_ -= ox * ox;
            sxy_ -= ox * ov;
        }
        pts_.push_back({x, v});
        sx_ += x;
        sy_ += v;
        sxx_ += x * x;
        sxy_ += x * v;

        // 傾向分類が rising / falling に変わったらトレンドイベント
        const trend_t t = trend();
        if (t != last_trend_) {
            last_trend_ = t;
            if (t == trend_t::rising) {
                fire(event::trend_up);
            } else if (t == trend_t::falling) {
                fire(event::trend_down);
            }
        }
    }

    /**
     * @brief 残差窓から MAD = 1.4826 * median(|r - median(r)|) を求める。
     * @details 内側 median はスライディングメディアンで O(1)、外側 median は
     *          quickselect 1 回 (平均 O(W)。Hoare 分割のため定常時の同値偏りにも強い)。
     *          動的確保なし。旧実装 (<algorithm> nth_element) と同じ
     *          upper median (c[n/2]) を返す。
     */
    // ponytail: <algorithm> nth_element を避け WASI minimal 対応 → detail::select_kth
    double residual_mad() const {
        const std::size_t n = res_.size();
        if (n == 0) return 0.0;
        const double med = res_med_.median();
        detail::array<double, C.window> c{};
        for (std::size_t i = 0; i < n; ++i) c[i] = detail::fabs_d(res_.at(i) - med);
        return 1.4826 * detail::select_kth(c.buf_, n, n / 2);
    }

    /**
     * @brief 残差 1 点を窓とメディアン構造の両方へ追加する。
     */
    void add_residual(double d) {
        if (res_.size() == C.window) {
            res_med_.erase(res_.front());
            res_.pop_front();
        }
        res_.push_back(d);
        res_med_.insert(d);
    }

    /**
     * @brief (イベント, ema, 直近値) をコールバックへ通知 (既定は noop)。
     */
    void fire(event e) {
        cb_(e, ema_, last_v_);
    }
};

}  // namespace keikoupp