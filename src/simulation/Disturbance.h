#pragma once

#include "simulation/IWaveSolver.h"
#include "simulation/WaveField.h"

namespace wl {

enum class ImpulseStatus { Applied, InvalidParameters, OutsideDomain, InsufficientSamples };

const char* toString(ImpulseStatus status);

// sigmaの下限 2*max(dx,dz)。
float minimumImpulseSigma(const WaveField& field);

// 3σ以内に必要な有効標本数。これ未満のイベントは拒否する。
constexpr int kMinImpulseSamples = 9;

// v += deltaVelocityPeak*K を1回だけ加える。dtは掛けない。
// zeroMean: K = (g1 - b*g2)/max|g1 - b*g2|、b = ΣA*g1/ΣA*g2（g1:σ・3σ打切り、g2:2σ・6σ打切り）。
// 単純ガウス: K = g1/max(g1)。平均水位はドリフトし得る。
// Fixedの拘束点は正規化・注入の対象外。端で切れたカーネルは領域内で再正規化する。
// 拒否時はfieldを変更しない。
ImpulseStatus applyImpulseKernel(WaveField& field, BoundaryMode mode, const Impulse& impulse);

}  // namespace wl
