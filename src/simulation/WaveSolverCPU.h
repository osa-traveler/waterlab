#pragma once

#include <cstdint>
#include <vector>

#include "simulation/IWaveSolver.h"
#include "simulation/WaveField.h"

namespace wl {

// tick内のサブステップ計画（仕様書4.2）。
struct SubstepPlan {
    uint32_t substeps;  // S
    double dt;          // tickDt/S
    double dtLimit;     // cflSafety/(c*sqrt(1/dx²+1/dz²))
    double courant;     // C = c*dt*sqrt(1/dx²+1/dz²) <= cflSafety
};

// sqrt(1/dx²+1/dz²)
double courantFactor(const GridDesc& grid);

// 個々の設定値を検査。不正ならWaterLabErrorを投げる。
void validateWaveSettings(const GridDesc& grid, const WaveSettings& settings);

// Sを求める。S>maxSubstepsなら拒否（dtを拡大して対応しない）。
SubstepPlan planSubsteps(const GridDesc& grid, const WaveSettings& settings, double tickDt);

// CPU参照実装。単一スレッド、float32、SoA。
class WaveSolverCPU final : public IWaveSolver {
public:
    WaveSolverCPU(const GridDesc& grid, const WaveSettings& settings);

    void reset() override;
    GridDesc grid() const override { return field_.grid(); }
    void applySettings(const WaveSettings& settings) override;
    void applyImpulse(const Impulse& impulse) override;
    void sampleBatch(const Vec2* xz, SurfaceSample* out, size_t count) const override;
    void advanceSubstep(float dt, const float* acceleration, size_t count) override;
    CpuFieldView cpuView() const override;

    const WaveSettings& settings() const { return settings_; }
    const WaveField& field() const { return field_; }

    // 検証用に状態を直接設定する（境界拘束を適用）。
    void setState(const float* height, const float* velocity);

    // 診断用の離散波動エネルギー（仕様書12.2）。
    // E = 0.5*ΣA*v² - 0.5*c²*ΣA*h*L(h)。Fixedの拘束点は除く。
    double energy() const;

private:
    void rebuildDecay(float dt);

    WaveField field_;
    WaveSettings settings_;
    std::vector<float> laplacian_;
    std::vector<float> gamma_;
    std::vector<float> decay_;  // exp(-γ*dt/2)
    mutable std::vector<float> scratch_;
    float decayDt_ = -1.0f;
    bool laplacianValid_ = false;  // laplacian_が現在のhに対応しているか
    uint64_t revision_ = 0;
};

}  // namespace wl
