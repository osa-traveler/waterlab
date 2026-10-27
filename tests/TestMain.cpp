#include <chrono>
#include <cstdio>
#include <cstring>

#include "TestHarness.h"

namespace wltest {

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

static int g_failures = 0;

void reportFailure(const char* file, int line, const std::string& message) {
    ++g_failures;
    std::fprintf(stderr, "  FAIL %s:%d: %s\n", file, line, message.c_str());
}

}  // namespace wltest

// 使い方: waterlab_tests [名前の部分一致フィルタ]
int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, failedTests = 0;
    for (const auto& t : wltest::registry()) {
        if (filter && std::strstr(t.name, filter) == nullptr) continue;
        const int before = wltest::g_failures;
        const auto start = std::chrono::steady_clock::now();
        try {
            t.fn();
        } catch (const std::exception& e) {
            wltest::reportFailure(__FILE__, __LINE__, std::string("unexpected exception: ") + e.what());
        }
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        const bool ok = wltest::g_failures == before;
        if (!ok) ++failedTests;
        std::printf("[%s] %s (%.0f ms)\n", ok ? " OK " : "FAIL", t.name, ms);
        ++run;
    }
    std::printf("%d tests, %d failed\n", run, failedTests);
    return failedTests == 0 && run > 0 ? 0 : 1;
}
