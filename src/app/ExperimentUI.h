#pragma once

#include <string>

#include "raylib.h"
#include "simulation/SimulationWorld.h"

namespace wl {

// キー操作・マウス入力・数値表示。入力はWorldのキューと設定要求を経由し、場を直接変更しない。
class ExperimentUI {
public:
    explicit ExperimentUI(const GridDesc& grid);

    // 水面表示の画面上の矩形（論理座標）。ウィンドウ高さに合わせた正方形。
    Rectangle viewRect() const;

    void handleInput(SimulationWorld& world);
    void drawOverlay(const SimulationWorld& world, double frameCpuMs) const;

    float clickStrength() const { return clickStrength_; }
    // Height表示の色範囲±m。物理には影響しない。
    float heightRange() const;

private:
    bool screenToWater(Vector2 screen, Vec2& out) const;
    void changeSettings(SimulationWorld& world, float dc, float dGamma, bool cycleBoundary);
    void setMessage(const std::string& message);

    GridDesc grid_;
    float clickStrength_ = 0.25f;  // m/s、ピーク速度変化の絶対値
    float clickSigma_ = 0.06f;     // m
    int heightRangeIndex_;
    bool showHelp_ = true;
    std::string message_;
    double messageTime_ = -100.0;
};

}  // namespace wl
