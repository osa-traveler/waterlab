#include "simulation/WaveField.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

#include "core/Error.h"

namespace wl {

namespace {
constexpr uint32_t kMinSamples = 3;
constexpr uint32_t kMaxSamples = 4096;
}  // namespace

void validateGrid(const GridDesc& grid) {
    if (grid.nx < kMinSamples || grid.nz < kMinSamples) {
        throw WaterLabError("grid: nx/nz must be at least 3 samples");
    }
    if (grid.nx > kMaxSamples || grid.nz > kMaxSamples) {
        throw WaterLabError("grid: nx/nz must not exceed 4096 samples");
    }
    // 配列サイズ乗算のオーバーフロー検査（作業配列を含め最大16本を想定）。
    const uint64_t count = static_cast<uint64_t>(grid.nx) * grid.nz;
    if (count > std::numeric_limits<size_t>::max() / (sizeof(float) * 16)) {
        throw WaterLabError("grid: sample count overflows memory size");
    }
    if (!std::isfinite(grid.lengthX) || !std::isfinite(grid.lengthZ) ||
        grid.lengthX <= 0.0f || grid.lengthZ <= 0.0f) {
        throw WaterLabError("grid: lengthX/lengthZ must be finite and positive");
    }
    if (!std::isfinite(grid.baseHeight)) {
        throw WaterLabError("grid: baseHeight must be finite");
    }
}

WaveField::WaveField(const GridDesc& grid) : grid_(grid), count_(0), dx_(0.0f), dz_(0.0f) {
    validateGrid(grid);
    count_ = static_cast<size_t>(grid.nx) * grid.nz;
    dx_ = static_cast<float>(static_cast<double>(grid.lengthX) / (grid.nx - 1));
    dz_ = static_cast<float>(static_cast<double>(grid.lengthZ) / (grid.nz - 1));

    areas_.assign(count_, 0.0f);
    const float a = cellArea();
    for (uint32_t j = 0; j < grid.nz; ++j) {
        const float wz = (j == 0 || j == grid.nz - 1) ? 0.5f : 1.0f;
        for (uint32_t i = 0; i < grid.nx; ++i) {
            const float wx = (i == 0 || i == grid.nx - 1) ? 0.5f : 1.0f;
            areas_[index(i, j)] = a * wx * wz;
        }
    }
    height_.assign(count_, 0.0f);
    velocity_.assign(count_, 0.0f);
}

float WaveField::xAt(uint32_t i) const {
    return static_cast<float>(-0.5 * grid_.lengthX + static_cast<double>(i) * dx_);
}

float WaveField::zAt(uint32_t j) const {
    return static_cast<float>(-0.5 * grid_.lengthZ + static_cast<double>(j) * dz_);
}

void WaveField::clear() {
    std::fill(height_.begin(), height_.end(), 0.0f);
    std::fill(velocity_.begin(), velocity_.end(), 0.0f);
}

bool WaveField::toGridCoords(Vec2 p, float& fi, float& fj) const {
    if (!isFinite(p)) return false;
    const double gi = (static_cast<double>(p.x) + 0.5 * grid_.lengthX) / dx_;
    const double gj = (static_cast<double>(p.z) + 0.5 * grid_.lengthZ) / dz_;
    const double maxI = grid_.nx - 1;
    const double maxJ = grid_.nz - 1;
    // 浮動小数の丸めで端点がわずかに外へ出る場合だけ許容する。
    constexpr double kEdgeTolerance = 1e-5;
    if (gi < -kEdgeTolerance || gi > maxI + kEdgeTolerance ||
        gj < -kEdgeTolerance || gj > maxJ + kEdgeTolerance) {
        return false;
    }
    fi = static_cast<float>(std::min(std::max(gi, 0.0), maxI));
    fj = static_cast<float>(std::min(std::max(gj, 0.0), maxJ));
    return true;
}

void WaveField::bilinear(const float* values, float fi, float fj, float& value,
                         float& gradX, float& gradZ) const {
    const uint32_t i0 = std::min(static_cast<uint32_t>(fi), grid_.nx - 2);
    const uint32_t j0 = std::min(static_cast<uint32_t>(fj), grid_.nz - 2);
    const float tx = fi - static_cast<float>(i0);
    const float tz = fj - static_cast<float>(j0);
    const float v00 = values[index(i0, j0)];
    const float v10 = values[index(i0 + 1, j0)];
    const float v01 = values[index(i0, j0 + 1)];
    const float v11 = values[index(i0 + 1, j0 + 1)];
    const float a = lerpf(v00, v10, tx);
    const float b = lerpf(v01, v11, tx);
    value = lerpf(a, b, tz);
    gradX = lerpf(v10 - v00, v11 - v01, tz) / dx_;
    gradZ = (b - a) / dz_;
}

}  // namespace wl
