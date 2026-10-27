#include "app/ExperimentUI.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/Error.h"
#include "rendering/HeightMapView.h"
#include "simulation/BoundaryConditions.h"

namespace wl {

namespace {

constexpr float kMargin = 16.0f;
constexpr float kWaveSpeedStep = 0.1f, kWaveSpeedMin = 0.1f, kWaveSpeedMax = 5.0f;
constexpr float kDampingStep = 0.05f, kDampingMin = 0.0f, kDampingMax = 3.0f;
constexpr float kStrengthStep = 0.05f, kStrengthMin = 0.05f, kStrengthMax = 1.0f;
// Height表示の色範囲（m）。仕様書7.4の±0.05mは既定クリックの波高（数mm以下）では
// ほぼ白になるため、既定を±0.001mとし切替可能にした（docs/decisions.md D09）。
constexpr float kHeightRanges[] = {0.0005f, 0.001f, 0.002f, 0.005f, 0.01f, 0.02f, 0.05f};
constexpr int kHeightRangeCount = int(sizeof(kHeightRanges) / sizeof(kHeightRanges[0]));
constexpr int kDefaultHeightRange = 1;

// 刻み幅の倍数へ丸めて浮動小数の累積誤差を避ける。
float stepValue(float value, float delta, float step, float lo, float hi) {
    const float snapped = std::round((value + delta) / step) * step;
    return std::clamp(snapped, lo, hi);
}

}  // namespace

ExperimentUI::ExperimentUI(const GridDesc& grid) : grid_(grid), heightRangeIndex_(kDefaultHeightRange) {}

float ExperimentUI::heightRange() const { return kHeightRanges[heightRangeIndex_]; }

Rectangle ExperimentUI::viewRect() const {
    const float h = float(GetScreenHeight()) - 2.0f * kMargin;
    const float w = h * grid_.lengthX / grid_.lengthZ;
    return {float(GetScreenWidth()) - w - kMargin, kMargin, w, h};
}

bool ExperimentUI::screenToWater(Vector2 screen, Vec2& out) const {
    const Rectangle r = viewRect();
    // テクセル中心が格子点。u=(i+0.5)/nx
    const float u = (screen.x - r.x) / r.width;
    const float v = (screen.y - r.y) / r.height;
    const float fi = u * float(grid_.nx) - 0.5f;
    const float fj = v * float(grid_.nz) - 0.5f;
    if (fi < 0.0f || fj < 0.0f || fi > float(grid_.nx - 1) || fj > float(grid_.nz - 1)) return false;
    out.x = -0.5f * grid_.lengthX + fi * grid_.lengthX / float(grid_.nx - 1);
    out.z = -0.5f * grid_.lengthZ + fj * grid_.lengthZ / float(grid_.nz - 1);
    return true;
}

void ExperimentUI::setMessage(const std::string& message) {
    message_ = message;
    messageTime_ = GetTime();
    TraceLog(LOG_INFO, "WATERLAB: %s", message.c_str());
}

void ExperimentUI::changeSettings(SimulationWorld& world, float dc, float dGamma, bool cycleBoundary) {
    WaveSettings s = world.effectiveWaveSettings();
    s.waveSpeed = stepValue(s.waveSpeed, dc, kWaveSpeedStep, kWaveSpeedMin, kWaveSpeedMax);
    s.damping = stepValue(s.damping, dGamma, kDampingStep, kDampingMin, kDampingMax);
    if (cycleBoundary) s.boundary = nextBoundaryMode(s.boundary);
    try {
        world.requestWaveSettings(s);
    } catch (const WaterLabError& e) {
        setMessage(std::string("settings rejected: ") + e.what());
    }
}

void ExperimentUI::handleInput(SimulationWorld& world) {
    if (IsKeyPressed(KEY_SPACE)) world.setPaused(!world.paused());
    if (IsKeyPressed(KEY_N)) {
        if (world.paused()) world.stepTick();
        else setMessage("N steps one tick only while paused (Space)");
    }
    if (IsKeyPressed(KEY_R)) {
        world.reset();
        setMessage("reset");
    }
    if (IsKeyPressed(KEY_H)) showHelp_ = !showHelp_;
    if (IsKeyPressed(KEY_TAB)) changeSettings(world, 0.0f, 0.0f, true);
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) changeSettings(world, kWaveSpeedStep, 0.0f, false);
    if (IsKeyPressed(KEY_LEFT_BRACKET)) changeSettings(world, -kWaveSpeedStep, 0.0f, false);
    if (IsKeyPressed(KEY_APOSTROPHE)) changeSettings(world, 0.0f, kDampingStep, false);
    if (IsKeyPressed(KEY_SEMICOLON)) changeSettings(world, 0.0f, -kDampingStep, false);
    if (IsKeyPressed(KEY_EQUAL)) {
        clickStrength_ = stepValue(clickStrength_, kStrengthStep, kStrengthStep, kStrengthMin, kStrengthMax);
    }
    if (IsKeyPressed(KEY_MINUS)) {
        clickStrength_ = stepValue(clickStrength_, -kStrengthStep, kStrengthStep, kStrengthMin, kStrengthMax);
    }
    if (IsKeyPressed(KEY_COMMA)) heightRangeIndex_ = std::max(0, heightRangeIndex_ - 1);
    if (IsKeyPressed(KEY_PERIOD)) heightRangeIndex_ = std::min(kHeightRangeCount - 1, heightRangeIndex_ + 1);
    if (IsKeyPressed(KEY_ONE)) setMessage("display: Height");
    if (IsKeyPressed(KEY_TWO) || IsKeyPressed(KEY_THREE) || IsKeyPressed(KEY_FOUR)) {
        setMessage("Wireframe/Normal/Water modes are not implemented yet (Phase 2)");
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vec2 p;
        if (screenToWater(GetMousePosition(), p)) {
            const bool negative = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            Impulse imp;
            imp.center = p;
            imp.deltaVelocityPeak = negative ? -clickStrength_ : clickStrength_;
            imp.sigma = clickSigma_;
            if (!world.queueImpulse(imp)) setMessage("input queue full; click ignored");
        }
    }
}

void ExperimentUI::drawOverlay(const SimulationWorld& world, double frameCpuMs) const {
    const WorldDiagnostics& d = world.diagnostics();
    const WaveSettings& s = world.effectiveWaveSettings();
    const int fs = 16;
    int y = int(kMargin);
    const int x = int(kMargin);
    char line[256];
    auto text = [&](const char* str, Color c = RAYWHITE) {
        DrawText(str, x, y, fs, c);
        y += fs + 4;
    };

    text("WaterLab v1.0 - Phase 1 (2D Height view)", SKYBLUE);
    std::snprintf(line, sizeof line, "FPS %d   frame CPU %.2f ms   render %dx%d (logical %dx%d)",
                  GetFPS(), frameCpuMs, GetRenderWidth(), GetRenderHeight(), GetScreenWidth(), GetScreenHeight());
    text(line);
    std::snprintf(line, sizeof line, "tick %llu   ticks/frame %u   S %u   dt %.5f s   C %.3f",
                  (unsigned long long)d.tick, d.ticksLastFrame, d.substeps, d.substepDt, d.courant);
    text(line);
    std::snprintf(line, sizeof line, "physics tick %.3f ms   max|h| %.4f m   max|v| %.3f m/s",
                  d.lastTickSeconds * 1000.0, d.maxAbsHeight, d.maxAbsVelocity);
    text(line);
    std::snprintf(line, sizeof line, "dropped %.2f s (clamp) + %.2f s (tick cap)", d.clampedSeconds,
                  d.droppedTickSeconds);
    text(line);
    std::snprintf(line, sizeof line, "impulses applied %llu  rejected %llu  overflow %llu",
                  (unsigned long long)d.appliedImpulses, (unsigned long long)d.rejectedImpulses,
                  (unsigned long long)d.queueOverflows);
    text(line);
    y += 6;
    std::snprintf(line, sizeof line, "boundary  %s", toString(s.boundary));
    text(line, YELLOW);
    std::snprintf(line, sizeof line, "c         %.1f m/s", s.waveSpeed);
    text(line, YELLOW);
    std::snprintf(line, sizeof line, "gamma     %.2f 1/s", s.damping);
    text(line, YELLOW);
    std::snprintf(line, sizeof line, "click     +/-%.2f m/s  sigma %.2f m", clickStrength_, clickSigma_);
    text(line, YELLOW);
    std::snprintf(line, sizeof line, "height color  blue -%.4g m / white 0 / red +%.4g m (saturates)",
                  heightRange(), heightRange());
    text(line, LIGHTGRAY);
    if (world.paused()) text("PAUSED", ORANGE);

    if (d.halted) {
        y += 6;
        text("PHYSICS HALTED - press R to reset", RED);
        DrawText(d.haltReason.c_str(), x, y, 14, RED);
        y += 20;
    }
    if (!message_.empty() && GetTime() - messageTime_ < 4.0) {
        y += 6;
        DrawText(message_.c_str(), x, y, 16, GREEN);
        y += 22;
    }
    if (showHelp_) {
        y += 10;
        const char* help[] = {
            "Left click: positive ripple   Shift+click: negative ripple",
            "Space: pause/resume   N: one tick (paused)   R: reset",
            "Tab: boundary   [ ]: wave speed   ; ': damping   - =: click strength",
            ", .: height color range   1: Height (2-4: Phase 2)   H: help",
        };
        for (const char* h : help) text(h, GRAY);
    }
}

}  // namespace wl
