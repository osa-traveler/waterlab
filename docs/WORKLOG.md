# WorkLog

新しいものを上に書く。書き方は [WORKFLOW.md](WORKFLOW.md) を参照。

## 2026-10-28

### MIT ライセンスとドキュメントの整備（ユーザーの依頼）
- `LICENSE`（MIT、著作権者は GitHub のアカウント名 osa-traveler）を追加した。仕様書の公開もユーザーの了承済み。
- 先行プロジェクト [Raytracer](https://github.com/osa-traveler/Raytracer) と同じ形で、`docs/` に次を追加した:
  - [RENDERING_GUIDE.md](RENDERING_GUIDE.md): 技術解説（高さ場、格子、ラプラシアン、Verlet、CFL、固定 tick、境界、ゼロ平均カーネル、エネルギー、色範囲、用語集）。
  - [WORKLOG.md](WORKLOG.md): このファイル。
  - [WORKFLOW.md](WORKFLOW.md): 進め方、ドキュメントの役割、公開時の確認事項。
  - [THIRD_PARTY.md](THIRD_PARTY.md): raylib 5.5 の入手方法・ライセンス（zlib）・SHA-256。
  - `screenshots/`: Phase 1 の画面（`--smoke-capture` の出力）。
- `Docs/` を `docs/` に改名した（仕様書の構成と Raytracer に合わせる。GitHub は大文字小文字を区別する）。[decisions.md](decisions.md) D01 に訂正として記録した。
- 解説に載せる数値は、使い捨ての測定プログラム（リポジトリ外）で 2026-10-28 に測った:
  - 既定クリック後の最大の高さ: 0.05 s で 3.1 mm、0.4 s で 0.56 mm（中心から 0.84 m）、0.8 s で 0.36 mm（1.72 m）。波の山の位置は c × t（0.88 m、1.76 m）とほぼ一致。
  - 固定端モード 30 s のエネルギー変動幅: S = 2 で 2.66×10⁻⁵、S = 4 で 7.07×10⁻⁶（比 3.8。2 次精度なら 4）。
  - ゼロ平均カーネル: Σ A·K ÷ Σ A·|K| = −3.4×10⁻⁹。単純ガウスでは Σ A·Δv = 5.6×10⁻³ m³/s。

### GitHub への公開（ユーザーの依頼）
- リモート: https://github.com/osa-traveler/waterlab （公開）、ブランチ `main`。
- **個人情報の対策**: グローバルの git 設定のメールが個人のアドレスだったので、このリポジトリのローカル設定だけ GitHub の noreply アドレスにしてからコミットした。全コミットの作者・コミッターが noreply であること、追跡ファイルに個人のメール・`/Users/` のパス・一時フォルダのパスがないことを確認した。
- `.DS_Store` と `water.code-workspace`（VS Code の個人用ワークスペース）は `.gitignore` で除外した（手元のファイルは残している）。
- **他者の権利**: raylib はリポジトリに含めず、ビルド時に取得する。フォント・画像・他人のコードは含めていない。
- コミット: `9669cee`（Phase 0〜1 の本体）。

### 「画面が真っ白」の調査と修正（ユーザーの報告）
- 症状: `./build/waterlab` を起動すると画面が真っ白。
- 調査: このセッションには画面録画の権限がないため、アプリ自身にフレームバッファを PNG で書き出させるオプション `--smoke-capture FILE.png` を追加した（中心に波紋を 1 回入れ、90 フレーム後に保存して終了）。
  - 1 回目の画像は背景しか写っていなかった。raylib は描画命令を `EndDrawing` でまとめて GPU に送るため、その前に読み出していたのが原因（私の取得処理の誤り。描画の不具合ではない）。読み出しの前に `rlDrawRenderBatchActive()` を呼ぶよう直した。
  - 2 回目の画像で、HUD と、うっすらとした同心円の波紋が描かれていることを確認した。
- 原因: Height 表示の色範囲が仕様書どおり ±0.05 m だったのに対し、既定のクリックの波は max|h| = 0.0004 m 程度（範囲の 1% 未満）で、ほぼ白にしか見えなかった。
- 修正: 色範囲を `,` `.` キーで ±0.0005〜0.05 m に切り替え可能にし、既定を ±0.001 m にした。現在の範囲は画面に常に表示する。仕様書からの変更として [decisions.md](decisions.md) D09 に記録した。
- 確認: `--smoke-capture` の画像で、赤と青の同心円がはっきり見えること（下図）。ctest 26/26 合格。
- 未確認: マウス操作（クリック位置、Shift の負の波紋、境界切替、停止中の入力）はユーザー側での確認待ち。

![Phase 1 の Height 表示（中心のクリックから 0.8 s 後、色範囲 ±0.001 m）](screenshots/2026-10-28_phase1-height-view-center-ripple.png)

### Phase 0〜1 の実装（ユーザーの依頼「着手してください」）
- 計画: 仕様書を読み、Phase 0〜5 の実装計画を立てた。最初の到達点を「ビルドでき、テストが通り、クリック波紋が見える」とした。
- 環境: Apple M1 iMac、macOS 26.6.2、Apple clang 21.0.0、CMake 3.28.3。
- **Phase 0**: CMake（arm64、C++17 必須・C++20 可、`WATERLAB_BUILD_APP`、`WATERLAB_ENABLE_SANITIZERS`）、raylib 5.5 を SHA-256 固定で取得、core の型（`Vec2`/`Vec3`、`IWaveSolver` の契約）、格子と台形則面積、設定の検査、依存なしのテストハーネス。
- **Phase 1**: velocity Verlet（仕様書 §4.3 の 11 手順の順序どおり）、CFL とサブステップ、3 種類の境界（Fixed・Reflective・Absorbing。Phase 3 の予定を一部前倒し）、ゼロ平均インパルス、固定 120 Hz の World（入力キュー、tick 境界の設定変更、描画補間、追いつき上限、異常停止）、2D の Height 表示、操作キー、診断表示、ヘッドレスのベンチマーク。
- テスト: 26 項目（T01〜T06、T08、T15、エネルギー保存・減衰、反射の符号、同心円の対称性、ラプラシアンのキャッシュなど）。Release・Debug+ASan/UBSan・C++20 のすべてで合格。
  - 途中の失敗: 反射の符号のテストが最初は失敗した。z 方向 3 点の細長い領域では、Fixed の側壁が波を押しつぶしていたため（テストの設計の誤り）。z 方向を 1 m に広げ、初期形状を z 方向の固定端モードとの積にして、両方の境界で同じ条件にした。
- 性能（Release、256²、S = 2、3000 tick の予備計測）: 物理 1 tick あたり p50 0.252 ms、p95 0.283 ms、max 0.621 ms。仕様書の目標（p95 ≦ 6 ms）は満たすが、正式な計測（20 s のウォームアップ後 120 s 以上）は Phase 5 で行う。
- 判断の記録: [decisions.md](decisions.md) D01〜D08。検証結果: [validation.md](validation.md)。

### 到達点と次の一手
- 到達点: Phase 1 完了（クリックでの波紋の操作確認だけユーザー待ち）。GitHub に公開済み。
- 次の一手: Phase 2（3D 水面）。128×128 セルのタイルメッシュ、勾配法線、カメラ、Water シェーダー（Fresnel・環境反射・解析的な水底色）、4 つの表示モード。
