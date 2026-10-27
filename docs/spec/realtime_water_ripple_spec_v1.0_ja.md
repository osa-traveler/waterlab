# リアルタイム水面・波紋シミュレーション仕様書 v1.0

- プロジェクト名：WaterLab
- 作成日：2026-10-28（日本時間）
- 対象：M1 iMac／macOS／Apple Silicon ネイティブ
- 実装言語：C++17を必須基準とし、C++20でもビルド可能
- 初期方式：CPU上の2D波動方程式＋3D高さ場描画
- 本書の用途：Claude Codeに渡し、設計・実装・検証・README作成まで進めるための実装仕様

## 0. 背景・決定事項

静かな4m四方の水面に入力や球体が作用すると波紋が生まれ、伝播・反射・減衰する小さな実験環境を作る。添付の「リアルタイム水面シミュレーション 実装計画 v1.0」を引き継ぎ、実装が未定だった計算法・単位・API・受け入れ基準を本書で確定する。

初期実装では流体全体を解かず、鉛直変位だけを持つ高さ場を用いる。球体は水面から浮力・抵抗を受け、水面には球体の動的な作用を近似的に戻す。水平流速、渦、液滴、砕波、越流、厳密な排水量・エネルギー・運動量保存は対象外。静止球の周囲に正しいメニスカスや水位上昇を再現するものでもない。

本書の「必須」はv1.0の完成条件。「将来」はv1.0の実装対象外であり、拡張点だけを準備する。前計画のPhase 5は将来計画として分離し、Metal Computeの未実装を理由にCPU版の完成を遅らせない。

### 0.1 技術選択

| 項目 | 採用内容 |
|---|---|
| ビルド | CMake 3.24以上、AppleClang、arm64 |
| 言語 | C++17、拡張構文無効、C++20任意検証 |
| ライブラリ | raylib 5.5を再現可能な基準版として固定。最新版という意味ではない |
| 描画 | raylibのデスクトップOpenGL 3.3、GLSL 330 |
| 数学型 | coreに小さな独自Vec2/Vec3、描画境界でraylib型へ変換。GLM不要 |
| シミュレーション | 単一CPUスレッド、float32、SoA、明示的時間積分 |
| 時間積分 | 高さh＋鉛直速度vのvelocity Verlet、速度の指数減衰を前後に分割 |
| 依存導入 | CMake FetchContentで版固定。オフライン時は指定済みソースパスを利用 |
| 設定 | CLI＋組み込み既定値。初期版にJSONライブラリは追加しない |
| テスト | raylib非依存のテスト実行ファイル＋CTest。小さな独自assertハーネスでよい |
| GPU化 | 将来のObjective-C++／Metal Computeバックエンド。CPU版を比較基準に残す |

OpenGLは初期実装の選択であり、Metalとの共有メモリやゼロコピーを保証しない。

## 1. 機能要件

| ID | 必須機能 | 完成時の振る舞い |
|---|---|---|
| F01 | 波動計算 | 256×256標本の高さ・速度を固定120Hzで進め、CFLに応じて内部時間刻みを分割 |
| F02 | クリック波紋 | 水面をクリックすると、その位置へ1回だけ速度インパルスを加える |
| F03 | 境界切替 | 固定端、反射壁、吸収帯を実行中に切り替えられる |
| F04 | 3D描画 | 高さで変形するメッシュ、勾配法線、光源、Fresnel、簡易環境反射 |
| F05 | 診断表示 | 高さ色、ワイヤーフレーム、法線RGB、Waterの4モード |
| F06 | 球体 | 最大8球。生成・自由落下・浮力・抵抗・槽の壁／底との衝突 |
| F07 | 近似双方向結合 | 水面が球へ作用し、着水・抵抗・水平移動が水面へ波を返す |
| F08 | 球体操作 | 球をドラッグ可能。位置を瞬間移動させず、外力で目標位置へ追従 |
| F09 | 実験操作 | 一時停止、1 tick実行、リセット、波速・減衰・強度変更 |
| F10 | 計測 | FPS、tick/substep数、CFL値、計算時間、max|h|、異常カウンタを表示 |
| F11 | ヘッドレス実行 | ウィンドウなしでテスト・ベンチマーク・決定的入力列を実行 |

水底はチェッカーパターンとし、Waterモードで解析的な水底色を合成する。画面空間屈折、実際のシーン反射、コースティクスは将来機能。水中球の物理的に正しい屈折表示はv1.0の受け入れ条件に含めない。

## 2. 非機能要件

1. 描画FPSやVSyncの状態で物理の時間刻みが変わらない。
2. 同一バイナリ・同一設定・同一tick入力列で再現可能。異なるコンパイラ間のビット一致は要求しない。
3. 10分の通常操作でNaN/Inf、未解放GPU資源、無制限なメモリ増加がない。
4. シミュレーションcoreはraylibヘッダ、OpenGL、ウィンドウ生成に依存しない。
5. 通常tick内のヒープ確保を避ける。フィールド・メッシュ・入力キュー・球配列は予約する。
6. 120Hzは目標とし、M1実機で測定して判定する。実測していない性能を達成済みと記載しない。
7. 1280×720の実描画ピクセル、Release、256²、8球、既定2サブステップで60FPSを目標。Retinaの論理サイズと実描画サイズをログに残す。
8. 物理tick時間p95≦6ms、フレーム時間p95≦16.7msを目標とする。VSync待機を除いたCPU時間と実フレーム時間は別々に報告する。
9. アプリ管理のCPUメモリ目標128MiB以下。ドライバ・OSの共有GPUメモリは別記する。
10. 長時間停止から復帰しても大量の追いつき計算で操作不能にならない。

## 3. 座標・格子・データ仕様

### 3.1 単位と座標

SI単位を使用する。長さm、時間s、質量kg、力N、速度m/s、加速度m/s²。右手系、Y上向き、水平方向X/Z。静水面y0=0、水底y=-1m。

水面領域はx,z∈[-2,+2]m。256×256は「境界を含む標本点数」であり、256セルではない。セル数は255×255。

```text
Nx = Nz = 256
Lx = Lz = 4.0
dx = Lx/(Nx-1) = 4/255 ≈ 0.0156862745 m
dz = Lz/(Nz-1)
x(i) = -Lx/2 + i*dx
z(j) = -Lz/2 + j*dz
index(i,j) = j*Nx + i
```

### 3.2 保持配列

| 名前 | 型・長さ | 意味 |
|---|---|---|
| height | float[Nx*Nz] | h、静水面からの鉛直変位m |
| velocity | float[Nx*Nz] | v=dh/dt、m/s。整数時刻の速度 |
| acceleration | float[Nx*Nz] | ラプラシアンと外力を含む作業領域 |
| source | float[Nx*Nz] | 現サブステップの外部加速度m/s² |
| damping | float[Nx*Nz] | γ、s⁻¹。設定変更時に再構築 |
| renderPrevious/current | float配列各1本 | 物理tick前後の高さスナップショット |
| normals | Vec3[Nx*Nz] | 描画時に補間高さから生成。coreの永続状態には不要 |

作業領域を含め6～8本のfloat配列でも数MiB程度。SoAとし、内側ループはi方向。配列サイズ乗算のオーバーフロー、非有限パラメータ、範囲外アクセスを検査する。

境界点の積分面積は台形則とし、内部A=dx*dz、辺A/2、角A/4。インパルス正規化・平均・エネルギー計測でも同じ面積を使う。これは反射境界の離散演算子と整合させるためである。

### 3.3 既定パラメータ

| パラメータ | 既定値 | 初期UIの範囲・制約 |
|---|---:|---|
| grid | 256×256 | CLIで128/256/512。変更は再初期化 |
| domain | 4×4m | 正、初期UIでは固定 |
| waveSpeed c | 2.2m/s | 0.1～5.0m/s |
| tickDt | 1/120s | 固定 |
| cflSafety | 0.90 | 0より大、1未満 |
| maxSubsteps | 16 | 超過要求は設定を拒否 |
| damping γ0 | 0.35s⁻¹ | 0～3s⁻¹ |
| boundary | Reflective | 3モード |
| spongeWidth | 0.50m | 各軸半幅未満 |
| spongeGamma | 8.0s⁻¹ | 吸収帯の追加減衰 |
| clickStrength | ±0.25m/s | ピーク速度変化、符号反転可 |
| clickSigma | 0.06m | 2*max(dx,dz)以上 |
| gravity | 9.81m/s² | 下向き |
| waterDensity | 1000kg/m³ | 正 |
| couplingDepth | 0.30m | 波への力変換用の有効深さ。実際の水深とは別 |
| sphereRadius | 0.12m | 0.06～0.25m |
| sphereDensity | 500kg/m³ | 100～2000kg/m³ |
| sphereSpawnY | 0.60m | 球中心 |
| Cd / entryCoefficient | 0.8 / 0.25 | 非負 |
| reactionGain / wakeGain | 0.15 / 0.08 | 非負、無次元 |
| maxSourceAcceleration | 5m/s² | 結合入力だけのソフト制限 |
| maxBodySpeed | 5m/s | 制限発生を診断表示 |

異常停止の閾値はmax|h|>0.5m、max|v|>10m/sまたはNaN/Inf。通常解を毎回クランプして安定しているように見せない。閾値超過時は物理を停止し、原因・tick・設定を表示してリセットを待つ。

## 4. 数値計算法

### 4.1 方程式

```text
∂h/∂t = v
∂v/∂t = c²∇²h - γ(x,z)*v + a_external(x,z,t)
```

γは速度に対する減衰率。高さそのものへ毎フレーム定数を掛けない。波速は深水波の分散関係から求めず、実験用定数とする。

内部点の5点ラプラシアン：

```text
L(h)[i,j] = (h[i+1,j]-2h[i,j]+h[i-1,j])/dx²
           + (h[i,j+1]-2h[i,j]+h[i,j-1])/dz²
```

### 4.2 CFLと自動サブステップ

```text
dtLimit = cflSafety / (c * sqrt(1/dx² + 1/dz²))
S = max(1, ceil(tickDt/dtLimit))
dt = tickDt/S
C = c*dt*sqrt(1/dx² + 1/dz²)  // 必ず C<=0.90
```

等間隔ならc*dt/dx≦0.90/√2。既定値ではdtLimit≈0.00454s、S=2、dt=1/240s、C≈0.826。512²・4m・2.2m/sではS=4となる。

CFLが保証するのは線形波動演算部分の安定性であり、強すぎる球体結合まで無条件に安定にするものではない。結合係数の制限と別の検証を行う。

cの変更はtick境界で適用し、Sを再計算する。S>maxSubstepsになる設定は適用せず理由を表示する。dtを勝手に拡大して対応しない。

### 4.3 1サブステップの更新

速度は半時刻ずらした表現を採用せず、height/velocityとも同一時刻の値を保存する。以下の順序を固定する。

```text
1. 今回のsourceをゼロにする。
2. 球体が現時刻のh/vをサンプルし、力とsourceを構築する。
3. v *= exp(-γ*dt/2)
4. a0 = c²*L(h) + source
5. v += 0.5*dt*a0
6. h += dt*v
7. 境界拘束を適用する。
8. a1 = c²*L(h) + source  // 更新済みh。sourceはこのsubstep中は固定
9. v += 0.5*dt*a1
10. v *= exp(-γ*dt/2)
11. 境界拘束を再適用する。球体を更新し異常を検査する。
```

ラプラシアンは各評価時点の一貫した高さから計算する。hを走査しながらその場更新して隣接点の新旧値を混ぜない。上記波動部は2次精度のvelocity Verletだが、球体との明示的な分離結合まで2次精度とは主張しない。

### 4.4 固定tickループ

実時間accumulatorはdouble、物理場はfloatを用いる。

```text
frameDt = min(monotonicElapsed, 0.10)
accumulator += frameDt
最大8回まで：accumulator >= tickDtなら
    tick境界の設定変更と入力イベントを消費
    renderPrevious = renderCurrent
    そのtickのSを確定
    pendingImpulseを1回だけvelocityへ反映
    S回substep(dt)
    renderCurrent = height
    accumulator -= tickDt
上限を超えた未処理tick時間は捨て、捨てた秒数を記録
alpha = accumulator/tickDt
renderHeight = lerp(renderPrevious, renderCurrent, alpha)
```

時間を捨てると実時間より遅れるが、巨大dtによる破綻を避ける。既にキューに入れたイベントは捨てず次の物理tickで消費する。再現試験は実時間accumulatorを使わず、tick番号で入力を再生する。

停止中はaccumulatorを増やさない。単一ステップはtickを1回進める。再開時は時計を取り直す。リセット時には高さ・速度・球・入力キュー・accumulator・描画履歴を初期化する。

## 5. 波紋インパルスと境界条件

### 5.1 クリック入力

入力は物理tickへキューイングし、描画フレーム中に直接場を変更しない。

```text
Impulse { positionXZ[m], deltaVelocityPeak[m/s], sigma[m], tick, sequenceId }
g(r) = exp(-r²/(2*sigma²))  (r<=3*sigma)、それ以外0
v[i,j] += deltaVelocityPeak * K[i,j]
```

radiusという曖昧な名前を避け、APIでもsigmaを使う。ピーク速度変化なのでdtは掛けない。サブステップ数に応じて繰り返し注入しない。

既定は体積ドリフトを抑える補償カーネルとする。g1のσ、g2の2σをそれぞれ3σ・6σで打ち切り、領域内で：

```text
b = sum(A*g1)/sum(A*g2)
Kraw = g1-b*g2
K = Kraw/max(abs(Kraw))
```

これによりsum(A*K)≈0で、中心と周辺に逆符号の入力を与える。標本点が不足する場合はイベントを拒否し診断する。固定端の拘束点は正規化対象から除く。端で切れたカーネルも領域内で再正規化する。比較用に単純ガウスモードも用意し、その場合平均水位がドリフトし得ると表示する。毎tick全体の平均を勝手に引く処理は行わない。

### 5.2 境界

| モード | 実装 | 観測 |
|---|---|---|
| Fixed | 最外周h=v=0。内部のみ積分 | 変位が反転して戻る |
| Reflective | Neumann条件∂h/∂n=0、鏡像ゴースト値 | 変位が同符号で反射 |
| Absorbing | Reflectiveの外側条件＋外周スポンジ減衰帯 | 反射を弱める。完全吸収ではない |

反射の左辺ゴーストはh[-1,j]=h[1,j]、右辺h[Nx,j]=h[Nx-2,j]。z側も同様。角は両軸へ独立適用する。境界標本自体も更新し、境界点を内側の値で単純上書きする方式と混在させない。

吸収帯は最寄りの領域端までの距離dを使い：

```text
s = clamp((spongeWidth-d)/spongeWidth, 0, 1)
γ = γ0 + spongeGamma*s*s
```

角で減衰を二重加算しない。切替時は場を保持し、Fixedへ切り替える場合だけ境界h/vを0にする。格子再構築を伴う変更はリセットする。

## 6. 球体と近似相互作用

### 6.1 剛体の範囲

SphereBodyはid、位置p、速度u、半径R、密度ρs、質量m、ドラッグ状態を持つ。m=ρs*4πR³/3。球の自転・姿勢・球同士の衝突はv1.0では扱わず、球同士は重なり得ることをREADMEに記す。

水槽内に中心を制限し、側壁x,z=±(2-R)、底y=-1+Rと衝突させる。反発係数0.15、接線速度を衝突時に0.98倍。法線方向が壁へ向かう場合だけ反発し、常時速度を反転しない。

### 6.2 水面問い合わせ

現時刻の高さhと鉛直速度vを双線形補間する。物理には描画補間値を使わない。球中心XZでのη=y0+hを水平な局所水面とみなす。

球冠の高さと浸水体積：

```text
s = clamp(η-(p.y-R), 0, 2R)
Vsub = π*s²*(R-s/3)
q = Vsub/(4πR³/3)
Fb = (0, ρwater*g*Vsub, 0)
```

水面傾斜に沿った浮力やトルクは加えない。球中心サンプルは小振幅を前提にした近似。将来の複数点サンプルでも同じ体積を重複計上しない。

### 6.3 抵抗・着水力・積分

波動モデルが持つ水速度は鉛直成分のみ。局所水速度uw=(0,vSample,0)とする。

```text
urel = u-uw
Awet = πR²*q
Fdrag = -0.5*ρwater*Cd*Awet*|urel|*urel
Fentry.y = entryCoefficient*ρwater*πR²*max(-urel.y,0)²*4*q*(1-q)
Fentry.x = Fentry.z = 0
Ftotal = (0,-m*g,0) + Fb + Fdrag + Fentry + Fmouse
```

Fentryは着水時の抵抗を滑らかに強める演出的な項で、別の着水インパルスを同時に加えない。乾燥時q=0と完全浸水q=1では0。dragとentryの合力が1サブステップで相対速度を逆転させないよう、抵抗インパルスの大きさをm*|urel|以下に制限する。適用後の力を波への反作用にも使用する。

各波サブステップと同じdtで半陰的Euler：u+=Ftotal/m*dt、p+=u*dt、壁・底衝突。速度上限は例外入力への防御で、発生回数を記録する。上限に頻繁に達する挙動は合格にしない。

### 6.4 球から水面への鉛直反作用

反作用は動的な鉛直抵抗と着水力を対象とする。静的浮力Fbや重力をそのまま水面へ毎tick戻すと定常的な押し込みと平均水位ドリフトを起こすため、戻さない。

球中心にσ=max(R/2,2*max(dx,dz))のガウス重みwを作り、sum(w)=1となるよう有効な標本上で正規化する。wは面積積分した分配率とし、端の台形面積も重みに含める。

```text
FwaterY = -reactionGain*(Fdrag.y+Fentry.y)
aReaction[i] = FwaterY*w[i]/(ρwater*couplingDepth*A[i])
```

下向きに落ちる球には上向きの水の力が働き、水面には下向きの作用が戻る。平均水位モードへの加速を抑えるため、aReactionの面積平均を全有効標本から引き、ゼロ平均のsourceとして加える。Fixedの場合は拘束点を除く。

これは領域全体への補償成分を含む演出上の結合であり、局所的・厳密な運動量保存は成立しない。reactionGain=0では球体は水面から影響を受けるが、水面への鉛直反作用は無効となる。

### 6.5 水平移動の航跡

水平流れを解かないため、水平抵抗の反作用を鉛直へ直接変換せず、前後の双極性ソースを別に定義する。

```text
U = length(u.xz)
e = u.xz/U  // U<0.01m/sなら航跡なし
front = p.xz + 0.5*R*e
back  = p.xz - 0.5*R*e
σwake = max(R/2, 2*max(dx,dz))
aWake = wakeGain*q*(U²/R)*(G(front)-G(back))
```

Gはピーク1、σwakeのガウス、3σで打ち切り。領域で切れた場合も差分の面積平均を引いてゼロ平均とする。前方は上向き、後方は下向き。これは速度二乗に比例する加速度m/s²で、速度変化にはdtを掛ける。q=0なら入力なし。

全球の反作用と航跡を足したsourceは、最大絶対値が5m/s²を超えた場合、全体を同一係数で縮小する。要素別クランプでゼロ平均を壊さない。縮小回数を記録する。入力を1/フレームや1/サブステップの回数に依存させない。

### 6.6 ドラッグ

球へのレイ交差を水面クリックより優先する。ドラッグ開始時に水平面y=p.yを固定し、その平面とマウスレイの交点をXZ目標とする。Yは自由落下・浮力で動く。

```text
Fmouse.xz = m*(ω²*(targetXZ-p.xz)-2*ζ*ω*u.xz)
ω=8rad/s、ζ=1、加速度上限20m/s²
```

目標XZを槽の範囲内に制限し、解放後は速度を保持する。マウス目標はtick境界で取り込む。カメラ操作とドラッグ入力を競合させない。

## 7. レンダリング仕様

### 7.1 メッシュと所有権

各格子標本を(x,y0+h,z)へ変換。セルを2三角形に分割し、上向きに見える頂点順序を確認する。256²は65,536頂点で16bitインデックスの境界にあり、512²への拡張も考慮して単一巨大メッシュに依存しない。

描画は最大128×128セルのタイルに分割する。各タイルの頂点数は最大129²=16,641、インデックス値は16bitに収まる。端タイルは残りセル数で生成。タイル境界の標本は複製するが、座標・高さ・法線は必ず同じ全体フィールドから取得する。256²では2×2タイルとなる。

初期化時に位置・法線・UV・インデックスを確保して動的メッシュとしてアップロードする。描画フレームごとに位置と法線のバッファだけ更新する。raylib 5.5では位置のUpdateMeshBufferインデックス0、法線2を使用し、版変更時はソースを確認する。

MeshをDrawMeshで直接所有・描画する構成を基本とし、Modelへ浅いコピーを渡して二重解放しない。raylibへ所有権を渡す配列はraylibと互換のアロケータで確保する。標準vectorのdataをUnloadMeshが解放する構成は禁止。Mesh、Shader、Material、Textureの所有者をRAIIで一意に定め、共有シェーダーを複数回解放しない。

### 7.2 法線

描画補間済み高さに対し、内部では中央差分、端では片側差分を使う。

```text
hx=(h[i+1,j]-h[i-1,j])/(2*dx)
hz=(h[i,j+1]-h[i,j-1])/(2*dz)
n=normalize((-hx,1,-hz))
```

Reflectiveの端では境界条件に合わせ法線方向の高さ勾配を0とする。タイル単位で微分しない。フラグメントシェーダーでも補間後の法線を正規化する。水面モデル変換は恒等変換を基本とする。

### 7.3 Waterシェーダー

必須uniformはcameraPosition、lightDirection、baseWaterColor、F0、specularStrength、表示倍率。高さと物理法線は表示倍率に依存させず、見た目の高さ倍率を設定した場合は描画形状と描画法線を同じ倍率で更新する。

```text
V=normalize(cameraPosition-worldPosition)
cosθ=clamp(dot(N,V),0,1)
F=F0+(1-F0)*(1-cosθ)^5
F0=((1.333-1)/(1.333+1))² ≈ 0.0204
C=mix(CbottomTint, Cenvironment(reflect(-V,N)), F)+Cspecular
```

Cenvironmentは空色と水平線の手続き的グラデーションでよい。外部HDR素材は必須にしない。CbottomTintは水底平面上のXZチェッカー色に水色と距離減衰を加えた解析的な近似。実際に別カメラで水底を描画したテクスチャではないことをREADMEに明記する。

初期Waterパスは不透明・深度書込あり。球を先に通常描画し、水面を深度テスト付きで描く。水中部分は隠れる仕様とし、必要なら別の水面非表示デバッグキーで確認する。透明ブレンドのソートや水中球の屈折を中途半端に必須化しない。

### 7.4 デバッグモード

- Height：hを±0.05mで青～白～赤に写像。超過は飽和し表示範囲を明示。
- Wireframe：物理高さによる線メッシュ。槽と球を併記。
- Normal：0.5*(N+1)をRGBへ変換。
- Water：Fresnel＋環境反射＋簡易水底色＋スペキュラー。

初期カメラ位置(4,3,4)、注視点(0,0,0)、上向き(0,1,0)。水面上方からの観察を基本とし、カメラが水中に入らないよう制限する。

### 7.5 水面ピッキング

波紋入力はレイと基準平面y=0の交差を採用する。傾いた波面との厳密交差は不要。レイがほぼ平行、交点が背後、領域外なら入力を拒否する。画面座標はraylibのマウスとカメラAPIの同じ座標系を使い、Retinaで独自の二重スケーリングをしない。

## 8. アーキテクチャとAPI契約

依存方向はapp → simulation/physics/rendering、physics → simulationの問い合わせ型、rendering → 読み取りスナップショット。simulationからapp/renderingへの参照は禁止。

```cpp
// C++17。以下の型はcoreで定義しraylib型を使わない。
struct Vec2 { float x, z; };
struct Vec3 { float x, y, z; };
enum class BoundaryMode { Fixed, Reflective, Absorbing };

struct GridDesc {
    uint32_t nx, nz;
    float lengthX, lengthZ, baseHeight;
};
struct WaveSettings {
    float waveSpeed, damping, cflSafety;
    BoundaryMode boundary;
    float spongeWidth, spongeGamma;
    uint32_t maxSubsteps;
};
struct Impulse {
    Vec2 center;
    float deltaVelocityPeak, sigma;
    bool zeroMean;
};
struct SurfaceSample {
    float height;           // 変位m。baseHeightは含まない
    float verticalVelocity;
    Vec3 normal;
    bool inside;
};
struct CpuFieldView {
    const float* height;
    const float* velocity;
    uint32_t nx, nz;
    uint64_t revision;
};

class IWaveSolver {
public:
    virtual ~IWaveSolver() = default;
    virtual void reset() = 0;
    virtual GridDesc grid() const = 0;
    virtual void applySettings(const WaveSettings&) = 0;
    virtual void applyImpulse(const Impulse&) = 0;
    virtual void sampleBatch(const Vec2* xz, SurfaceSample* out,
                             size_t count) const = 0;
    virtual void advanceSubstep(float dt,
                               const float* acceleration,
                               size_t count) = 0;
    virtual CpuFieldView cpuView() const = 0;
};
```

### 8.1 契約詳細

- SimulationWorldが固定tick、S計算、球・波の結合順序を所有する。solver自身が120Hzタイマーを持たない。
- advanceSubstepは正で有限のdtを要求し、CFL違反ならエラー。sourceのcountはNx*Nz。sourceは呼出し中のみ借用し、保持しない。
- applyImpulseは即時にvへ1回反映する低レベルAPI。appは直接呼ばずWorldのtickイベント経由で使う。
- sampleBatchは領域外をinside=falseとし、height/velocity=0、normal=(0,1,0)を返す。暗黙に端へクランプしない。領域内の最右端は最後のセルの補間係数1として扱う。
- cpuViewは読み取り専用。次の変更操作・リセット・破棄まで有効。描画側は物理tick後に所有スナップショットへコピーする。
- revisionは状態変更時に増加。設定エラー時は変更前の状態を維持し、理由を返す例外またはResult型を統一して使う。
- resetは有効な格子・設定を保持してh/vを0にする。球・入力・時計のリセットはWorld側。
- エラーを無言で握りつぶさない。例外はapp境界で捕捉しログに残す。

### 8.2 補助モジュール

- WaveField：配列、格子、補間、境界面積。
- WaveSolverCPU：差分、Verlet、減衰、CFL検証。
- BoundaryConditions：境界アクセス・スポンジ係数。
- Disturbance：クリックカーネルと正規化。
- SimulationWorld：tickと入力消費、球更新、スナップショット。
- WaterInteraction：浸水量・力・水面source。
- WaterRenderer：メッシュ更新、描画、デバッグ表示。
- ExperimentUI：キー操作、数値表示、カメラ。

## 9. 操作・CLI・ログ

### 9.1 必須操作

| 入力 | 動作 |
|---|---|
| 左クリック | 球があれば選択、なければ正の波紋 |
| Shift＋左クリック | 負の波紋 |
| 左ドラッグ | 選択球のXZ目標を移動 |
| 右ドラッグ／ホイール | カメラ回転／ズーム |
| B | 最後の有効な水面カーソル位置へ球を生成。未指定なら中央 |
| Space | 停止／再開 |
| N | 停止中に1 tick |
| R | 全体リセット |
| 1～4 | Height／Wireframe／Normal／Water |
| Tab | 境界を順送り |
| [ / ] | cを0.1m/s変更 |
| ; / ' | γを0.05s⁻¹変更 |
| - / = | クリック強度の絶対値を変更 |
| H | ヘルプ表示切替 |

キー調整は押下イベントを使い、OSの文字入力レイアウトではなくraylibのキー定数を基準にする。値・単位・現在の境界を常時表示する。rayguiは必須にしない。

### 9.2 CLI

```sh
./build/waterlab --grid 256 --wave-speed 2.2 --damping 0.35 --boundary reflective
./build/waterlab_bench --grid 256 --spheres 8 --ticks 14400 --warmup 1200 --csv bench.csv
./build/waterlab_tests
```

benchは物理のみを測定し、描画FPSを報告しない。アプリは--benchmark-render 120で描画を含む120秒の計測を行えるようにする。結果にはビルド種別、OS、CPU、解像度、raylib版、S、各時間p50/p95/max、drop時間、異常・制限回数を含める。時刻取得はsteady_clock。

再現用イベントCSVは`tick,type,x,z,value,sigma`とする。typeはimpulse/spawn。impulseのvalueはm/s、spawnのvalueは中心y。tickは0開始、同tick内はファイル行順。同tickに設定変更を記録する拡張は将来とし、設定自体をログ冒頭へ保存する。

## 10. ディレクトリ構成

```text
WaterLab/
├── CMakeLists.txt
├── README.md
├── .gitignore
├── cmake/
│   └── Dependencies.cmake
├── docs/
│   ├── specification_v1.0_ja.md
│   ├── decisions.md
│   └── validation.md
├── src/
│   ├── core/
│   │   ├── Math.h
│   │   └── Units.h
│   ├── simulation/
│   │   ├── IWaveSolver.h
│   │   ├── WaveField.h/.cpp
│   │   ├── WaveSolverCPU.h/.cpp
│   │   ├── BoundaryConditions.h/.cpp
│   │   ├── Disturbance.h/.cpp
│   │   └── SimulationWorld.h/.cpp
│   ├── physics/
│   │   ├── SphereBody.h
│   │   ├── Buoyancy.h/.cpp
│   │   └── WaterInteraction.h/.cpp
│   ├── rendering/
│   │   ├── WaterMesh.h/.cpp
│   │   ├── WaterRenderer.h/.cpp
│   │   └── RaylibResources.h
│   └── app/
│       ├── Main.cpp
│       ├── ExperimentUI.h/.cpp
│       └── InputEvents.h/.cpp
├── assets/shaders/
│   ├── water.vs
│   └── water.fs
├── tests/
│   ├── TestMain.cpp
│   ├── WaveSolverTests.cpp
│   ├── BoundaryTests.cpp
│   ├── InteractionTests.cpp
│   ├── MeshTests.cpp
│   └── fixtures/events.csv
└── tools/
    └── Benchmark.cpp
```

`.h/.cpp`は対応する2ファイルを表す。生成物はbuild/へ置き、ソースツリーへ書き込まない。asset探索は起動時作業ディレクトリに依存させず、CMakeで実行ファイル隣へコピーし、その場所から解決する。

### 10.1 CMakeターゲット

- waterlab_core：simulation＋physics。raylibリンクなし。
- waterlab_render：raylib、描画と入力補助。
- waterlab：GUIアプリ。
- waterlab_tests：core＋テスト。CTest登録。
- waterlab_bench：core＋ヘッドレス計測。
- WATERLAB_BUILD_APP=OFFならraylibを取得せずテストとbenchをビルド可能。
- WATERLAB_ENABLE_SANITIZERS=ONでDebugのASan/UBSanを有効化。
- FetchContentのraylibは5.5タグへ固定し、最初の導入時に解決したcommitを依存記録へ残す。浮動mainブランチを使わない。
- CMakeのFETCHCONTENT_SOURCE_DIR_RAYLIBでローカルソース指定を可能にする。

標準手順：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/waterlab
```

初回依存取得にはネットワークが必要。オフライン時はraylibソースの所在を指定し、取得できない依存を黙って省略しない。

## 11. フェーズ別タスクと受け入れ基準

### Phase 0：ビルド・core基盤

タスク：CMake、型、格子、パラメータ検証、テスト実行、READMEのビルド手順を作る。

受け入れ：arm64でビルドできる。GUIなし構成のCTestが通る。異常設定を理由付きで拒否する。初期化後のサイズ・dx・座標が本書に一致する。

### Phase 1：2D波紋

タスク：Verlet、CFL、減衰、反射境界、インパルス、固定tick、Height表示、一時停止・リセット。

受け入れ：既定S=2。中心入力からほぼ同心円状に伝播する。静水面が自発的に動かない。入力1回が1回だけ反映される。減衰ありで振幅が長期的に下がる。30/60/144FPS相当の入力駆動でも同一tick状態が一致する。

### Phase 2：3D水面

タスク：タイルメッシュ、法線、カメラ、Waterシェーダー、4表示モード、球を除く水槽外観。

受け入れ：タイルの継ぎ目が見えない。平面法線が(0,1,0)。波紋で光の反射が変化し、視線が浅くなるほどFresnel反射が強くなる。インデックス越境がない。繰り返しリセットしてもGPU資源が増加しない。

### Phase 3：全境界・安定性

タスク：固定端、吸収帯、切替、診断値、長時間試験。

受け入れ：反射符号が境界で異なる。吸収帯で戻り波のエネルギーが反射壁より少ない。512²やc=5でもSを増やしてCFLを守る。異常入力時に停止理由を表示し、リセットで復帰できる。

### Phase 4：球体と双方向近似

タスク：球生成、落下、浸水体積、浮力、抵抗、着水力、水面反作用、航跡、ドラッグ、衝突。

受け入れ：既定の低密度球が落下して着水波を生み、減衰しながら浮く。静水面で球密度を水密度の半分にすると平衡で約半体積が浸水する。高密度球は沈む。ドラッグで波が生まれ、止めると追加入力が止まる。双方向有効・無効の比較で水面入力に差がある。8球・10分で発散しない。

### Phase 5：v1.0仕上げ

タスク：自動テスト整備、M1実測、資源検証、操作説明、制限事項、検証結果、依存版記録。

受け入れ：第12節の必須テストが通る。性能目標の実測結果を記録し、未達ならボトルネックと具体的な差を報告する。表示だけで未実装機能をあるように見せない。CPU版が単独で完成している。

### 将来Phase M：Metal Compute・高品質描画

タスク：第13節のバックエンド、CPU比較、屈折・実シーン反射・水中球描画を別々に導入。

受け入れは別の拡張仕様で確定する。v1.0ではインターフェース設計と拡張メモまでを納品する。

## 12. テストと数値検証

全数値テストは描画なしで実行可能にする。比較許容誤差は絶対誤差と相対誤差を区別し、テストソースに単位と根拠を記す。

### 12.1 必須自動テスト

| ID | テスト | 判定 |
|---|---|---|
| T01 | 静水面 | source=0、初期h=v=0で10,000 tick後も各要素0 |
| T02 | CFL | 128/256/512、複数cでC≦0.90。既定S=2。上限超過・NaN設定を拒否 |
| T03 | 補間 | 定数場・線形高さ場の内部で誤差1e-5m以下。最右端・角・領域外も確認 |
| T04 | インパルス | 1回のv変化が強度に一致。dtやSで二重注入しない |
| T05 | ゼロ平均 | 補償カーネルの面積加重積分の絶対値がΣA|K|の1e-5以下。端でも確認 |
| T06 | 境界拘束 | Fixed外周h=v=0。Reflectiveの定数場ラプラシアン0、鏡像値が仕様通り |
| T07 | 再現性 | 同一設定でイベントCSVを2回再生し同一ビルドなら配列一致 |
| T08 | FPS独立 | 追いつき上限に達しない模擬30/60/144FPS列で共通tickのh/vが一致 |
| T09 | 球冠体積 | s=0,R,2Rで0、半球、全体積。単調増加 |
| T10 | 浮力平衡 | 結合offの平水面で既定球を30s積分。最後5sで浸水率0.50±0.03、平均|vy|<0.03m/s |
| T11 | 反作用符号 | 下向き着水時、球への動的Fy>0、補償前の水面合力<0 |
| T12 | 航跡 | q=0またはU=0なら0。移動方向反転で双極子も反転。sourceの積分≈0 |
| T13 | タイル | 128/256/512で全セルを一度ずつ三角形化。全インデックスが各タイル頂点数未満 |
| T14 | 法線 | 平面と既知の線形高さの法線が解析値に一致。共有辺の法線一致 |
| T15 | 防御 | 異常高さ・非有限値で停止。リセットで全状態とキューが初期化 |

### 12.2 エネルギーと収束

波動部単独、source=0、球なしで検証する。診断エネルギーは物理的な流体エネルギーではなく、離散波動系の二次形式：

```text
E = 0.5 * Σ A_i*v_i² - 0.5*c² * Σ A_i*h_i*L(h)_i
```

境界面積重みとLを同じ条件で評価する。Fixedの拘束点は自由度に含めない。Verletではこの通常エネルギーは毎ステップ厳密に一定ではない。数値誤差による小さな振動を許容し、単調性を誤って要求しない。

- 固定端の滑らかな低周波モードを初期高さに設定し、無減衰で30s積分。Eの相対変動幅1%以下を目標とする。高周波のクリック入力へ同じ閾値を流用しない。
- 同じモードでdtを半分にし、エネルギー誤差と解の時間誤差が減少することを確認する。
- 減衰ありでは1秒移動平均Eが長期的に減少し、30s後に初期の10%未満となること。ゼロモードは除く。
- Fixedの解析モードh=A*sin(π(x+2)/4)*sin(π(z+2)/4)*cos(ωt)、ω=c*sqrt((π/4)²+(π/4)²)を使い、格子細分化と十分小さいdtでL2誤差が概ね2次で減ることを確認する。

### 12.3 境界・波速の比較実験

反射符号は細長い領域の平面波パルスで比較し、位相反転の有無を観測する。波速は細長い領域で十分な幅のパルスを2プローブで検出し、到達時刻差から求める。離散分散を考慮し、低周波パルスで既定cの±5%を目標とする。

吸収帯は4m領域の中心に同一補償入力を与え、中心領域[-1,+1]²のEを2～4sで積分する。Absorbingの積分値がReflectiveの50%以下を目標とする。γ0=0、他の条件を固定し、必要なら帯幅・γの調整理由を記録する。完全無反射を要求しない。

### 12.4 手動・実機検証

- 正負クリック、境界付近、連打、停止中入力、再開、1 tick、リセット。
- 128/256/512、c変更、境界切替で表示・物理が破綻しない。
- 球8個、端の球、沈む球、ドラッグ高速移動、ウィンドウ移動・リサイズ。
- 4表示モード、斜め視点のFresnel、タイル継ぎ目、Retinaクリック位置。
- Releaseで10分、メモリと異常カウンタを観測。Debug ASan/UBSanでcoreテストと通常操作を検証。
- 性能測定は20sウォームアップ後120s以上。測定時のM1 iMacのメモリ、OS、実ピクセル解像度、電源・負荷条件を記録する。

## 13. Metal Computeへの拡張設計

### 13.1 残すもの・置き換えるもの

波動方程式、境界、格子配置、単位、インパルス、tick入力、受け入れ用fixtureはCPU版と共通化する。Compute版はfloat32のh/v/sourceをGPUに置き、CPU版は常に参照実装として残す。

velocity Verletの前半kick＋driftと後半kickを分離したdispatchにする。hの更新を同一dispatch内で隣接スレッドへ公開してラプラシアンを読む構成は禁止。古いhから新しいhへのping-pongバッファ、dispatch間の適切な依存同期を用いる。threadgroup内barrierだけで全格子を同期できると考えない。

### 13.2 APIの移行

v1.0のcpuViewはCPUバックエンド専用の契約である。GPU版実装時に「GPUでも同期なしでCPU配列を返せる」ように偽装しない。別の能力インターフェースを加える。

```text
BackendCapabilities { cpuReadable, gpuResident, asyncSampling }
requestSamples(points, requestId) -> 非同期要求
pollSamples(requestId) -> 完了済み時刻と結果
acquireRenderSurface() -> backend固有の読み取りハンドルと同期情報
```

CPU版sampleBatchの即時応答をGPU版の各球・各サブステップで同期readbackに置き換えない。将来は球の結合もGPU上へ移す案を優先する。遅延サンプルで結合する案は、遅延による安定性を別途評価してから採用する。

### 13.3 OpenGLとの境界

Metal Computeとraylib/OpenGL間の資源共有はv1.0の前提にしない。最初のGPU検証はヘッドレス計算＋必要時の明示readbackでよい。リアルタイムGPU描画は、Metalレンダラー導入または検証済みの共有方式を別タスクとして扱う。ComputeをGPU化しただけで高速になるとは保証しない。

### 13.4 比較基準

同一の初期条件とイベントをCPU/GPUで実行し、短時間のh/v L2・最大誤差、波速、減衰、境界反射を比較する。ビット一致は要求しない。最初の目標は1秒後の相対L2誤差1e-3以下とし、ほぼゼロ場は絶対誤差で扱う。GPU計算時間・転送・同期・描画を分けて測定する。

## 14. Claude Codeへの実装指示

以下を実装時の作業指示とする。

1. 本書を`docs/specification_v1.0_ja.md`へ保存し、既存リポジトリがあれば最初にREADME・AGENTS.md・ビルド設定を確認する。既存成果物を無断で上書きしない。
2. Phase 0→5の順に実装する。最初の到達点は「ビルド可能でテストが通り、クリック波紋が見える」状態。その後も球体と検証まで継続する。
3. 各Phaseで実装・ビルド・該当テストを行い、結果を`docs/validation.md`へ追記する。未実行項目は未実行と記す。
4. 物理coreとraylibを分離し、GUIを開けない環境でもcoreテストを実行できるようにする。
5. 256標本と256セル、120Hz tickと内部dt、速度インパルスと力、物理高さと描画補間を混同しない。
6. クリック入力にdtを掛けない。連続加速度にはdtを掛ける。物体結合は全サブステップで更新する。
7. 配列境界・CFL・境界条件・面積重みを先に正しく実装する。数値発散を高さクランプで隠さない。
8. メッシュはタイル化し、raylibの所有権と解放責務を明確にする。GPUバッファを毎フレーム再生成しない。
9. 着水の波は本書の動的反作用で生成する。追加の衝撃を重ねて見栄えを調整する場合は仕様変更として理由と試験を記す。
10. 浮力・抵抗・航跡の係数調整は第12節の試験結果に基づく。静的浮力を水面へ継続的に戻す仕様へ勝手に変更しない。
11. 実装上の不明点は本書の単位・式・境界契約を優先して合理的に決め、`docs/decisions.md`へ記録する。重大な要件変更は明示する。
12. 新しい大型ライブラリやMetal、実シーン屈折を先に追加しない。将来拡張用の空実装でCPU版の必須機能を置き換えない。
13. READMEに依存導入、ビルド、実行、操作、設定、テスト、計測、既知の近似と制限を書く。
14. 最終報告は、完成機能、ビルド・テスト結果、実機性能、未実施の検証、残課題を分ける。「M1で動くはず」を実測結果として扱わない。

### 14.1 納品チェックリスト

- [ ] CPU版の全必須機能が実装されている。
- [ ] CMakeからarm64ビルドできる。
- [ ] GUIなしテストとbenchが独立して動く。
- [ ] 256²／4m／120Hz／既定2サブステップが一致する。
- [ ] 3境界・4描画モード・正負インパルスを切り替えられる。
- [ ] 球体落下・浮力・抵抗・近似反作用・航跡・ドラッグが動く。
- [ ] core自動テスト、数値比較、メモリ・実機確認の結果が記録されている。
- [ ] 物理的に保存しない量、描画の近似、未実装のMetalが明記されている。
- [ ] READMEから第三者がビルド・実験・検証を再現できる。

## 15. 参考資料と参照範囲

本書の数値設定・近似結合・受け入れ閾値はWaterLab向けの設計判断であり、参照資料が性能や物理精度を保証するものではない。

- [raylib 5.5 公開ヘッダ](https://github.com/raysan5/raylib/blob/5.5/src/raylib.h)：使用するAPIとMesh構造の確認。依存版を変更する場合は再確認する。
- [raylib 5.5 メッシュ実装](https://github.com/raysan5/raylib/blob/5.5/src/rmodels.c)：バッファ更新・描画・解放の実装確認。
- [raylib公式チートシート](https://www.raylib.com/cheatsheet/cheatsheet.html)：APIの概観。最新表記と固定版の差は固定版ソースを優先する。
- [Fix Your Timestep! 原著ソース](https://github.com/mas-bandwidth/gafferongames/blob/main/content/post/fix_your_timestep.md)：固定時間刻みと描画補間の考え方。

添付計画で挙げられていた外部デモは、本仕様の必須依存や検証済み実装として扱わない。

---
文書終端：リアルタイム水面・波紋シミュレーション仕様書 v1.0
