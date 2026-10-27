#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

// 依存なしの小さなassertハーネス。
namespace wltest {

struct TestCase {
    const char* name;
    void (*fn)();
};

std::vector<TestCase>& registry();
void reportFailure(const char* file, int line, const std::string& message);

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

}  // namespace wltest

#define WL_TEST(name)                                                  \
    static void name();                                                \
    static ::wltest::Registrar name##_registrar(#name, &name);         \
    static void name()

#define WL_CHECK(cond)                                                          \
    do {                                                                        \
        if (!(cond)) ::wltest::reportFailure(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (0)

// 絶対誤差で比較する。tolには単位と根拠をテスト側で記す。
#define WL_CHECK_NEAR(actual, expected, tol)                                         \
    do {                                                                             \
        const double wl_a = static_cast<double>(actual);                             \
        const double wl_e = static_cast<double>(expected);                           \
        if (!(std::fabs(wl_a - wl_e) <= static_cast<double>(tol))) {                 \
            char wl_buf[256];                                                        \
            std::snprintf(wl_buf, sizeof(wl_buf), "NEAR(%s, %s): %.9g vs %.9g (tol %.3g)", \
                          #actual, #expected, wl_a, wl_e, static_cast<double>(tol)); \
            ::wltest::reportFailure(__FILE__, __LINE__, wl_buf);                     \
        }                                                                            \
    } while (0)

#define WL_CHECK_THROWS(expr)                                                     \
    do {                                                                          \
        bool wl_threw = false;                                                    \
        try { expr; } catch (const std::exception&) { wl_threw = true; }          \
        if (!wl_threw) ::wltest::reportFailure(__FILE__, __LINE__, "THROWS(" #expr ")"); \
    } while (0)
