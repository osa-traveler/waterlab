#pragma once

#include <algorithm>
#include <cmath>

// raylib非依存の小さな数学型。描画境界でraylib型へ変換する。
namespace wl {

struct Vec2 {
    float x, z;
};

struct Vec3 {
    float x, y, z;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.z + b.z}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.z - b.z}; }
inline Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.z * s}; }
inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.z * b.z; }
inline float length(Vec2 a) { return std::sqrt(dot(a, a)); }

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }

inline Vec3 normalize(Vec3 a) {
    const float len = length(a);
    return len > 0.0f ? a * (1.0f / len) : Vec3{0.0f, 1.0f, 0.0f};
}

inline float clampf(float v, float lo, float hi) { return std::min(std::max(v, lo), hi); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

inline bool isFinite(Vec2 a) { return std::isfinite(a.x) && std::isfinite(a.z); }

}  // namespace wl
