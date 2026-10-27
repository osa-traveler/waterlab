#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/Units.h"
#include "simulation/IWaveSolver.h"
#include "simulation/WaveSolverCPU.h"

namespace wl {

struct WorldSettings {
    GridDesc grid;
    WaveSettings wave;
    double tickDt = units::kTickDt;   // s、固定
    double maxFrameDt = 0.10;          // s、1フレームで受け付ける実時間の上限
    uint32_t maxTicksPerFrame = 8;
    uint32_t eventCapacity = 256;      // 1tickあたりのキュー容量（予約済み）
    float abortHeight = 0.5f;          // m、max|h|がこれを超えたら停止
    float abortVelocity = 10.0f;       // m/s
};

void validateWorldSettings(const WorldSettings& settings);

struct WorldDiagnostics {
    uint64_t tick = 0;                 // 完了したtick数
    uint32_t substeps = 0;             // S
    double substepDt = 0.0;            // s
    double courant = 0.0;              // C
    uint32_t ticksLastFrame = 0;
    double lastTickSeconds = 0.0;      // 直近tickの計算時間
    double clampedSeconds = 0.0;       // frameDt上限で捨てた累計時間
    double droppedTickSeconds = 0.0;   // tick上限で捨てた累計時間
    uint64_t droppedTickEvents = 0;
    float maxAbsHeight = 0.0f;         // m
    float maxAbsVelocity = 0.0f;       // m/s
    uint64_t appliedImpulses = 0;
    uint64_t rejectedImpulses = 0;
    uint64_t queueOverflows = 0;
    std::string lastRejectReason;
    bool halted = false;               // 異常停止中
    uint64_t haltTick = 0;
    std::string haltReason;
};

// 固定tick・S計算・入力消費・スナップショットを所有する（仕様書4.4、8.1）。
class SimulationWorld {
public:
    explicit SimulationWorld(const WorldSettings& settings);

    // 実時間を受け取り、必要なtickを進める。停止中・異常停止中は何もしない。
    void advanceFrame(double frameDt);
    // 1tickだけ進める（停止中のN操作、ヘッドレス再生）。異常停止中は進めない。
    void stepTick();

    // 次の物理tickで1回だけvelocityへ反映する。キュー満杯ならfalse。
    bool queueImpulse(const Impulse& impulse);
    // 即座に検査し（不正ならWaterLabError）、次のtick境界で適用する。
    void requestWaveSettings(const WaveSettings& settings);

    void setPaused(bool paused);
    bool paused() const { return paused_; }
    // h/v・入力キュー・accumulator・描画履歴・tick番号・異常状態を初期化する。
    void reset();

    const WorldSettings& settings() const { return settings_; }
    // 適用待ちがあればその値、なければ現在値。
    const WaveSettings& effectiveWaveSettings() const;
    const WorldDiagnostics& diagnostics() const { return diag_; }
    const IWaveSolver& solver() const { return *solver_; }
    IWaveSolver& solver() { return *solver_; }

    // 描画補間係数。停止中は最新tickを表示するため1。
    float renderAlpha() const;
    // lerp(renderPrevious, renderCurrent, alpha)をoutへ書く。長さnx*nz。
    void interpolatedHeight(float* out) const;
    const std::vector<float>& renderCurrent() const { return renderCurrent_; }

    // 各tick完了後に呼ばれる（検証用）。
    using TickObserver = std::function<void(uint64_t tick, const SimulationWorld&)>;
    void setTickObserver(TickObserver observer) { observer_ = std::move(observer); }

private:
    void runTick();
    bool checkAnomaly();

    WorldSettings settings_;
    std::unique_ptr<WaveSolverCPU> solver_;
    SubstepPlan plan_;
    bool hasPendingSettings_ = false;
    WaveSettings pendingSettings_;
    SubstepPlan pendingPlan_{};

    std::vector<Impulse> events_;
    std::vector<float> renderPrevious_;
    std::vector<float> renderCurrent_;
    double accumulator_ = 0.0;
    bool paused_ = false;
    WorldDiagnostics diag_;
    TickObserver observer_;
};

}  // namespace wl
