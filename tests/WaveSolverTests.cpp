#include <algorithm>
#include <cmath>
#include <vector>

#include "TestHarness.h"
#include "core/Error.h"
#include "simulation/BoundaryConditions.h"
#include "simulation/Disturbance.h"
#include "simulation/SimulationWorld.h"
#include "simulation/WaveSolverCPU.h"

using namespace wl;

namespace {

constexpr double kTickDt = 1.0 / 120.0;
constexpr double kPi = 3.14159265358979323846;

void runSubsteps(WaveSolverCPU& solver, int ticks) {
    const SubstepPlan plan = planSubsteps(solver.grid(), solver.settings(), kTickDt);
    for (int t = 0; t < ticks; ++t) {
        for (uint32_t s = 0; s < plan.substeps; ++s) {
            solver.advanceSubstep(static_cast<float>(plan.dt), nullptr, 0);
        }
    }
}

// 固定端の最低次モード h = A*sin(π(x+2)/4)*sin(π(z+2)/4)
std::vector<float> fixedLowestMode(const WaveField& f, float amplitude) {
    std::vector<float> h(f.count());
    for (uint32_t j = 0; j < f.nz(); ++j) {
        for (uint32_t i = 0; i < f.nx(); ++i) {
            h[f.index(i, j)] = amplitude *
                static_cast<float>(std::sin(kPi * i / (f.nx() - 1)) * std::sin(kPi * j / (f.nz() - 1)));
        }
    }
    return h;
}

}  // namespace

// Phase 0: 初期化後のサイズ・dx・座標が仕様書3.1に一致する。
WL_TEST(GridGeometryMatchesSpec) {
    WaveField f(GridDesc{});
    WL_CHECK(f.nx() == 256 && f.nz() == 256);
    WL_CHECK(f.count() == 65536u);
    // float32の丸め誤差程度（相対1e-7）を許容
    WL_CHECK_NEAR(f.dx(), 4.0 / 255.0, 1e-8);
    WL_CHECK_NEAR(f.dz(), 4.0 / 255.0, 1e-8);
    WL_CHECK_NEAR(f.xAt(0), -2.0, 1e-6);
    WL_CHECK_NEAR(f.xAt(255), 2.0, 1e-6);
    WL_CHECK_NEAR(f.zAt(255), 2.0, 1e-6);
    WL_CHECK(f.index(3, 2) == 2u * 256u + 3u);
    // 台形則面積の総和は領域面積16m²。float総和なので相対1e-5
    double total = 0.0;
    for (size_t k = 0; k < f.count(); ++k) total += f.areas()[k];
    WL_CHECK_NEAR(total, 16.0, 16.0 * 1e-5);
    WL_CHECK_NEAR(f.areas()[f.index(0, 0)], f.cellArea() / 4, 1e-12);
    WL_CHECK_NEAR(f.areas()[f.index(5, 0)], f.cellArea() / 2, 1e-12);
    WL_CHECK_NEAR(f.areas()[f.index(5, 5)], f.cellArea(), 1e-12);
}

WL_TEST(InvalidGridAndSettingsRejected) {
    GridDesc g;
    g.nx = 2;
    WL_CHECK_THROWS(WaveField{g});
    g = GridDesc{};
    g.lengthX = std::nanf("");
    WL_CHECK_THROWS(WaveField{g});
    g = GridDesc{};
    g.lengthZ = -1.0f;
    WL_CHECK_THROWS(WaveField{g});

    WaveSolverCPU solver(GridDesc{}, WaveSettings{});
    const uint64_t revision = solver.cpuView().revision;
    auto expectReject = [&](WaveSettings s) {
        WL_CHECK_THROWS(solver.applySettings(s));
        WL_CHECK(solver.cpuView().revision == revision);
        WL_CHECK(solver.settings().waveSpeed == 2.2f);
    };
    WaveSettings s;
    s.waveSpeed = std::nanf(""); expectReject(s);
    s = {}; s.waveSpeed = 0.0f; expectReject(s);
    s = {}; s.damping = -0.1f; expectReject(s);
    s = {}; s.damping = INFINITY; expectReject(s);
    s = {}; s.cflSafety = 1.0f; expectReject(s);
    s = {}; s.cflSafety = 0.0f; expectReject(s);
    s = {}; s.spongeWidth = 2.0f; expectReject(s);
    s = {}; s.spongeGamma = -1.0f; expectReject(s);
    s = {}; s.maxSubsteps = 0; expectReject(s);
}

// T01: source=0、初期h=v=0で10,000tick後も各要素0。
WL_TEST(T01_StillWaterStaysZero) {
    SimulationWorld world{WorldSettings{}};
    for (int t = 0; t < 10000; ++t) world.stepTick();
    const CpuFieldView v = world.solver().cpuView();
    bool allZero = true;
    for (size_t k = 0; k < size_t(v.nx) * v.nz; ++k) {
        allZero = allZero && v.height[k] == 0.0f && v.velocity[k] == 0.0f;
    }
    WL_CHECK(allZero);
    WL_CHECK(world.diagnostics().tick == 10000u);
}

// T02: 128/256/512、複数cでC<=0.90。既定S=2。上限超過・NaNを拒否。
WL_TEST(T02_CflSubstepPlanning) {
    for (uint32_t n : {128u, 256u, 512u}) {
        for (float c : {0.1f, 1.0f, 2.2f, 3.7f, 5.0f}) {
            GridDesc g;
            g.nx = g.nz = n;
            WaveSettings s;
            s.waveSpeed = c;
            const SubstepPlan p = planSubsteps(g, s, kTickDt);
            WL_CHECK(p.courant <= 0.90 + 1e-9);
            WL_CHECK(p.substeps >= 1);
            WL_CHECK_NEAR(p.dt * p.substeps, kTickDt, 1e-15);
            // 1つ少ないSではCFLを満たさない（最小のS）
            if (p.substeps > 1) {
                WL_CHECK(c * (kTickDt / (p.substeps - 1)) * courantFactor(g) > 0.90);
            }
        }
    }
    const SubstepPlan def = planSubsteps(GridDesc{}, WaveSettings{}, kTickDt);
    WL_CHECK(def.substeps == 2);
    WL_CHECK_NEAR(def.dt, 1.0 / 240.0, 1e-15);
    WL_CHECK_NEAR(def.dtLimit, 0.00454, 1e-5);
    WL_CHECK_NEAR(def.courant, 0.826, 1e-3);
    GridDesc g512;
    g512.nx = g512.nz = 512;
    WL_CHECK(planSubsteps(g512, WaveSettings{}, kTickDt).substeps == 4);

    WaveSettings tooFew;
    tooFew.maxSubsteps = 1;
    WL_CHECK_THROWS(planSubsteps(GridDesc{}, tooFew, kTickDt));
    WaveSettings nanC;
    nanC.waveSpeed = std::nanf("");
    WL_CHECK_THROWS(planSubsteps(GridDesc{}, nanC, kTickDt));

    WaveSolverCPU solver(GridDesc{}, WaveSettings{});
    WL_CHECK_THROWS(solver.advanceSubstep(static_cast<float>(kTickDt), nullptr, 0));
    WL_CHECK_THROWS(solver.advanceSubstep(-1.0f, nullptr, 0));
    std::vector<float> wrongSize(10, 0.0f);
    WL_CHECK_THROWS(solver.advanceSubstep(1.0f / 240.0f, wrongSize.data(), wrongSize.size()));
}

// T03: 定数場・線形高さ場の内部で誤差1e-5m以下。最右端・角・領域外も確認。
WL_TEST(T03_Interpolation) {
    WaveSolverCPU solver(GridDesc{}, WaveSettings{});
    const WaveField& f = solver.field();
    const float a = 0.01f, b = -0.02f, c0 = 0.03f;  // h = a*x + b*z + c0 [m]
    std::vector<float> h(f.count()), v(f.count());
    for (uint32_t j = 0; j < f.nz(); ++j) {
        for (uint32_t i = 0; i < f.nx(); ++i) {
            h[f.index(i, j)] = a * f.xAt(i) + b * f.zAt(j) + c0;
            v[f.index(i, j)] = 0.5f;
        }
    }
    solver.setState(h.data(), v.data());

    const std::vector<Vec2> pts = {{0.123f, -0.456f}, {-1.99f, 1.37f}, {2.0f, 2.0f},
                                   {-2.0f, -2.0f},   {2.0f, -2.0f},  {2.0f, 0.3f}};
    std::vector<SurfaceSample> out(pts.size());
    solver.sampleBatch(pts.data(), out.data(), pts.size());
    for (size_t k = 0; k < pts.size(); ++k) {
        WL_CHECK(out[k].inside);
        WL_CHECK_NEAR(out[k].height, a * pts[k].x + b * pts[k].z + c0, 1e-5);
        WL_CHECK_NEAR(out[k].verticalVelocity, 0.5, 1e-6);
        const Vec3 n = normalize({-a, 1.0f, -b});
        WL_CHECK_NEAR(out[k].normal.x, n.x, 1e-4);
        WL_CHECK_NEAR(out[k].normal.y, n.y, 1e-4);
        WL_CHECK_NEAR(out[k].normal.z, n.z, 1e-4);
    }

    const std::vector<Vec2> outside = {{2.01f, 0.0f}, {0.0f, -2.5f}, {NAN, 0.0f}, {0.0f, INFINITY}};
    std::vector<SurfaceSample> o(outside.size());
    solver.sampleBatch(outside.data(), o.data(), outside.size());
    for (const SurfaceSample& s : o) {
        WL_CHECK(!s.inside);
        WL_CHECK(s.height == 0.0f && s.verticalVelocity == 0.0f);
        WL_CHECK(s.normal.x == 0.0f && s.normal.y == 1.0f && s.normal.z == 0.0f);
    }
}

// T04: 1回のv変化が強度に一致し、dtやSで二重注入しない。
WL_TEST(T04_ImpulseMatchesStrength) {
    WaveSolverCPU solver(GridDesc{}, WaveSettings{});
    const WaveField& f = solver.field();
    Impulse imp;
    imp.center = {f.xAt(128), f.zAt(100)};  // 格子点上
    for (float peak : {0.25f, -0.25f, 0.7f}) {
        solver.reset();
        imp.deltaVelocityPeak = peak;
        solver.applyImpulse(imp);
        float maxAbs = 0.0f;
        for (size_t k = 0; k < f.count(); ++k) maxAbs = std::max(maxAbs, std::abs(f.velocity()[k]));
        // float32の丸め（相対1e-6）
        WL_CHECK_NEAR(maxAbs, std::abs(peak), std::abs(peak) * 1e-6);
        WL_CHECK_NEAR(f.velocity()[f.index(128, 100)], peak, std::abs(peak) * 1e-6);
        // 高さは即時には変わらない（速度インパルス）
        WL_CHECK(f.height()[f.index(128, 100)] == 0.0f);
    }
    imp.sigma = 0.5f * minimumImpulseSigma(f);
    WL_CHECK_THROWS(solver.applyImpulse(imp));
    imp.sigma = 0.06f;
    imp.center = {2.5f, 0.0f};
    WL_CHECK_THROWS(solver.applyImpulse(imp));
}

// T05: 補償カーネルの面積加重積分の絶対値がΣA|K|の1e-5以下。端・角・Fixedでも確認。
WL_TEST(T05_ZeroMeanKernel) {
    for (BoundaryMode mode : {BoundaryMode::Reflective, BoundaryMode::Fixed}) {
        for (Vec2 c : {Vec2{0.0f, 0.0f}, Vec2{1.95f, 0.1f}, Vec2{-1.97f, 1.96f}, Vec2{2.0f, -2.0f}}) {
            WaveField f(GridDesc{});
            Impulse imp;
            imp.center = c;
            const ImpulseStatus st = applyImpulseKernel(f, mode, imp);
            WL_CHECK(st == ImpulseStatus::Applied);
            double sum = 0.0, sumAbs = 0.0;
            for (size_t k = 0; k < f.count(); ++k) {
                sum += double(f.areas()[k]) * f.velocity()[k];
                sumAbs += double(f.areas()[k]) * std::abs(f.velocity()[k]);
            }
            WL_CHECK(sumAbs > 0.0);
            WL_CHECK(std::abs(sum) <= 1e-5 * sumAbs);
            if (mode == BoundaryMode::Fixed) {
                bool edgeZero = true;
                for (uint32_t i = 0; i < f.nx(); ++i) {
                    edgeZero = edgeZero && f.velocity()[f.index(i, 0)] == 0.0f &&
                               f.velocity()[f.index(i, f.nz() - 1)] == 0.0f;
                }
                WL_CHECK(edgeZero);
            }
        }
    }
    // 単純ガウスモードはゼロ平均ではない（比較用）
    WaveField f(GridDesc{});
    Impulse gauss;
    gauss.zeroMean = false;
    WL_CHECK(applyImpulseKernel(f, BoundaryMode::Reflective, gauss) == ImpulseStatus::Applied);
    double sum = 0.0;
    for (size_t k = 0; k < f.count(); ++k) sum += double(f.areas()[k]) * f.velocity()[k];
    WL_CHECK(sum > 0.0);
}

// ラプラシアンのキャッシュ再利用が、毎回再計算した場合とビット一致する。
WL_TEST(LaplacianCacheMatchesRecompute) {
    for (BoundaryMode mode : {BoundaryMode::Fixed, BoundaryMode::Reflective, BoundaryMode::Absorbing}) {
        WaveSettings s;
        s.boundary = mode;
        WaveSolverCPU cached(GridDesc{}, s), fresh(GridDesc{}, s);
        Impulse imp;
        imp.center = {0.3f, -0.2f};
        cached.applyImpulse(imp);
        fresh.applyImpulse(imp);
        const size_t n = cached.field().count();
        for (int step = 0; step < 50; ++step) {
            cached.advanceSubstep(1.0f / 240.0f, nullptr, 0);
            // setStateはキャッシュを無効化する
            const std::vector<float> h(fresh.field().height(), fresh.field().height() + n);
            const std::vector<float> v(fresh.field().velocity(), fresh.field().velocity() + n);
            fresh.setState(h.data(), v.data());
            fresh.advanceSubstep(1.0f / 240.0f, nullptr, 0);
        }
        WL_CHECK(std::equal(cached.field().height(), cached.field().height() + n, fresh.field().height()));
        WL_CHECK(std::equal(cached.field().velocity(), cached.field().velocity() + n, fresh.field().velocity()));
    }
}

// 12.2: 固定端の低周波モード、無減衰30sでEの相対変動幅1%以下。
WL_TEST(EnergyConservedForFixedLowMode) {
    WaveSettings s;
    s.boundary = BoundaryMode::Fixed;
    s.damping = 0.0f;
    WaveSolverCPU solver(GridDesc{}, s);
    const std::vector<float> h = fixedLowestMode(solver.field(), 0.01f);
    const std::vector<float> v(h.size(), 0.0f);
    solver.setState(h.data(), v.data());
    const double e0 = solver.energy();
    WL_CHECK(e0 > 0.0);
    double eMin = e0, eMax = e0;
    for (int t = 0; t < 30 * 120; ++t) {
        runSubsteps(solver, 1);
        const double e = solver.energy();
        eMin = std::min(eMin, e);
        eMax = std::max(eMax, e);
    }
    WL_CHECK((eMax - eMin) / e0 <= 0.01);
}

// 12.2: 減衰ありで1秒移動平均Eが長期的に減少し、30s後に初期の10%未満。
WL_TEST(EnergyDecaysWithDamping) {
    WaveSettings s;
    s.boundary = BoundaryMode::Fixed;
    WaveSolverCPU solver(GridDesc{}, s);
    const std::vector<float> h = fixedLowestMode(solver.field(), 0.01f);
    const std::vector<float> v(h.size(), 0.0f);
    solver.setState(h.data(), v.data());
    const double e0 = solver.energy();
    double prevAvg = e0;
    for (int sec = 0; sec < 30; ++sec) {
        double sum = 0.0;
        for (int t = 0; t < 120; ++t) {
            runSubsteps(solver, 1);
            sum += solver.energy();
        }
        const double avg = sum / 120.0;
        WL_CHECK(avg < prevAvg);
        prevAvg = avg;
    }
    WL_CHECK(solver.energy() < 0.1 * e0);
}

// Phase 1: 中心入力からほぼ同心円状に伝播する（境界到達前の8方向対称性）。
WL_TEST(RippleSpreadsSymmetrically) {
    WaveSolverCPU solver(GridDesc{}, WaveSettings{});
    const WaveField& f = solver.field();
    const uint32_t ci = 128, cj = 128;
    Impulse imp;
    imp.center = {f.xAt(ci), f.zAt(cj)};
    solver.applyImpulse(imp);
    runSubsteps(solver, 48);  // 0.4s、波面は約0.9m（境界まで約2m）

    float maxAbs = 0.0f;
    for (size_t k = 0; k < f.count(); ++k) maxAbs = std::max(maxAbs, std::abs(f.height()[k]));
    WL_CHECK(maxAbs > 1e-5f);
    float worst = 0.0f;
    const auto at = [&](int di, int dj) { return f.height()[f.index(ci + di, cj + dj)]; };
    for (int a = 0; a <= 100; ++a) {
        for (int b = 0; b <= a; ++b) {
            const float ref = at(a, b);
            for (float other : {at(b, a), at(-a, b), at(a, -b), at(-a, -b), at(-b, -a)}) {
                worst = std::max(worst, std::abs(other - ref));
            }
        }
    }
    // 演算順序の違いによるfloat誤差のみ許容（振幅の1e-4）
    WL_CHECK(worst <= 1e-4f * maxAbs);
    // 波面が外へ進んでいる: 0.4s後、中心から0.7～1.1mの環に最大振幅がある
    float ringMax = 0.0f, innerMax = 0.0f;
    for (int a = 0; a <= 100; ++a) {
        const float r = a * f.dx();
        if (r > 0.7f && r < 1.1f) ringMax = std::max(ringMax, std::abs(at(a, 0)));
        if (r < 0.3f) innerMax = std::max(innerMax, std::abs(at(a, 0)));
    }
    WL_CHECK(ringMax > innerMax);
}
