#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "app/ExperimentUI.h"
#include "core/Error.h"
#include "raylib.h"
#include "rlgl.h"
#include "rendering/HeightMapView.h"
#include "simulation/BoundaryConditions.h"
#include "simulation/SimulationWorld.h"

using namespace wl;

namespace {

[[noreturn]] void usage(const std::string& error) {
    if (!error.empty()) std::fprintf(stderr, "error: %s\n", error.c_str());
    std::fprintf(stderr,
                 "usage: waterlab [--grid 128|256|512] [--wave-speed M_PER_S] [--damping PER_S]\n"
                 "                [--boundary fixed|reflective|absorbing] [--smoke-capture FILE.png]\n");
    std::exit(error.empty() ? 0 : 2);
}

float parseFloat(const std::string& name, const char* text) {
    char* end = nullptr;
    const float v = std::strtof(text, &end);
    if (end == text || *end != '\0') usage("invalid number for " + name + ": " + text);
    return v;
}

struct AppOptions {
    WorldSettings world;
    // 起動直後に中心へ波紋を1回入れ、kSmokeFrames後の画面をPNGに保存して終了する（動作確認用）。
    std::string smokeCapturePath;
};

constexpr int kSmokeFrames = 90;

AppOptions parseOptions(int argc, char** argv) {
    AppOptions opt;
    WorldSettings& ws = opt.world;
    for (int k = 1; k < argc; ++k) {
        const std::string a = argv[k];
        auto next = [&]() -> const char* {
            if (k + 1 >= argc) usage("missing value for " + a);
            return argv[++k];
        };
        if (a == "--help" || a == "-h") {
            usage("");
        } else if (a == "--grid") {
            const std::string v = next();
            if (v != "128" && v != "256" && v != "512") usage("--grid must be 128, 256 or 512");
            ws.grid.nx = ws.grid.nz = uint32_t(std::stoul(v));
        } else if (a == "--wave-speed") {
            ws.wave.waveSpeed = parseFloat(a, next());
            if (!(ws.wave.waveSpeed >= 0.1f && ws.wave.waveSpeed <= 5.0f)) usage("--wave-speed must be 0.1..5.0 m/s");
        } else if (a == "--damping") {
            ws.wave.damping = parseFloat(a, next());
            if (!(ws.wave.damping >= 0.0f && ws.wave.damping <= 3.0f)) usage("--damping must be 0..3 1/s");
        } else if (a == "--boundary") {
            if (!parseBoundaryMode(next(), ws.wave.boundary)) usage("--boundary must be fixed, reflective or absorbing");
        } else if (a == "--smoke-capture") {
            opt.smokeCapturePath = next();
        } else {
            usage("unknown option " + a);
        }
    }
    return opt;
}

}  // namespace

int main(int argc, char** argv) {
    const AppOptions options = parseOptions(argc, argv);
    const WorldSettings& settings = options.world;
    const bool smoke = !options.smokeCapturePath.empty();
    try {
        SimulationWorld world(settings);

        SetConfigFlags(FLAG_WINDOW_HIGHDPI | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
        InitWindow(1280, 720, "WaterLab");
        SetWindowMinSize(800, 480);
        TraceLog(LOG_INFO, "WATERLAB: logical %dx%d, render %dx%d", GetScreenWidth(), GetScreenHeight(),
                 GetRenderWidth(), GetRenderHeight());
        TraceLog(LOG_INFO, "WATERLAB: grid %ux%u, c=%.2f m/s, gamma=%.2f 1/s, boundary=%s, S=%u",
                 settings.grid.nx, settings.grid.nz, settings.wave.waveSpeed, settings.wave.damping,
                 toString(settings.wave.boundary), world.diagnostics().substeps);

        {
            // GPU資源はCloseWindowより前に解放する。
            HeightMapView view(settings.grid.nx, settings.grid.nz);
            ExperimentUI ui(settings.grid);
            std::vector<float> renderHeight(world.renderCurrent().size());

            using Clock = std::chrono::steady_clock;
            Clock::time_point last = Clock::now();
            bool wasPaused = world.paused();
            int frame = 0;
            if (smoke) world.queueImpulse(Impulse{});

            while (!WindowShouldClose()) {
                const Clock::time_point frameStart = Clock::now();
                double frameDt = std::chrono::duration<double>(frameStart - last).count();
                last = frameStart;

                ui.handleInput(world);
                if (wasPaused && !world.paused()) frameDt = 0.0;  // 再開時は時計を取り直す
                wasPaused = world.paused();

                world.advanceFrame(frameDt);
                world.interpolatedHeight(renderHeight.data());
                view.update(renderHeight.data(), ui.heightRange());
                const double cpuMs =
                    std::chrono::duration<double, std::milli>(Clock::now() - frameStart).count();

                BeginDrawing();
                ClearBackground(Color{18, 22, 30, 255});
                view.draw(ui.viewRect());
                ui.drawOverlay(world, cpuMs);
                if (smoke && ++frame == kSmokeFrames) {
                    rlDrawRenderBatchActive();  // バッチ描画を確定させてから読み出す
                    Image shot = LoadImageFromScreen();
                    const bool saved = ExportImage(shot, options.smokeCapturePath.c_str());
                    UnloadImage(shot);
                    TraceLog(saved ? LOG_INFO : LOG_ERROR, "WATERLAB: smoke capture %s -> %s",
                             saved ? "saved" : "FAILED", options.smokeCapturePath.c_str());
                    EndDrawing();
                    break;
                }
                EndDrawing();
            }
        }
        CloseWindow();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        if (IsWindowReady()) CloseWindow();
        return 1;
    }
    return 0;
}
