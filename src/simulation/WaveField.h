#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "simulation/IWaveSolver.h"

namespace wl {

// 格子が有効でなければWaterLabErrorを投げる。
void validateGrid(const GridDesc& grid);

// 高さ場の格子・保持配列・台形則面積・補間。
// x(i) = -Lx/2 + i*dx、z(j) = -Lz/2 + j*dz、index(i,j) = j*nx + i。
class WaveField {
public:
    explicit WaveField(const GridDesc& grid);

    const GridDesc& grid() const { return grid_; }
    uint32_t nx() const { return grid_.nx; }
    uint32_t nz() const { return grid_.nz; }
    size_t count() const { return count_; }
    float dx() const { return dx_; }
    float dz() const { return dz_; }
    float cellArea() const { return dx_ * dz_; }

    float xAt(uint32_t i) const;
    float zAt(uint32_t j) const;
    size_t index(uint32_t i, uint32_t j) const { return static_cast<size_t>(j) * grid_.nx + i; }

    // 台形則の積分面積。内部A、辺A/2、角A/4。
    const float* areas() const { return areas_.data(); }

    float* height() { return height_.data(); }
    const float* height() const { return height_.data(); }
    float* velocity() { return velocity_.data(); }
    const float* velocity() const { return velocity_.data(); }

    void clear();

    // XZを連続格子座標へ変換。領域外・非有限ならfalse。端へはクランプしない。
    bool toGridCoords(Vec2 p, float& fi, float& fj) const;

    // 双線形補間。toGridCoordsで得た領域内座標を渡す。最右端は最後のセルの係数1。
    void bilinear(const float* values, float fi, float fj, float& value,
                  float& gradX, float& gradZ) const;

private:
    GridDesc grid_;
    size_t count_;
    float dx_, dz_;
    std::vector<float> areas_;
    std::vector<float> height_;
    std::vector<float> velocity_;
};

}  // namespace wl
