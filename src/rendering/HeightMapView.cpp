#include "rendering/HeightMapView.h"

#include <algorithm>
#include <cmath>

namespace wl {

HeightMapView::HeightMapView(uint32_t nx, uint32_t nz)
    : pixels_(static_cast<size_t>(nx) * nz, WHITE) {
    Image image = GenImageColor(static_cast<int>(nx), static_cast<int>(nz), WHITE);
    texture_ = UniqueTexture(LoadTextureFromImage(image));
    UnloadImage(image);
    SetTextureFilter(texture_.get(), TEXTURE_FILTER_BILINEAR);
}

Color HeightMapView::heightColor(float h, float range) {
    const float t = std::isfinite(h) ? std::clamp(h / range, -1.0f, 1.0f) : 0.0f;
    const auto mix = [](unsigned char a, unsigned char b, float s) {
        return static_cast<unsigned char>(std::lround(a + (b - a) * s));
    };
    const Color white{245, 245, 245, 255};
    const Color blue{30, 70, 200, 255};
    const Color red{205, 40, 35, 255};
    const Color& end = t < 0.0f ? blue : red;
    const float s = std::abs(t);
    return {mix(white.r, end.r, s), mix(white.g, end.g, s), mix(white.b, end.b, s), 255};
}

void HeightMapView::update(const float* height, float range) {
    for (size_t k = 0; k < pixels_.size(); ++k) pixels_[k] = heightColor(height[k], range);
    UpdateTexture(texture_.get(), pixels_.data());
}

void HeightMapView::draw(Rectangle dest) const {
    const Texture2D& tex = texture_.get();
    DrawTexturePro(tex, {0.0f, 0.0f, float(tex.width), float(tex.height)}, dest, {0.0f, 0.0f}, 0.0f, WHITE);
}

}  // namespace wl
