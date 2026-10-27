#include <algorithm>
#include <cmath>
#include <vector>

#include "TestHarness.h"
#include "simulation/BoundaryConditions.h"
#include "simulation/WaveSolverCPU.h"

using namespace wl;

// T06: Fixedの外周h=v=0。
WL_TEST(T06_FixedBoundaryStaysZero) {
    WaveSettings s;
    s.boundary = BoundaryMode::Fixed;
    WaveSolverCPU solver(GridDesc{}, s);
    Impulse imp;
    imp.center = {1.85f, -1.85f};
    solver.applyImpulse(imp);
    for (int k = 0; k < 400; ++k) solver.advanceSubstep(1.0f / 240.0f, nullptr, 0);
    const WaveField& f = solver.field();
    bool zero = true;
    float interior = 0.0f;
    for (uint32_t j = 0; j < f.nz(); ++j) {
        for (uint32_t i = 0; i < f.nx(); ++i) {
            const size_t k = f.index(i, j);
            if (isConstrained(BoundaryMode::Fixed, i, j, f.nx(), f.nz())) {
                zero = zero && f.height()[k] == 0.0f && f.velocity()[k] == 0.0f;
            } else {
                interior = std::max(interior, std::abs(f.height()[k]));
            }
        }
    }
    WL_CHECK(zero);
    WL_CHECK(interior > 0.0f);
}

// T06: Reflectiveの定数場ラプラシアンは境界を含め0。
WL_TEST(T06_ReflectiveConstantLaplacianZero) {
    WaveField f(GridDesc{});
    std::vector<float> h(f.count(), 0.037f), lap(f.count(), 1.0f);
    computeLaplacian(f, BoundaryMode::Reflective, h.data(), lap.data());
    WL_CHECK(std::all_of(lap.begin(), lap.end(), [](float x) { return x == 0.0f; }));
}

// T06: 鏡像ゴースト値 h[-1]=h[1]、h[N]=h[N-2]。角は両軸へ独立適用。
WL_TEST(T06_ReflectiveMirrorGhosts) {
    GridDesc g;
    g.nx = 8;
    g.nz = 6;
    WaveField f(g);
    std::vector<float> h(f.count()), lap(f.count());
    for (size_t k = 0; k < h.size(); ++k) h[k] = std::sin(0.7f * float(k)) * 0.01f;
    computeLaplacian(f, BoundaryMode::Reflective, h.data(), lap.data());
    const float idx2 = 1.0f / (f.dx() * f.dx());
    const float idz2 = 1.0f / (f.dz() * f.dz());
    auto H = [&](int i, int j) {
        if (i < 0) i = 1;
        if (i > 7) i = 6;
        if (j < 0) j = 1;
        if (j > 5) j = 4;
        return h[f.index(uint32_t(i), uint32_t(j))];
    };
    for (int j = 0; j < 6; ++j) {
        for (int i = 0; i < 8; ++i) {
            const float expected = (H(i + 1, j) - 2 * H(i, j) + H(i - 1, j)) * idx2 +
                                   (H(i, j + 1) - 2 * H(i, j) + H(i, j - 1)) * idz2;
            // float演算順序差（相対1e-5）
            WL_CHECK_NEAR(lap[f.index(uint32_t(i), uint32_t(j))], expected, std::abs(expected) * 1e-5 + 1e-6);
        }
    }
}

// Reflectiveでは変位が同符号で、Fixedでは反転して反射する（細長い領域の平面波パルス）。
// Fixedでは側壁もh=0になるため、z方向は両モード共通の最低次モードsin(π(z+Lz/2)/Lz)とする。
WL_TEST(ReflectionSignDependsOnBoundary) {
    auto reflectedPeak = [](BoundaryMode mode) {
        GridDesc g;
        g.nx = 401;
        g.nz = 101;
        g.lengthX = 4.0f;
        g.lengthZ = 1.0f;
        WaveSettings s;
        s.boundary = mode;
        s.damping = 0.0f;
        s.spongeWidth = 0.0f;
        WaveSolverCPU solver(g, s);
        const WaveField& f = solver.field();
        const float pi = 3.14159265f;
        std::vector<float> h(f.count()), v(f.count(), 0.0f);
        for (uint32_t j = 0; j < f.nz(); ++j)
            for (uint32_t i = 0; i < f.nx(); ++i) {
                const float x = f.xAt(i);
                const float zMode = std::sin(pi * float(j) / float(f.nz() - 1));
                h[f.index(i, j)] = 0.01f * zMode * std::exp(-x * x / (2 * 0.1f * 0.1f));
            }
        solver.setState(h.data(), v.data());
        // 右向きの半分がx=2で反射し、x=1.0へ戻るまで: 経路3m / 2.2m/s ≈ 1.36s
        const SubstepPlan p = planSubsteps(g, s, 1.0 / 120.0);
        for (int t = 0; t < int(1.36 * 120); ++t)
            for (uint32_t k = 0; k < p.substeps; ++k) solver.advanceSubstep(float(p.dt), nullptr, 0);
        float extreme = 0.0f;
        const uint32_t mid = f.nz() / 2;
        for (uint32_t i = 0; i < f.nx(); ++i) {
            const float x = f.xAt(i);
            const float hv = f.height()[f.index(i, mid)];
            if (x > 0.6f && x < 1.4f && std::abs(hv) > std::abs(extreme)) extreme = hv;
        }
        return extreme;
    };
    // 入射振幅は約0.005m。符号が明確に分かれることを確認する。
    WL_CHECK(reflectedPeak(BoundaryMode::Fixed) < -1e-3f);
    WL_CHECK(reflectedPeak(BoundaryMode::Reflective) > 1e-3f);
}

// 吸収帯: 角で減衰を二重加算せず、Absorbing以外では加算しない。
WL_TEST(SpongeDampingField) {
    WaveSettings s;
    s.boundary = BoundaryMode::Absorbing;
    WaveField f(GridDesc{});
    std::vector<float> gamma(f.count());
    buildDampingField(f, s, gamma.data());
    const float edge = s.damping + s.spongeGamma;
    WL_CHECK_NEAR(gamma[f.index(0, 0)], edge, 1e-6);
    WL_CHECK_NEAR(gamma[f.index(0, 128)], edge, 1e-6);
    WL_CHECK_NEAR(gamma[f.index(255, 255)], edge, 1e-6);
    WL_CHECK_NEAR(gamma[f.index(128, 128)], s.damping, 1e-6);
    // i=16: d=16*dx、s=(W-d)/W、γ=γ0+spongeGamma*s²
    const double d16 = 16.0 * f.dx();
    const double s16 = (s.spongeWidth - d16) / s.spongeWidth;
    WL_CHECK_NEAR(spongeWeight(f, 16, 128, s.spongeWidth), s16, 1e-5);
    WL_CHECK_NEAR(gamma[f.index(16, 128)], s.damping + s.spongeGamma * s16 * s16, 1e-4);
    float maxGamma = 0.0f;
    for (float g : gamma) maxGamma = std::max(maxGamma, g);
    WL_CHECK_NEAR(maxGamma, edge, 1e-6);

    s.boundary = BoundaryMode::Reflective;
    buildDampingField(f, s, gamma.data());
    WL_CHECK(std::all_of(gamma.begin(), gamma.end(), [&](float g) { return g == s.damping; }));
}

WL_TEST(BoundaryModeParsing) {
    BoundaryMode m;
    WL_CHECK(parseBoundaryMode("reflective", m) && m == BoundaryMode::Reflective);
    WL_CHECK(parseBoundaryMode("Fixed", m) && m == BoundaryMode::Fixed);
    WL_CHECK(parseBoundaryMode("ABSORBING", m) && m == BoundaryMode::Absorbing);
    WL_CHECK(!parseBoundaryMode("open", m));
    WL_CHECK(nextBoundaryMode(BoundaryMode::Fixed) == BoundaryMode::Reflective);
    WL_CHECK(nextBoundaryMode(BoundaryMode::Absorbing) == BoundaryMode::Fixed);
}
