# サードパーティのコード

このリポジトリには、他人のソースコード・フォント・画像は含めていない。ビルド時に取得する次のものに依存している。

| 名前 | 用途 | ライセンス | 入手方法 |
|---|---|---|---|
| [raylib](https://github.com/raysan5/raylib) 5.5 | ウィンドウ、OpenGL 3.3 描画、入力、既定フォント | zlib | CMake の設定時に、GitHub のタグアーカイブ `https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz` を自動取得し、`build/_deps/` に展開する。SHA-256 を `cmake/Dependencies.cmake` で固定して検証する（下記）。リポジトリには含めない |

raylib は、内部で GLFW（zlib/libpng）などを同梱している。いずれも raylib のアーカイブに含まれ、このリポジトリには含めない。

コア（`src/core`、`src/simulation`）とテスト・ベンチマークは、標準ライブラリだけに依存する（`-DWATERLAB_BUILD_APP=OFF` で raylib を取得せずにビルドできる）。

## raylib の取得と検証

- 固定している SHA-256: `aea98ecf5bc5c5e0b789a76de0083a21a70457050ea4cc2aec7566935f5e258e`（`raylib-5.5.tar.gz`、42,278,112 バイト）。
- タグ `5.5` が指す commit: `c1ab645ca298a2801097931d1079b10ff7eb9df8`（2026-10-28 に `git ls-remote` で確認）。
- この SHA-256 は、**初回にダウンロードしたアーカイブから計算したもの**（2026-10-28）。公式が公開しているハッシュとの照合まではしていない。以後は、同じ内容でなければ CMake が失敗する（改ざんや差し替えの検出）。
- オフラインの場合は、`-DFETCHCONTENT_SOURCE_DIR_RAYLIB=/path/to/raylib-5.5` で展開済みのソースを指定する。
- バージョンを上げるときは、`cmake/Dependencies.cmake` の URL・ハッシュ・commit を一緒に更新し、`./build/waterlab --smoke-capture out.png` で描画を確認する。メッシュのバッファ番号（位置0、法線2）は版によって変わり得るので、ソースで確認する（仕様書 §7.1）。

## ライセンス表記

zlib ライセンス（raylib）は、ソースを再配布する場合に表示を改変・削除しないことを求める。バイナリ配布での表記は任意（推奨）。現在は、リポジトリにも配布物にも raylib を含めていないので、同梱は不要。実行ファイルを配布するようになったら、ここを更新する。

このリポジトリ自体のライセンスは [MIT](../LICENSE)。
