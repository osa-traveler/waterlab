// ヘッドレス物理ベンチマーク。描画FPSは報告しない。
// 例: ./build/waterlab_bench --grid 256 --ticks 14400 --warmup 1200 --csv bench.csv
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/Error.h"
#include "simulation/SimulationWorld.h"

using namespace wl;

namespace {

struct Options {
    uint32_t grid = 256;
    uint32_t spheres = 0;
    uint64_t ticks = 14400;
    uint64_t warmup = 1200;
    std::string csv;
};

[[noreturn]] void usage(const char* msg) {
    std::fprintf(stderr, "error: %s\nusage: waterlab_bench [--grid N] [--spheres N] [--ticks N] "
                         "[--warmup N] [--csv FILE]\n", msg);
    std::exit(2);
}

Options parse(int argc, char** argv) {
    Options o;
    for (int k = 1; k < argc; ++k) {
        const std::string a = argv[k];
        auto next = [&]() -> const char* {
            if (k + 1 >= argc) usage(("missing value for " + a).c_str());
            return argv[++k];
        };
        if (a == "--grid") o.grid = uint32_t(std::strtoul(next(), nullptr, 10));
        else if (a == "--spheres") o.spheres = uint32_t(std::strtoul(next(), nullptr, 10));
        else if (a == "--ticks") o.ticks = std::strtoull(next(), nullptr, 10);
        else if (a == "--warmup") o.warmup = std::strtoull(next(), nullptr, 10);
        else if (a == "--csv") o.csv = next();
        else usage(("unknown option " + a).c_str());
    }
    if (o.grid != 128 && o.grid != 256 && o.grid != 512) usage("--grid must be 128, 256 or 512");
    if (o.ticks == 0) usage("--ticks must be > 0");
    // 球体はPhase 4で実装する。未実装の機能を計測したように見せない。
    if (o.spheres != 0) usage("--spheres is not implemented yet (Phase 4)");
    return o;
}

double percentile(std::vector<double> v, double p) {
    std::sort(v.begin(), v.end());
    const size_t idx = std::min(v.size() - 1, size_t(p * double(v.size() - 1) + 0.5));
    return v[idx];
}

}  // namespace

int main(int argc, char** argv) {
    const Options opt = parse(argc, argv);
    try {
        WorldSettings ws;
        ws.grid.nx = ws.grid.nz = opt.grid;
        SimulationWorld world(ws);

        // 決定的な入力列: 0.5秒ごとに位置を変えたインパルス
        auto feed = [&](uint64_t tick) {
            if (tick % 60 == 0) {
                Impulse imp;
                const float t = float(tick / 60);
                imp.center = {1.2f * std::sin(t * 0.7f), 1.2f * std::cos(t * 1.3f)};
                imp.deltaVelocityPeak = (tick / 60) % 2 ? -0.25f : 0.25f;
                world.queueImpulse(imp);
            }
        };

        for (uint64_t t = 0; t < opt.warmup; ++t) {
            feed(t);
            world.stepTick();
        }
        std::vector<double> ms;
        ms.reserve(opt.ticks);
        for (uint64_t t = 0; t < opt.ticks; ++t) {
            feed(opt.warmup + t);
            const auto start = std::chrono::steady_clock::now();
            world.stepTick();
            ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        }
        const WorldDiagnostics& d = world.diagnostics();
#ifdef NDEBUG
        const char* buildType = "Release";
#else
        const char* buildType = "Debug";
#endif
        const double p50 = percentile(ms, 0.50), p95 = percentile(ms, 0.95);
        const double maxMs = *std::max_element(ms.begin(), ms.end());
        std::printf("build=%s grid=%u ticks=%llu warmup=%llu S=%u C=%.3f\n", buildType, opt.grid,
                    (unsigned long long)opt.ticks, (unsigned long long)opt.warmup, d.substeps, d.courant);
        std::printf("tick_ms p50=%.4f p95=%.4f max=%.4f\n", p50, p95, maxMs);
        std::printf("halted=%d rejected_impulses=%llu max|h|=%.4g m\n", d.halted ? 1 : 0,
                    (unsigned long long)d.rejectedImpulses, d.maxAbsHeight);
        if (!opt.csv.empty()) {
            FILE* f = std::fopen(opt.csv.c_str(), "w");
            if (!f) {
                std::fprintf(stderr, "error: cannot open %s\n", opt.csv.c_str());
                return 1;
            }
            std::fprintf(f, "build,grid,ticks,warmup,substeps,p50_ms,p95_ms,max_ms,halted\n");
            std::fprintf(f, "%s,%u,%llu,%llu,%u,%.5f,%.5f,%.5f,%d\n", buildType, opt.grid,
                         (unsigned long long)opt.ticks, (unsigned long long)opt.warmup, d.substeps,
                         p50, p95, maxMs, d.halted ? 1 : 0);
            std::fclose(f);
        }
        return d.halted ? 1 : 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
