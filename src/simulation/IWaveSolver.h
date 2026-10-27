#pragma once

#include <cstddef>
#include <cstdint>

#include "core/Math.h"

// 波動ソルバーのバックエンド契約（仕様書第8節）。raylib型を使わない。
namespace wl {

enum class BoundaryMode { Fixed, Reflective, Absorbing };

struct GridDesc {
    uint32_t nx = 256, nz = 256;  // 境界を含む標本点数（セル数はnx-1）
    float lengthX = 4.0f, lengthZ = 4.0f, baseHeight = 0.0f;
};

struct WaveSettings {
    float waveSpeed = 2.2f;   // c、m/s
    float damping = 0.35f;    // γ0、s⁻¹
    float cflSafety = 0.90f;  // (0,1)
    BoundaryMode boundary = BoundaryMode::Reflective;
    float spongeWidth = 0.50f;  // m
    float spongeGamma = 8.0f;   // s⁻¹
    uint32_t maxSubsteps = 16;
};

struct Impulse {
    Vec2 center{0.0f, 0.0f};          // m
    float deltaVelocityPeak = 0.25f;  // m/s、ピーク速度変化。dtは掛けない
    float sigma = 0.06f;              // m
    bool zeroMean = true;             // 補償カーネル（false: 単純ガウス）
};

struct SurfaceSample {
    float height;  // 変位m。baseHeightは含まない
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
    // accelerationは呼出し中のみ借用。countはnx*nz、またはnullptrと0（外力なし）。
    virtual void advanceSubstep(float dt,
                                const float* acceleration,
                                size_t count) = 0;
    virtual CpuFieldView cpuView() const = 0;
};

}  // namespace wl
