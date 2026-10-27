#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

#include "TestHarness.h"
#include "simulation/SimulationWorld.h"

using namespace wl;

namespace {

std::vector<float> copyHeight(const SimulationWorld& w) {
    const CpuFieldView v = w.solver().cpuView();
    return std::vector<float>(v.height, v.height + size_t(v.nx) * v.nz);
}

std::vector<float> copyVelocity(const SimulationWorld& w) {
    const CpuFieldView v = w.solver().cpuView();
    return std::vector<float>(v.velocity, v.velocity + size_t(v.nx) * v.nz);
}

}  // namespace

// T04（World経由）: キューのインパルスは次のtickで1回だけ、サブステップ前に反映される。
WL_TEST(T04_WorldAppliesImpulseOnce) {
    for (float c : {2.2f, 5.0f}) {  // S=2とS=5
        WorldSettings ws;
        ws.wave.waveSpeed = c;
        SimulationWorld world(ws);
        WaveSolverCPU manual(ws.grid, ws.wave);
        const SubstepPlan plan = planSubsteps(ws.grid, ws.wave, ws.tickDt);

        Impulse imp;
        imp.center = {0.2f, -0.3f};
        WL_CHECK(world.queueImpulse(imp));
        for (int t = 0; t < 3; ++t) world.stepTick();

        manual.applyImpulse(imp);
        for (int t = 0; t < 3; ++t)
            for (uint32_t s = 0; s < plan.substeps; ++s) manual.advanceSubstep(float(plan.dt), nullptr, 0);

        const size_t n = manual.field().count();
        const std::vector<float> h = copyHeight(world);
        WL_CHECK(std::equal(h.begin(), h.end(), manual.field().height()));
        WL_CHECK(world.diagnostics().appliedImpulses == 1u);
        WL_CHECK(world.diagnostics().substeps == plan.substeps);
        (void)n;
    }
}

// T08: 追いつき上限に達しない模擬30/60/144FPS列で共通tickのh/vが一致。
WL_TEST(T08_FrameRateIndependence) {
    const std::vector<uint64_t> checkpoints = {120, 300, 480};
    auto run = [&](double fps) {
        std::map<uint64_t, std::pair<std::vector<float>, std::vector<float>>> snaps;
        SimulationWorld world{WorldSettings{}};
        Impulse imp;
        imp.center = {-0.4f, 0.25f};
        world.queueImpulse(imp);
        world.setTickObserver([&](uint64_t tick, const SimulationWorld& w) {
            if (std::find(checkpoints.begin(), checkpoints.end(), tick) != checkpoints.end()) {
                snaps[tick] = {copyHeight(w), copyVelocity(w)};
            }
        });
        while (world.diagnostics().tick < checkpoints.back()) {
            world.advanceFrame(1.0 / fps);
            WL_CHECK(world.diagnostics().droppedTickEvents == 0u);
        }
        return snaps;
    };
    const auto a = run(30.0), b = run(60.0), c = run(144.0);
    for (uint64_t t : checkpoints) {
        WL_CHECK(a.count(t) && b.count(t) && c.count(t));
        WL_CHECK(a.at(t) == b.at(t));
        WL_CHECK(a.at(t) == c.at(t));
    }
}

// 4.4: frameDtは0.1sで打ち切り、1フレーム最大8tick。超過分は捨てて記録する。
WL_TEST(FrameCatchUpIsBounded) {
    SimulationWorld world{WorldSettings{}};
    world.advanceFrame(5.0);
    const WorldDiagnostics& d = world.diagnostics();
    WL_CHECK(d.ticksLastFrame == 8u);
    WL_CHECK(d.tick == 8u);
    WL_CHECK_NEAR(d.clampedSeconds, 4.9, 1e-9);
    WL_CHECK(d.droppedTickEvents == 1u);
    // 0.1 - 8/120 = 1/30s のうちtick単位の分を捨てる
    WL_CHECK(d.droppedTickSeconds > 0.025 && d.droppedTickSeconds <= 1.0 / 30.0 + 1e-9);
    WL_CHECK(world.renderAlpha() < 1.0f);
    world.advanceFrame(std::nan(""));
    WL_CHECK(world.diagnostics().ticksLastFrame == 0u);
}

// 停止中はaccumulatorを増やさず、Nで1tick、停止中の入力は次のtickで消費。
WL_TEST(PauseAndSingleStep) {
    SimulationWorld world{WorldSettings{}};
    world.setPaused(true);
    world.advanceFrame(0.05);
    WL_CHECK(world.diagnostics().tick == 0u);
    Impulse imp;
    world.queueImpulse(imp);
    world.stepTick();
    WL_CHECK(world.diagnostics().tick == 1u);
    WL_CHECK(world.diagnostics().appliedImpulses == 1u);
    WL_CHECK(world.renderAlpha() == 1.0f);
    world.setPaused(false);
    world.advanceFrame(1.0 / 120.0 + 1e-6);
    WL_CHECK(world.diagnostics().tick == 2u);
    WL_CHECK(world.diagnostics().appliedImpulses == 1u);
}

// 設定変更はtick境界で適用し、Sを再計算する。不正な要求は理由付きで拒否し状態を保つ。
WL_TEST(SettingsAppliedAtTickBoundary) {
    SimulationWorld world{WorldSettings{}};
    WaveSettings fast;
    fast.waveSpeed = 5.0f;
    world.requestWaveSettings(fast);
    WL_CHECK(world.effectiveWaveSettings().waveSpeed == 5.0f);
    WL_CHECK(world.diagnostics().substeps == 2u);
    world.stepTick();
    WL_CHECK(world.diagnostics().substeps == 5u);
    WL_CHECK(world.diagnostics().courant <= 0.9);

    WaveSettings bad = fast;
    bad.maxSubsteps = 2;
    WL_CHECK_THROWS(world.requestWaveSettings(bad));
    WL_CHECK(world.effectiveWaveSettings().maxSubsteps == 16u);

    // Fixedへの切替で境界h/vを0にする
    Impulse imp;
    imp.center = {1.9f, 0.0f};
    world.queueImpulse(imp);
    world.stepTick();
    WaveSettings fixed = fast;
    fixed.boundary = BoundaryMode::Fixed;
    world.requestWaveSettings(fixed);
    world.stepTick();
    const CpuFieldView v = world.solver().cpuView();
    bool edgeZero = true;
    for (uint32_t j = 0; j < v.nz; ++j) {
        const size_t k = size_t(j) * v.nx + (v.nx - 1);
        edgeZero = edgeZero && v.height[k] == 0.0f && v.velocity[k] == 0.0f;
    }
    WL_CHECK(edgeZero);
}

// T15: 異常高さ・非有限値で停止し、リセットで全状態とキューが初期化される。
WL_TEST(T15_AnomalyHaltsAndResetRecovers) {
    SimulationWorld world{WorldSettings{}};
    Impulse huge;
    huge.deltaVelocityPeak = 20.0f;  // max|v| > 10 m/s
    world.queueImpulse(huge);
    world.stepTick();
    WL_CHECK(world.diagnostics().halted);
    WL_CHECK(world.diagnostics().haltReason.find("max|v|") != std::string::npos);
    const uint64_t haltedTick = world.diagnostics().tick;
    world.advanceFrame(0.05);
    world.stepTick();
    WL_CHECK(world.diagnostics().tick == haltedTick);

    world.queueImpulse(Impulse{});
    world.reset();
    WL_CHECK(!world.diagnostics().halted);
    WL_CHECK(world.diagnostics().tick == 0u);
    world.stepTick();  // キューは空なので静水のまま
    const std::vector<float> h = copyHeight(world);
    WL_CHECK(std::all_of(h.begin(), h.end(), [](float x) { return x == 0.0f; }));
    WL_CHECK(world.diagnostics().appliedImpulses == 0u);

    // NaN注入
    auto& cpu = dynamic_cast<WaveSolverCPU&>(world.solver());
    std::vector<float> nanH(h.size(), 0.0f), zeros(h.size(), 0.0f);
    nanH[1234] = std::nanf("");
    cpu.setState(nanH.data(), zeros.data());
    world.stepTick();
    WL_CHECK(world.diagnostics().halted);
    WL_CHECK(world.diagnostics().haltReason.find("NaN") != std::string::npos);
    world.reset();
    WL_CHECK(!world.diagnostics().halted);
}

// 拒否されたインパルスは診断に記録され、場を変更しない。
WL_TEST(RejectedImpulseIsCounted) {
    SimulationWorld world{WorldSettings{}};
    Impulse tiny;
    tiny.sigma = 0.001f;
    world.queueImpulse(tiny);
    world.stepTick();
    WL_CHECK(world.diagnostics().rejectedImpulses == 1u);
    WL_CHECK(!world.diagnostics().lastRejectReason.empty());
    const std::vector<float> h = copyHeight(world);
    WL_CHECK(std::all_of(h.begin(), h.end(), [](float x) { return x == 0.0f; }));
}

WL_TEST(EventQueueCapacity) {
    WorldSettings ws;
    ws.eventCapacity = 4;
    SimulationWorld world(ws);
    for (int k = 0; k < 4; ++k) WL_CHECK(world.queueImpulse(Impulse{}));
    WL_CHECK(!world.queueImpulse(Impulse{}));
    WL_CHECK(world.diagnostics().queueOverflows == 1u);
}

// 描画補間はrenderPreviousとrenderCurrentの間にある。
WL_TEST(RenderInterpolation) {
    SimulationWorld world{WorldSettings{}};
    world.queueImpulse(Impulse{});
    world.advanceFrame(3.5 / 120.0);
    const float alpha = world.renderAlpha();
    WL_CHECK_NEAR(alpha, 0.5, 1e-6);
    std::vector<float> out(world.renderCurrent().size());
    world.interpolatedHeight(out.data());
    const std::vector<float> h = copyHeight(world);
    WL_CHECK(h == world.renderCurrent());
}
