#include "simulation/SimulationWorld.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "core/Error.h"
#include "simulation/BoundaryConditions.h"

namespace wl {

void validateWorldSettings(const WorldSettings& s) {
    if (!std::isfinite(s.tickDt) || s.tickDt <= 0.0) throw WaterLabError("tickDt must be > 0 s");
    if (!std::isfinite(s.maxFrameDt) || s.maxFrameDt <= 0.0) throw WaterLabError("maxFrameDt must be > 0 s");
    if (s.maxTicksPerFrame < 1) throw WaterLabError("maxTicksPerFrame must be >= 1");
    if (s.eventCapacity < 1) throw WaterLabError("eventCapacity must be >= 1");
    if (!(s.abortHeight > 0.0f) || !(s.abortVelocity > 0.0f)) {
        throw WaterLabError("abort thresholds must be > 0");
    }
    planSubsteps(s.grid, s.wave, s.tickDt);
}

SimulationWorld::SimulationWorld(const WorldSettings& settings) : settings_(settings) {
    validateWorldSettings(settings);
    solver_ = std::make_unique<WaveSolverCPU>(settings.grid, settings.wave);
    plan_ = planSubsteps(settings.grid, settings.wave, settings.tickDt);
    const size_t n = solver_->field().count();
    events_.reserve(settings.eventCapacity);
    renderPrevious_.assign(n, 0.0f);
    renderCurrent_.assign(n, 0.0f);
    diag_.substeps = plan_.substeps;
    diag_.substepDt = plan_.dt;
    diag_.courant = plan_.courant;
}

const WaveSettings& SimulationWorld::effectiveWaveSettings() const {
    return hasPendingSettings_ ? pendingSettings_ : solver_->settings();
}

bool SimulationWorld::queueImpulse(const Impulse& impulse) {
    if (events_.size() >= settings_.eventCapacity) {
        ++diag_.queueOverflows;
        return false;
    }
    events_.push_back(impulse);
    return true;
}

void SimulationWorld::requestWaveSettings(const WaveSettings& settings) {
    const SubstepPlan plan = planSubsteps(settings_.grid, settings, settings_.tickDt);
    pendingSettings_ = settings;
    pendingPlan_ = plan;
    hasPendingSettings_ = true;
}

void SimulationWorld::setPaused(bool paused) {
    paused_ = paused;
}

void SimulationWorld::reset() {
    solver_->reset();
    events_.clear();
    std::fill(renderPrevious_.begin(), renderPrevious_.end(), 0.0f);
    std::fill(renderCurrent_.begin(), renderCurrent_.end(), 0.0f);
    accumulator_ = 0.0;
    const WorldDiagnostics old = diag_;
    diag_ = WorldDiagnostics{};
    diag_.substeps = plan_.substeps;
    diag_.substepDt = plan_.dt;
    diag_.courant = plan_.courant;
    // 時間を捨てた累計はセッション全体の記録として残す。
    diag_.clampedSeconds = old.clampedSeconds;
    diag_.droppedTickSeconds = old.droppedTickSeconds;
    diag_.droppedTickEvents = old.droppedTickEvents;
}

void SimulationWorld::advanceFrame(double frameDt) {
    diag_.ticksLastFrame = 0;
    if (paused_ || diag_.halted) return;
    if (!std::isfinite(frameDt) || frameDt < 0.0) frameDt = 0.0;
    if (frameDt > settings_.maxFrameDt) {
        diag_.clampedSeconds += frameDt - settings_.maxFrameDt;
        frameDt = settings_.maxFrameDt;
    }
    accumulator_ += frameDt;

    uint32_t ticks = 0;
    while (accumulator_ >= settings_.tickDt && ticks < settings_.maxTicksPerFrame) {
        runTick();
        accumulator_ -= settings_.tickDt;
        ++ticks;
        if (diag_.halted) {
            accumulator_ = 0.0;
            break;
        }
    }
    if (accumulator_ >= settings_.tickDt) {
        const double keep = std::fmod(accumulator_, settings_.tickDt);
        diag_.droppedTickSeconds += accumulator_ - keep;
        ++diag_.droppedTickEvents;
        accumulator_ = keep;
    }
    diag_.ticksLastFrame = ticks;
}

void SimulationWorld::stepTick() {
    if (diag_.halted) return;
    runTick();
}

void SimulationWorld::runTick() {
    const auto start = std::chrono::steady_clock::now();

    // tick境界の設定変更
    if (hasPendingSettings_) {
        solver_->applySettings(pendingSettings_);
        plan_ = pendingPlan_;
        hasPendingSettings_ = false;
        diag_.substeps = plan_.substeps;
        diag_.substepDt = plan_.dt;
        diag_.courant = plan_.courant;
    }

    renderPrevious_.swap(renderCurrent_);

    // 入力イベントを1回だけ反映
    for (const Impulse& e : events_) {
        try {
            solver_->applyImpulse(e);
            ++diag_.appliedImpulses;
        } catch (const WaterLabError& err) {
            ++diag_.rejectedImpulses;
            diag_.lastRejectReason = err.what();
        }
    }
    events_.clear();

    const float dt = static_cast<float>(plan_.dt);
    for (uint32_t s = 0; s < plan_.substeps; ++s) {
        solver_->advanceSubstep(dt, nullptr, 0);
        if (checkAnomaly()) break;
    }

    const CpuFieldView view = solver_->cpuView();
    std::copy(view.height, view.height + renderCurrent_.size(), renderCurrent_.begin());
    ++diag_.tick;
    diag_.lastTickSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (observer_) observer_(diag_.tick, *this);
}

bool SimulationWorld::checkAnomaly() {
    const CpuFieldView view = solver_->cpuView();
    const size_t n = static_cast<size_t>(view.nx) * view.nz;
    float maxH = 0.0f, maxV = 0.0f;
    bool finite = true;
    for (size_t k = 0; k < n; ++k) {
        const float h = view.height[k];
        const float v = view.velocity[k];
        if (!std::isfinite(h) || !std::isfinite(v)) {
            finite = false;
            break;
        }
        maxH = std::max(maxH, std::abs(h));
        maxV = std::max(maxV, std::abs(v));
    }
    diag_.maxAbsHeight = maxH;
    diag_.maxAbsVelocity = maxV;

    const char* cause = nullptr;
    if (!finite) cause = "non-finite height/velocity (NaN/Inf)";
    else if (maxH > settings_.abortHeight) cause = "max|h| exceeded abort threshold";
    else if (maxV > settings_.abortVelocity) cause = "max|v| exceeded abort threshold";
    if (!cause) return false;

    const WaveSettings& w = solver_->settings();
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "%s at tick %llu (max|h|=%.3g m, max|v|=%.3g m/s; c=%.2f, gamma=%.2f, %s, S=%u)",
                  cause, static_cast<unsigned long long>(diag_.tick), maxH, maxV, w.waveSpeed,
                  w.damping, toString(w.boundary), plan_.substeps);
    diag_.halted = true;
    diag_.haltTick = diag_.tick;
    diag_.haltReason = buf;
    return true;
}

float SimulationWorld::renderAlpha() const {
    if (paused_ || diag_.halted) return 1.0f;
    return static_cast<float>(std::min(1.0, accumulator_ / settings_.tickDt));
}

void SimulationWorld::interpolatedHeight(float* out) const {
    const float alpha = renderAlpha();
    const size_t n = renderCurrent_.size();
    for (size_t k = 0; k < n; ++k) {
        out[k] = lerpf(renderPrevious_[k], renderCurrent_[k], alpha);
    }
}

}  // namespace wl
