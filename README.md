# WaterLab

4m四方の静かな水面に入力を与え、波紋の伝播・反射・減衰を観察する小さな実験環境です。
CPU上の2D波動方程式（高さ場）で計算します。仕様は [docs/spec/realtime_water_ripple_spec_v1.0_ja.md](docs/spec/realtime_water_ripple_spec_v1.0_ja.md) を参照してください。

![Phase 1 の Height 表示](docs/screenshots/2026-10-28_phase1-height-view-center-ripple.png)

> **現在の実装状況：Phase 1（2D波紋）まで。** 3D水面描画（Phase 2）、球体（Phase 4）は未実装です。

## 最終的な目標

小川のように、岩や草があっても水面の形は保たれたまま水が流れていく様子を、格子に「流れの向き」を持たせて再現することです（[docs/decisions.md](docs/decisions.md) D10）。まず v1.0（流れのない波紋）を完成させ、その後に流れ版へ進みます。

## ドキュメント

| ファイル | 内容 |
|---|---|
| [docs/RENDERING_GUIDE.md](docs/RENDERING_GUIDE.md) | 技術解説（なぜそうするのか。式の意味と実測の数値） |
| [docs/WORKLOG.md](docs/WORKLOG.md) | 日々の作業記録 |
| [docs/validation.md](docs/validation.md) | Phase ごとの検証結果 |
| [docs/decisions.md](docs/decisions.md) | 仕様書にない点・変えた点の判断と理由 |
| [docs/WORKFLOW.md](docs/WORKFLOW.md) | 進め方のルール |
| [docs/THIRD_PARTY.md](docs/THIRD_PARTY.md) | 依存ライブラリ（raylib）とライセンス |

## 動作環境

- macOS／Apple Silicon（arm64）。M1 iMac で確認
- CMake 3.24以上、AppleClang（C++17。C++20でもビルド可能）
- raylib 5.5（CMake FetchContentで自動取得。版とSHA256は `cmake/Dependencies.cmake` で固定）

## ビルドと実行

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/waterlab
```

初回はraylibのソース取得にネットワークが必要です。オフラインの場合は、展開済みのraylib 5.5ソースを指定します。

```sh
cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_RAYLIB=/path/to/raylib-5.5
```

GUIなし（raylibを取得しない）でテストとベンチマークだけをビルドする場合：

```sh
cmake -S . -B build-core -DWATERLAB_BUILD_APP=OFF
cmake --build build-core -j && ./build-core/waterlab_tests
```

ASan/UBSan（Debugのみ有効）：

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DWATERLAB_ENABLE_SANITIZERS=ON -DWATERLAB_BUILD_APP=OFF
```

## 起動オプション

```sh
./build/waterlab --grid 256 --wave-speed 2.2 --damping 0.35 --boundary reflective
```

| オプション | 既定値 | 範囲 |
|---|---:|---|
| `--grid` | 256 | 128／256／512（境界を含む標本点数） |
| `--wave-speed` | 2.2 | 0.1～5.0 m/s |
| `--damping` | 0.35 | 0～3 s⁻¹ |
| `--boundary` | reflective | fixed／reflective／absorbing |

## 操作（Phase 1時点）

| 入力 | 動作 |
|---|---|
| 左クリック | 正の波紋（ピーク速度変化 +強度） |
| Shift＋左クリック | 負の波紋 |
| Space | 停止／再開 |
| N | 停止中に1 tick進める |
| R | 全体リセット |
| Tab | 境界を Fixed → Reflective → Absorbing の順に切替 |
| `[` / `]` | 波速 c を ±0.1 m/s |
| `;` / `'` | 減衰 γ を ±0.05 s⁻¹ |
| `-` / `=` | クリック強度の絶対値を ±0.05 m/s（0.05～1.0） |
| `,` / `.` | Height表示の色範囲を狭く／広く（±0.0005～0.05 m、既定±0.001 m） |
| 1 | Height表示（2～4はPhase 2で実装予定） |
| H | ヘルプ表示切替 |

画面右の正方形が水面の上面図です。右方向が+X、下方向が+Z。高さ h を青（負）～白（0）～赤（正）で表し、範囲外は飽和します。色範囲は画面左に表示されます。

## テストとベンチマーク

```sh
./build/waterlab_tests            # 全テスト
./build/waterlab_tests T05        # 名前の部分一致で絞り込み
./build/waterlab_bench --grid 256 --ticks 14400 --warmup 1200 --csv bench.csv
```

ベンチマークは物理計算のみを計測し、描画FPSは報告しません。`--spheres` は Phase 4 まで未実装のため、0以外を指定するとエラーになります。

## モデルと既知の近似・制限

- 鉛直変位だけを持つ高さ場の線形波動方程式 ∂²h/∂t² = c²∇²h − γ∂h/∂t です。水平流速、渦、液滴、砕波、越流は扱いません。
- 波速 c は実験用の定数で、深水波の分散関係から求めたものではありません。
- 物理は固定120Hzです。CFL条件（C≦0.90）を満たすよう、tick内を S 回のサブステップに分けます（既定 S=2）。描画FPSによって物理の結果は変わりません。
- `--smoke-capture FILE.png` を付けると、中心に波紋を1回入れ、90フレーム後の画面をPNGに保存して終了します（描画の動作確認用）。
- 1フレームで受け付ける経過時間は0.1秒まで、進めるtickは8回までです。それを超えた時間は捨て、画面に記録します（実時間より遅れます）。
- 吸収帯（Absorbing）は反射を弱めるだけで、完全に無反射にはなりません。
- クリック入力は既定でゼロ平均の補償カーネル（中心と周辺で符号が逆）です。平均水位はほぼ変わりません。
- max|h|>0.5m、max|v|>10m/s、NaN/Inf のいずれかで物理を停止し、理由を表示します。R でリセットすると再開できます。値をクランプして安定しているように見せることはしません。
- Metal Compute版は未実装です（仕様書第13節の将来拡張）。

## ディレクトリ

```text
src/core        Vec2/Vec3、単位、例外（raylib非依存）
src/simulation  格子、境界、インパルス、CPUソルバー、固定tickのWorld（raylib非依存）
src/rendering   raylib資源のRAII、Height表示
src/app         エントリポイント、操作・数値表示
tests           依存なしのテストハーネスと数値テスト
tools           ヘッドレスベンチマーク
docs            仕様書、技術解説、作業記録、設計判断、検証記録
```

## ライセンス

[MIT](LICENSE)。依存ライブラリのライセンスは [docs/THIRD_PARTY.md](docs/THIRD_PARTY.md) を参照してください。
