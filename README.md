# keikoupp

時系列の変化点検出（スパイク・水準シフト・トレンド）を行う C++23 ヘッダオンリーライブラリ。

## 概要

- EMA 平滑化と、MAD 正規化した残差を使った CUSUM 検知により、スパイクと水準シフトを検出する。
- 回帰窓の傾きと MAD の比較によるトレンド分類（上昇 / 横ばい / 下降）を行う。
- `analyzer<TimeMode, Config>` の 2 引数テンプレートで、パラメータはすべてコンパイル時に固定される。
- ライブラリ本体は依存ゼロ。Catch2 はテストとサンプルでのみ使用（vcpkg）。

## 使い方

```cpp
#include <keikoupp/keikoupp.hpp>   // 傘ヘッダ。config.hpp / event.hpp / analyzer.hpp を取り込む
```

`keikoupp::analyzer<M, C>` を、時間モード `M` と `keikoupp::Config{...}` の組み合わせで宣言する。

- `TimeMode::fixed`: サンプル番号を x 軸とする等間隔データ向け。`push(double v)` と `next()` を使用。
- `TimeMode::realtime`: 実時刻を x 軸とするデータ向け。`push(double t, double v)` と `forecast(t)` を使用。

`push` / `next` / `forecast` は requires 節により宣言がモードごとに排他され、モードに合わない関数を呼ぶとコンパイルエラーになる（実行時の失敗ではなく静的に拒否される）。

### 例 1: 温度センサー（fixed）

```cpp
#include <cstdio>
#include <keikoupp/keikoupp.hpp>

int main() {
    using keikoupp::TimeMode;
    static constexpr auto sensor_cfg = keikoupp::Config{0.3, 0.5, 3.0, 0.3, 2.0, 60, 20};
    keikoupp::analyzer<TimeMode::fixed, sensor_cfg> temp;
    temp.on_event([](keikoupp::event e, const double ema, const double v) {
        std::printf("event=%d ema=%.2f v=%.2f\n",
                    static_cast<int>(e), ema, v);
    });
    for (int i = 0; i < 100; ++i)
        temp.push(22.0);            // 安定状態
    temp.push(45.0);                // 異常値（1点）
    temp.push(22.0);                // 元に戻る → ノイズとして不検出
    for (int i = 0; i < 30; ++i)
        temp.push(22.0 + 0.1 * i);  // 緩やかな上昇トレンド
    std::printf("ema=%.2f slope=%.3f trend=%d\n",
                temp.ema(), temp.slope(), static_cast<int>(temp.trend()));
}
```

### 例 2: 帯域の定期測定（realtime）

```cpp
#include <cstdio>
#include <keikoupp/keikoupp.hpp>

int main() {
    using keikoupp::TimeMode;
    static constexpr auto bw_cfg = keikoupp::Config{0.1, 0.01, 0.05, 0.01, 0.05, 120, 2};
    keikoupp::analyzer<TimeMode::realtime, bw_cfg> bw;
    bw.on_event([](keikoupp::event e, const double ema, const double v) {
        std::printf("t+ event=%d ema=%.0f v=%.0f\n",
                    static_cast<int>(e), ema, v);
    });
    double t = 1'700'000'000.0;
    for (int i = 0; i < 120; ++i, t += 60.0)  // 60秒ごとの測定
        bw.push(t, 950e6 - 1e6 * i);          // 1Gbps → 約120Mbps へ漸減
    std::printf("ema=%.0f slope=%.0f/s\n", bw.ema(), bw.slope());
    std::printf("forecast(now+60)=%.0f\n", bw.forecast(t + 60.0));
}
```

### セルフチェック

`example/demo.cpp` は仕様の全ステージ（EMA・スパイク・シフト・トレンド・補間）を `check()` で検証する自己完結サンプル。
`bash test.sh` の `example_demo` テストとして実行され、失敗時は非ゼロで終了する。

### イベントコールバック

`on_event(F&& f)` で登録した関数に、検知のたびに `(event e, double ema, double v)` が渡される。
イベントは `push` の内部で同期発火する。

`keikoupp::event` は次の 6 値をとる。

| 値 | 意味 |
|---|---|
| `none` | 変化なし（既定値。コールバックでは通知されない） |
| `spike` | スパイク（一時的な急変） |
| `shift_up` | 水準シフト（上向き） |
| `shift_down` | 水準シフト（下向き） |
| `trend_up` | トレンド上昇 |
| `trend_down` | トレンド下降 |

## 値の定義

`keikoupp::Config` は 7 フィールドで、デフォルト値はなく利用者がすべて指定する。
以下は各フィールドの意味と、代表的な目安値である（MAD はノイズの標準偏差に相当する尺度）。

| フィールド | 型 | 意味 | 目安 |
|---|---|---|---|
| `alpha` | `double` | EMA 平滑化係数（`0 < alpha <= 1`）。大きいほど応答が速い | 0.01–0.3 |
| `spike_k` | `double` | スパイク用 CUSUM の感度 k（MAD の倍数） | 0.5 |
| `spike_h` | `double` | スパイク用 CUSUM の閾値 h | 3.0 |
| `shift_k` | `double` | 水準シフト用 CUSUM の感度 k（MAD の倍数） | 0.3 |
| `shift_h` | `double` | 水準シフト用 CUSUM の閾値 h | 2.0 |
| `window` | `std::size_t` | 残差 MAD と回帰に使う窓幅（点数） | 60–120 |
| `spike_confirm` | `std::size_t` | スパイク発火に必要な閾値超過の連続点数 | 1–3 |

### 内部用の固定値（v1 では Config から変更不可）

| 定数 | 値 | 意味 |
|---|---|---|
| MAD 係数 | `1.4826` | `MAD = 1.4826 × median(|r − median(r)|)`。ノイズ標準偏差のロバスト推定 |
| MAD 下限 | `1e-12` | 0 除算を避けるための MAD の下限（analyzer.hpp で適用） |
| トレンド横ばい帯 | `1.0 × MAD` | `trend()` の分岐で使う固定幅（Config にはない） |

### NaN / unknown の意味

| 値 | 意味 |
|---|---|
| `ema()` が NaN | 1 点も投入されていない |
| `slope()` が NaN | 窓が満ちていない、または x が全部同じで傾きが算出できない |
| `trend()` が `unknown` | 窓が満ちていない、または slope が NaN |

`trend` は `analyzer` の入れ子 enum で、`rising` / `flat` / `falling` / `unknown` の 4 値である。

## アルゴリズム

1. **EMA 更新**: 1 点目は `ema = v` でシードし、以降 `ema = alpha × v + (1 − alpha) × ema` で更新する。
2. **残差と MAD**: 残差 `d = v − prev_ema`（初回は 0）を窓幅 `window` のリングバッファで保持し、
   `MAD = 1.4826 × median(|d − median(d)|)` を計算する。下限 `1e-12` を適用し、`ds = d / MAD` と正規化する。
3. **スパイク検知**: `S = max(0, S + |ds| − spike_k)` を更新し、`S > spike_h` が `spike_confirm` 点連続したら `event::spike` を発火して `S` を 0 にリセットする。単発の外れ値は拾わない。
4. **水準シフト検知**: 上向き・下向きに独立した CUSUM を持つ。
   `su = max(0, su + ds − shift_k)` が `shift_h` を超えたら `shift_up`、`sd = max(0, sd − ds − shift_k)` が `shift_h` を超えたら `shift_down` を発火し、該当 CUSUM を 0 にリセットする。
5. **回帰**: 窓幅 `window` 点の (x, v) を部分和（O(1) 差分更新）で管理し、
   `slope = (n×Sxy − Sx×Sy) / (n×Sxx − Sx²)` で傾きを求める。窓未満や退化時は NaN。
6. **トレンド判定**: 窓未満・傾き NaN は `unknown`。`slope > MAD` なら `rising`、`slope < −MAD` なら `falling`、いずれでもなければ `flat`。
   分類が **`rising` / `falling` に変化する遷移時のみ** `trend_up` / `trend_down` を発火する（`flat` への復帰は通知しない）。

1 点の `push` 内で検知と通知が完結し、履歴は各 CUSUM と窓にのみ保持される。

## ビルド

C++23 が必要（requires 節を使用）。

```bash
bash build.sh   # cmake 構成 + ビルド（vcpkg toolchain を使用）
bash test.sh    # ctest --test-dir build --output-on-failure
```

ライブラリ本体はヘッダオンリーで依存ゼロ。Catch2（vcpkg）はテストとサンプルのみに必要。

## ライセンス

未定。LICENSE ファイルはまだ整備されていない（公開時に方針を決めて追加する）。