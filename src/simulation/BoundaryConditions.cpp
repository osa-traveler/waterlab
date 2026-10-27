#include "simulation/BoundaryConditions.h"

#include <algorithm>
#include <cctype>

namespace wl {

const char* toString(BoundaryMode mode) {
    switch (mode) {
        case BoundaryMode::Fixed: return "Fixed";
        case BoundaryMode::Reflective: return "Reflective";
        case BoundaryMode::Absorbing: return "Absorbing";
    }
    return "Unknown";
}

bool parseBoundaryMode(const std::string& text, BoundaryMode& out) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "fixed") { out = BoundaryMode::Fixed; return true; }
    if (lower == "reflective") { out = BoundaryMode::Reflective; return true; }
    if (lower == "absorbing") { out = BoundaryMode::Absorbing; return true; }
    return false;
}

BoundaryMode nextBoundaryMode(BoundaryMode mode) {
    switch (mode) {
        case BoundaryMode::Fixed: return BoundaryMode::Reflective;
        case BoundaryMode::Reflective: return BoundaryMode::Absorbing;
        case BoundaryMode::Absorbing: return BoundaryMode::Fixed;
    }
    return BoundaryMode::Reflective;
}

void computeLaplacian(const WaveField& field, BoundaryMode mode, const float* h, float* out) {
    const uint32_t nx = field.nx();
    const uint32_t nz = field.nz();
    const float idx2 = 1.0f / (field.dx() * field.dx());
    const float idz2 = 1.0f / (field.dz() * field.dz());

    for (uint32_t j = 0; j < nz; ++j) {
        const uint32_t jm = (j == 0) ? 1 : j - 1;
        const uint32_t jp = (j == nz - 1) ? nz - 2 : j + 1;
        const float* c = h + field.index(0, j);
        const float* m = h + field.index(0, jm);
        const float* p = h + field.index(0, jp);
        float* o = out + field.index(0, j);

        o[0] = 2.0f * (c[1] - c[0]) * idx2 + (m[0] - 2.0f * c[0] + p[0]) * idz2;
        for (uint32_t i = 1; i + 1 < nx; ++i) {
            o[i] = (c[i + 1] - 2.0f * c[i] + c[i - 1]) * idx2 +
                   (p[i] - 2.0f * c[i] + m[i]) * idz2;
        }
        const uint32_t last = nx - 1;
        o[last] = 2.0f * (c[last - 1] - c[last]) * idx2 +
                  (m[last] - 2.0f * c[last] + p[last]) * idz2;
    }

    if (mode == BoundaryMode::Fixed) {
        for (uint32_t i = 0; i < nx; ++i) {
            out[field.index(i, 0)] = 0.0f;
            out[field.index(i, nz - 1)] = 0.0f;
        }
        for (uint32_t j = 0; j < nz; ++j) {
            out[field.index(0, j)] = 0.0f;
            out[field.index(nx - 1, j)] = 0.0f;
        }
    }
}

void applyBoundaryConstraint(BoundaryMode mode, WaveField& field) {
    if (mode != BoundaryMode::Fixed) return;
    const uint32_t nx = field.nx();
    const uint32_t nz = field.nz();
    float* h = field.height();
    float* v = field.velocity();
    for (uint32_t i = 0; i < nx; ++i) {
        const size_t a = field.index(i, 0), b = field.index(i, nz - 1);
        h[a] = v[a] = h[b] = v[b] = 0.0f;
    }
    for (uint32_t j = 0; j < nz; ++j) {
        const size_t a = field.index(0, j), b = field.index(nx - 1, j);
        h[a] = v[a] = h[b] = v[b] = 0.0f;
    }
}

float spongeWeight(const WaveField& field, uint32_t i, uint32_t j, float spongeWidth) {
    if (spongeWidth <= 0.0f) return 0.0f;
    const float halfX = 0.5f * field.grid().lengthX;
    const float halfZ = 0.5f * field.grid().lengthZ;
    const float x = field.xAt(i);
    const float z = field.zAt(j);
    const float d = std::max(0.0f, std::min({x + halfX, halfX - x, z + halfZ, halfZ - z}));
    return clampf((spongeWidth - d) / spongeWidth, 0.0f, 1.0f);
}

void buildDampingField(const WaveField& field, const WaveSettings& settings, float* gamma) {
    const bool sponge = settings.boundary == BoundaryMode::Absorbing;
    for (uint32_t j = 0; j < field.nz(); ++j) {
        for (uint32_t i = 0; i < field.nx(); ++i) {
            float g = settings.damping;
            if (sponge) {
                const float s = spongeWeight(field, i, j, settings.spongeWidth);
                g += settings.spongeGamma * s * s;
            }
            gamma[field.index(i, j)] = g;
        }
    }
}

}  // namespace wl
