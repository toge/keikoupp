#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <set>
#include <utility>

#include "config.hpp"
#include "event.hpp"

namespace keikoupp {

/// @brief 固定長リングバッファ。満杯時の push_back は最古要素を上書きする。
template <typename T, std::size_t N>
struct ring_buffer {
    std::array<T, N> buf_{};
    std::size_t head_ = 0;
    std::size_t size_ = 0;

    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }

    T& front() noexcept { return buf_[head_]; }
    T& at(std::size_t i) noexcept { return buf_[(head_ + i) % N]; }
    T const& at(std::size_t i) const noexcept { return buf_[(head_ + i) % N]; }

    void push_back(T const& v) noexcept {
        if (size_ == N) {
            buf_[head_] = v;             // 最古要素を上書きして頭を進める
            head_ = (head_ + 1) % N;
        } else {
            buf_[(head_ + size_) % N] = v;
            ++size_;
        }
    }
    void pop_front() noexcept {
        --size_;
        head_ = (head_ + 1) % N;
    }
};

/// @brief スライディング窓の中央値 (上側メディアン、c[n/2] 相当)。
/// 昇順に整列した窓の下半分 lower_ と上半分 upper_ を 2 つの multiset で管理し、
/// balanced を保つため挿入・削除は O(log n) で済む。
struct sliding_median {
    std::multiset<double> lower_;  ///< 窓の下半分
    std::multiset<double> upper_;  ///< 窓の上半分 (median は *upper_.begin())

    std::size_t size() const noexcept { return lower_.size() + upper_.size(); }

    void insert(double v) {
        if (lower_.empty() || v <= *lower_.rbegin()) {
            lower_.insert(v);
        } else {
            upper_.insert(v);
        }
        rebalance();
    }

    void erase(double v) {
        auto it = lower_.find(v);
        if (it != lower_.end()) {
            lower_.erase(it);
        } else {
            auto uit = upper_.find(v);
            if (uit != upper_.end()) upper_.erase(uit);
        }
        rebalance();
    }

    /// @brief 中央値 (upper_ の最小値)。空なら NaN。
    double median() const noexcept {
        if (upper_.empty()) return lower_.empty() ? std::numeric_limits<double>::quiet_NaN()
                                                  : *lower_.rbegin();
        return *upper_.begin();
    }

private:
    void rebalance() {
        const std::size_t n = size();
        const std::size_t tgt = n / 2;  // lower_ は n/2 個
        while (lower_.size() > tgt) {
            auto it = --lower_.end();
            upper_.insert(*it);
            lower_.erase(it);
        }
        while (lower_.size() < tgt) {
            auto it = upper_.begin();
            lower_.insert(*it);
            upper_.erase(it);
        }
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
 */
template <TimeMode M, Config C>
struct analyzer {
    /// @brief トレンド分類。
    enum class trend { rising, flat, falling, unknown };

private:
    using trend_t = trend;  ///< trend() と同名のため内部利用専用の別名

public:

    /**
     * @brief 既定構築。ema は未初期化 (NaN)、CUSUM は 0 から開始。
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
        if (std::isnan(s)) return trend_t::unknown;
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
     * @brief イベント検知コールバックを登録する。
     * @param f 呼び出し先。引数は (イベント種別, その時点の ema, 直近の値)。
     */
    template <typename F>
    void on_event(F&& f) {
        cb_ = std::forward<F>(f);
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
sliding_median res_med_;                             ///< 残差のスライディングメディアン
    double sx_ = 0.0, sy_ = 0.0, sxx_ = 0.0, sxy_ = 0.0;  ///< 回帰部分和
    double mad_ = 0.0;                               ///< MAD ノイズ尺度 (下限済み)

    trend_t last_trend_ = trend_t::unknown;  ///< 直前の傾向分類 (変化検知用)
    std::function<void(event, double, double)> cb_;  ///< イベントコールバック

    /**
     * @brief push 共通処理。x は固定モードでは連番、実時間モードでは時刻。
     * @details EMA 更新 → 残差・MAD 更新 → spike / shift CUSUM 検知 → 回帰窓
     *          更新 → 傾向変化検知、の順で処理し、検知したイベントは即座に
     *          コールバックへ通知する。
     */
    void push_impl(double x, double v) {
        const double prev = ema_;
        if (std::isnan(prev)) {
            ema_ = v;  // 第 1 点でシード
        } else {
            ema_ = C.alpha * v + (1.0 - C.alpha) * prev;
        }
        last_v_ = v;
        last_t_ = x;

        // 残差 (v - ema) の更新と MAD ノイズ尺度の更新
        const double d = std::isnan(prev) ? 0.0 : v - prev;
        add_residual(d);
        mad_ = std::max(1e-12, residual_mad());  // 0 除算回避の下限

        const double ds = d / mad_;  // MAD 正規化

        // spike 検知: S = max(0, S + (|d| - k)) | S > h の連続超過で発火し S を 0 に
        s_spike_ = std::max(0.0, s_spike_ + std::abs(ds) - C.spike_k);
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
        su_ = std::max(0.0, su_ + ds - C.shift_k);
        if (su_ > C.shift_h) {
            fire(event::shift_up);
            su_ = 0.0;
        }
        sd_ = std::max(0.0, sd_ - ds - C.shift_k);
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
     *          nth_element 1 回 (O(W))。動的確保なし。
     */
    double residual_mad() const {
        const std::size_t n = res_.size();
        if (n == 0) return 0.0;
        const double med = res_med_.median();
        std::array<double, C.window> c{};
        for (std::size_t i = 0; i < n; ++i) c[i] = std::abs(res_.at(i) - med);
        std::nth_element(c.begin(), c.begin() + n / 2, c.begin() + n);
        return 1.4826 * c[n / 2];
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
     * @brief コールバックが登録されていれば (イベント, ema, 直近値) を通知。
     */
    void fire(event e) {
        if (cb_) cb_(e, ema_, last_v_);
    }
};

}  // namespace keikoupp