#include "simulation/Disturbance.h"

#include <algorithm>
#include <cmath>

#include "simulation/BoundaryConditions.h"

namespace wl {

const char* toString(ImpulseStatus status) {
    switch (status) {
        case ImpulseStatus::Applied: return "applied";
        case ImpulseStatus::InvalidParameters: return "invalid impulse parameters";
        case ImpulseStatus::OutsideDomain: return "impulse center outside domain";
        case ImpulseStatus::InsufficientSamples: return "too few samples inside the kernel";
    }
    return "unknown";
}

float minimumImpulseSigma(const WaveField& field) {
    return 2.0f * std::max(field.dx(), field.dz());
}

namespace {

struct KernelBox {
    uint32_t i0, i1, j0, j1;  // 両端を含む
};

KernelBox kernelBox(const WaveField& field, float fi, float fj, double radius) {
    const double ri = radius / field.dx();
    const double rj = radius / field.dz();
    auto lo = [](double c, double r) {
        return static_cast<uint32_t>(std::max(0.0, std::ceil(c - r)));
    };
    auto hi = [](double c, double r, uint32_t n) {
        return static_cast<uint32_t>(std::min(static_cast<double>(n - 1), std::floor(c + r)));
    };
    return {lo(fi, ri), hi(fi, ri, field.nx()), lo(fj, rj), hi(fj, rj, field.nz())};
}

}  // namespace

ImpulseStatus applyImpulseKernel(WaveField& field, BoundaryMode mode, const Impulse& impulse) {
    const float minSigma = minimumImpulseSigma(field);
    if (!std::isfinite(impulse.deltaVelocityPeak) || !std::isfinite(impulse.sigma) ||
        impulse.sigma < minSigma * (1.0f - 1e-6f)) {
        return ImpulseStatus::InvalidParameters;
    }
    float fi = 0.0f, fj = 0.0f;
    if (!field.toGridCoords(impulse.center, fi, fj)) return ImpulseStatus::OutsideDomain;

    const double sigma = impulse.sigma;
    const double r1 = 3.0 * sigma;
    const double r2 = impulse.zeroMean ? 6.0 * sigma : r1;
    const double inv1 = 1.0 / (2.0 * sigma * sigma);
    const double inv2 = 1.0 / (8.0 * sigma * sigma);  // 2σ
    const double cx = impulse.center.x;
    const double cz = impulse.center.z;
    const KernelBox box = kernelBox(field, fi, fj, r2);
    const float* area = field.areas();

    auto g1 = [&](double r2sq) { return r2sq <= r1 * r1 ? std::exp(-r2sq * inv1) : 0.0; };
    auto g2 = [&](double r2sq) { return r2sq <= r2 * r2 ? std::exp(-r2sq * inv2) : 0.0; };
    auto dist2 = [&](uint32_t i, uint32_t j) {
        const double ddx = field.xAt(i) - cx;
        const double ddz = field.zAt(j) - cz;
        return ddx * ddx + ddz * ddz;
    };

    // 1: 面積加重和と有効標本数
    double sum1 = 0.0, sum2 = 0.0;
    int samples = 0;
    for (uint32_t j = box.j0; j <= box.j1; ++j) {
        for (uint32_t i = box.i0; i <= box.i1; ++i) {
            if (isConstrained(mode, i, j, field.nx(), field.nz())) continue;
            const double d2 = dist2(i, j);
            const double a = area[field.index(i, j)];
            if (d2 <= r1 * r1) ++samples;
            sum1 += a * g1(d2);
            sum2 += a * g2(d2);
        }
    }
    if (samples < kMinImpulseSamples || sum1 <= 0.0 || sum2 <= 0.0) {
        return ImpulseStatus::InsufficientSamples;
    }
    const double b = impulse.zeroMean ? sum1 / sum2 : 0.0;
    auto kraw = [&](double d2) { return impulse.zeroMean ? g1(d2) - b * g2(d2) : g1(d2); };

    // 2: 最大絶対値による正規化
    double maxAbs = 0.0;
    for (uint32_t j = box.j0; j <= box.j1; ++j) {
        for (uint32_t i = box.i0; i <= box.i1; ++i) {
            if (isConstrained(mode, i, j, field.nx(), field.nz())) continue;
            maxAbs = std::max(maxAbs, std::abs(kraw(dist2(i, j))));
        }
    }
    if (!(maxAbs > 0.0)) return ImpulseStatus::InsufficientSamples;

    // 3: 速度へ1回だけ注入
    const double scale = impulse.deltaVelocityPeak / maxAbs;
    float* v = field.velocity();
    for (uint32_t j = box.j0; j <= box.j1; ++j) {
        for (uint32_t i = box.i0; i <= box.i1; ++i) {
            if (isConstrained(mode, i, j, field.nx(), field.nz())) continue;
            v[field.index(i, j)] += static_cast<float>(scale * kraw(dist2(i, j)));
        }
    }
    return ImpulseStatus::Applied;
}

}  // namespace wl
