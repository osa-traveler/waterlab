#pragma once

// SI単位系。長さm、時間s、質量kg、力N、速度m/s、加速度m/s²。
// 右手系、Y上向き、水平方向X/Z。静水面y0=0、水底y=-1m。
namespace wl::units {

constexpr double kTickDt = 1.0 / 120.0;   // s、物理tick
constexpr float kGravity = 9.81f;         // m/s²、下向き
constexpr float kWaterDensity = 1000.0f;  // kg/m³
constexpr float kStillWaterY = 0.0f;      // m
constexpr float kTankBottomY = -1.0f;     // m

}  // namespace wl::units
