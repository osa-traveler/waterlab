#pragma once

#include <utility>

#include "raylib.h"

// raylib資源の一意な所有者（RAII）。コピー禁止・ムーブのみで二重解放を防ぐ。
namespace wl {

class UniqueTexture {
public:
    UniqueTexture() = default;
    explicit UniqueTexture(Texture2D texture) : texture_(texture) {}
    ~UniqueTexture() { release(); }

    UniqueTexture(const UniqueTexture&) = delete;
    UniqueTexture& operator=(const UniqueTexture&) = delete;
    UniqueTexture(UniqueTexture&& other) noexcept : texture_(std::exchange(other.texture_, Texture2D{})) {}
    UniqueTexture& operator=(UniqueTexture&& other) noexcept {
        if (this != &other) {
            release();
            texture_ = std::exchange(other.texture_, Texture2D{});
        }
        return *this;
    }

    const Texture2D& get() const { return texture_; }
    bool valid() const { return texture_.id != 0; }

private:
    void release() {
        if (texture_.id != 0) UnloadTexture(texture_);
        texture_ = Texture2D{};
    }

    Texture2D texture_{};
};

}  // namespace wl
