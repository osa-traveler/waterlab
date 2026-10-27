#pragma once

#include <stdexcept>

namespace wl {

// 設定・入力の拒否に使う統一例外。呼出し側の状態は変更前のまま保たれる。
// app境界で捕捉してログに残す。
class WaterLabError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

}  // namespace wl
