#include "simulation/WaveSolverCPU.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "core/Error.h"
#include "simulation/BoundaryConditions.h"
#include "simulation/Disturbance.h"

namespace wl {

namespace {

// 浮動小数の丸めでCがcflSafetyをわずかに超える場合の許容。
constexpr double kCourantTolerance = 1e-6;

bool finitePositive(float v) { return std::isfinite(v) && v > 0.0f; }

}  // namespace

double courantFactor(const GridDesc& grid) {
    const double dx = static_cast<double>(grid.lengthX) / (grid.nx - 1);
    const double dz = static_cast<double>(grid.lengthZ) / (grid.nz - 1);
    return std::sqrt(1.0 / (dx * dx) + 1.0 / (dz * dz));
}

void validateWaveSettings(const GridDesc& grid, const WaveSettings& s) {
    validateGrid(grid);
    if (!finitePositive(s.waveSpeed)) {
        throw WaterLabError("waveSpeed must be finite and > 0 m/s");
    }
    if (!std::isfinite(s.damping) || s.damping < 0.0f) {
        throw WaterLabError("damping must be finite and >= 0 1/s");
    }
    if (!std::isfinite(s.cflSafety) || s.cflSafety <= 0.0f || s.cflSafety >= 1.0f) {
        throw WaterLabError("cflSafety must be in (0, 1)");
    }
    const float halfMin = 0.5f * std::min(grid.lengthX, grid.lengthZ);
    if (!std::isfinite(s.spongeWidth) || s.spongeWidth < 0.0f || s.spongeWidth >= halfMin) {
        throw WaterLabError("spongeWidth must be >= 0 and less than half of each domain axis");
    }
    if (!std::isfinite(s.spongeGamma) || s.spongeGamma < 0.0f) {
        throw WaterLabError("spongeGamma must be finite and >= 0 1/s");
    }
    if (s.maxSubsteps < 1) {
        throw WaterLabError("maxSubsteps must be >= 1");
    }
}

SubstepPlan planSubsteps(const GridDesc& grid, const WaveSettings& s, double tickDt) {
    validateWaveSettings(grid, s);
    if (!std::isfinite(tickDt) || tickDt <= 0.0) {
        throw WaterLabError("tickDt must be finite and > 0 s");
    }
    const double k = courantFactor(grid);
    const double dtLimit = s.cflSafety / (s.waveSpeed * k);
    const double ratio = tickDt / dtLimit;
    // ratioが整数に丸め誤差で乗った場合に余分なサブステップを作らない。
    const double steps = std::max(1.0, std::ceil(ratio - 1e-9));
    if (steps > s.maxSubsteps) {
        throw WaterLabError("CFL requires " + std::to_string(static_cast<long long>(steps)) +
                            " substeps but maxSubsteps is " + std::to_string(s.maxSubsteps));
    }
    SubstepPlan plan;
    plan.substeps = static_cast<uint32_t>(steps);
    plan.dt = tickDt / steps;
    plan.dtLimit = dtLimit;
    plan.courant = s.waveSpeed * plan.dt * k;
    return plan;
}

WaveSolverCPU::WaveSolverCPU(const GridDesc& grid, const WaveSettings& settings)
    : field_(grid), settings_(settings) {
    validateWaveSettings(grid, settings);
    const size_t n = field_.count();
    laplacian_.assign(n, 0.0f);
    gamma_.assign(n, 0.0f);
    decay_.assign(n, 1.0f);
    scratch_.assign(n, 0.0f);
    buildDampingField(field_, settings_, gamma_.data());
    laplacianValid_ = true;  // h=0
}

void WaveSolverCPU::reset() {
    field_.clear();
    std::fill(laplacian_.begin(), laplacian_.end(), 0.0f);
    laplacianValid_ = true;
    ++revision_;
}

void WaveSolverCPU::applySettings(const WaveSettings& settings) {
    validateWaveSettings(field_.grid(), settings);  // 失敗時は変更前のまま
    const bool toFixed = settings.boundary == BoundaryMode::Fixed &&
                         settings_.boundary != BoundaryMode::Fixed;
    settings_ = settings;
    buildDampingField(field_, settings_, gamma_.data());
    decayDt_ = -1.0f;
    if (toFixed) applyBoundaryConstraint(settings_.boundary, field_);
    laplacianValid_ = false;
    ++revision_;
}

void WaveSolverCPU::applyImpulse(const Impulse& impulse) {
    const ImpulseStatus status = applyImpulseKernel(field_, settings_.boundary, impulse);
    if (status != ImpulseStatus::Applied) {
        throw WaterLabError(std::string("impulse rejected: ") + toString(status));
    }
    ++revision_;
}

void WaveSolverCPU::sampleBatch(const Vec2* xz, SurfaceSample* out, size_t count) const {
    for (size_t k = 0; k < count; ++k) {
        float fi = 0.0f, fj = 0.0f;
        if (!field_.toGridCoords(xz[k], fi, fj)) {
            out[k] = {0.0f, 0.0f, {0.0f, 1.0f, 0.0f}, false};
            continue;
        }
        float h = 0.0f, hx = 0.0f, hz = 0.0f;
        float v = 0.0f, vx = 0.0f, vz = 0.0f;
        field_.bilinear(field_.height(), fi, fj, h, hx, hz);
        field_.bilinear(field_.velocity(), fi, fj, v, vx, vz);
        out[k] = {h, v, normalize({-hx, 1.0f, -hz}), true};
    }
}

void WaveSolverCPU::rebuildDecay(float dt) {
    const size_t n = field_.count();
    for (size_t k = 0; k < n; ++k) {
        decay_[k] = static_cast<float>(std::exp(-0.5 * static_cast<double>(gamma_[k]) * dt));
    }
    decayDt_ = dt;
}

void WaveSolverCPU::advanceSubstep(float dt, const float* acceleration, size_t count) {
    if (!std::isfinite(dt) || dt <= 0.0f) {
        throw WaterLabError("advanceSubstep: dt must be finite and > 0");
    }
    const double courant = settings_.waveSpeed * static_cast<double>(dt) * courantFactor(field_.grid());
    if (courant > settings_.cflSafety * (1.0 + kCourantTolerance)) {
        throw WaterLabError("advanceSubstep: CFL violated (C=" + std::to_string(courant) + ")");
    }
    const size_t n = field_.count();
    if (acceleration == nullptr ? count != 0 : count != n) {
        throw WaterLabError("advanceSubstep: acceleration count must be nx*nz (or 0 with nullptr)");
    }

    if (decayDt_ != dt) rebuildDecay(dt);
    const BoundaryMode mode = settings_.boundary;
    // 前サブステップの手順8で求めたc²L(h)はhが変わっていなければ再利用できる。
    if (!laplacianValid_) computeLaplacian(field_, mode, field_.height(), laplacian_.data());

    float* h = field_.height();
    float* v = field_.velocity();
    const float* lap = laplacian_.data();
    const float* decay = decay_.data();
    const float c2 = settings_.waveSpeed * settings_.waveSpeed;
    const float halfDt = 0.5f * dt;

    // 3-6: 減衰半分、前半kick、drift
    if (acceleration) {
        for (size_t k = 0; k < n; ++k) {
            v[k] = v[k] * decay[k] + halfDt * (c2 * lap[k] + acceleration[k]);
            h[k] += dt * v[k];
        }
    } else {
        for (size_t k = 0; k < n; ++k) {
            v[k] = v[k] * decay[k] + halfDt * c2 * lap[k];
            h[k] += dt * v[k];
        }
    }
    // 7: 境界拘束
    applyBoundaryConstraint(mode, field_);
    // 8-10: 更新済みhで後半kick、減衰半分
    computeLaplacian(field_, mode, h, laplacian_.data());
    if (acceleration) {
        for (size_t k = 0; k < n; ++k) {
            v[k] = (v[k] + halfDt * (c2 * lap[k] + acceleration[k])) * decay[k];
        }
    } else {
        for (size_t k = 0; k < n; ++k) {
            v[k] = (v[k] + halfDt * c2 * lap[k]) * decay[k];
        }
    }
    // 11: 境界拘束の再適用（hは変わらないのでラプラシアンは有効のまま）
    applyBoundaryConstraint(mode, field_);
    laplacianValid_ = true;
    ++revision_;
}

CpuFieldView WaveSolverCPU::cpuView() const {
    return {field_.height(), field_.velocity(), field_.nx(), field_.nz(), revision_};
}

void WaveSolverCPU::setState(const float* height, const float* velocity) {
    const size_t bytes = field_.count() * sizeof(float);
    std::memcpy(field_.height(), height, bytes);
    std::memcpy(field_.velocity(), velocity, bytes);
    applyBoundaryConstraint(settings_.boundary, field_);
    laplacianValid_ = false;
    ++revision_;
}

double WaveSolverCPU::energy() const {
    computeLaplacian(field_, settings_.boundary, field_.height(), scratch_.data());
    const float* h = field_.height();
    const float* v = field_.velocity();
    const float* a = field_.areas();
    const double c2 = static_cast<double>(settings_.waveSpeed) * settings_.waveSpeed;
    double kinetic = 0.0, potential = 0.0;
    for (uint32_t j = 0; j < field_.nz(); ++j) {
        for (uint32_t i = 0; i < field_.nx(); ++i) {
            if (isConstrained(settings_.boundary, i, j, field_.nx(), field_.nz())) continue;
            const size_t k = field_.index(i, j);
            kinetic += static_cast<double>(a[k]) * v[k] * v[k];
            potential += static_cast<double>(a[k]) * h[k] * scratch_[k];
        }
    }
    return 0.5 * kinetic - 0.5 * c2 * potential;
}

}  // namespace wl
