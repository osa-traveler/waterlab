#pragma once

#include <cstdint>
#include <string>

#include "simulation/IWaveSolver.h"
#include "simulation/WaveField.h"

namespace wl {

const char* toString(BoundaryMode mode);
bool parseBoundaryMode(const std::string& text, BoundaryMode& out);
BoundaryMode nextBoundaryMode(BoundaryMode mode);

// Fixedの最外周はh=v=0の拘束点で、積分・正規化・平均の対象から除く。
inline bool isConstrained(BoundaryMode mode, uint32_t i, uint32_t j, uint32_t nx, uint32_t nz) {
    return mode == BoundaryMode::Fixed && (i == 0 || j == 0 || i == nx - 1 || j == nz - 1);
}

// 5点ラプラシアン。Reflective/Absorbingは鏡像ゴースト（h[-1]=h[1]、h[N]=h[N-2]）を
// 全標本に適用。Fixedは拘束点の値を0とする。
void computeLaplacian(const WaveField& field, BoundaryMode mode, const float* h, float* out);

// Fixedなら最外周のh=v=0。他モードでは何もしない（Neumann条件はラプラシアン側）。
void applyBoundaryConstraint(BoundaryMode mode, WaveField& field);

// 吸収帯の重みs∈[0,1]。最寄りの領域端までの距離dを使うため角で二重加算しない。
float spongeWeight(const WaveField& field, uint32_t i, uint32_t j, float spongeWidth);

// γ(x,z)、s⁻¹。Absorbingのみ吸収帯を加算する。
void buildDampingField(const WaveField& field, const WaveSettings& settings, float* gamma);

}  // namespace wl
