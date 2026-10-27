#pragma once

#include <cstdint>
#include <vector>

#include "raylib.h"
#include "rendering/RaylibResources.h"

namespace wl {

// 高さ場の上面2D表示（Heightモード）。hを±rangeで青～白～赤に写像し、超過は飽和する。
// 画像のx方向が格子i（+X）、y方向が格子j（+Z）。
class HeightMapView {
public:
    HeightMapView(uint32_t nx, uint32_t nz);

    void update(const float* height, float range);
    void draw(Rectangle dest) const;

    static Color heightColor(float h, float range);

private:
    std::vector<Color> pixels_;
    UniqueTexture texture_;
};

}  // namespace wl
