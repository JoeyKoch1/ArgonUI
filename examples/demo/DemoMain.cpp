// ArgonUI - examples/demo/DemoMain.cpp
//
// The smallest complete ArgonUI application: a Win32 window, a D3D11 backend
// that owns its own device and swapchain, and one frame of menu per pump.
#include <cstdio>

#include "DemoMenu.h"
#include "Win32Host.h"

#include "argonui/Backend/Registry.h"

using namespace argon::ui;

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    argonhost::Win32Host host;

    std::string error;
    if (!host.Create(L"ArgonUI demo", 1280, 800, &error)) {
        MessageBoxW(nullptr, L"Could not create the window.", L"ArgonUI", MB_OK | MB_ICONERROR);
        return 1;
    }

    // --- Backend --------------------------------------------------------
    // CreateBackend picks the first compiled-in API, honouring the
    // ARGONUI_BACKEND environment variable if it is set.
    const GraphicsApi api = SelectBackend();
    IArgonBackend* backend = CreateBackend(api);
    if (backend == nullptr) {
        MessageBoxW(nullptr, L"No ArgonUI backend is available.", L"ArgonUI",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    DeviceDesc device;
    device.api = api;
    device.deviceSource = DeviceSource::CreateInternal;
    device.windowHandle = host.Handle();
    device.width = static_cast<u32>(host.Width());
    device.height = static_cast<u32>(host.Height());
    device.enableVsync = true;
    device.msaaSamples = 1;  // the shader anti-aliases analytically; MSAA would double-filter

    if (!backend->Initialize(device)) {
        MessageBoxA(nullptr, backend->LastError(), "ArgonUI backend failed to initialise",
                    MB_OK | MB_ICONERROR);
        delete backend;
        return 1;
    }

    TargetDesc target;
    target.targetSource = TargetSource::OwnSwapchain;
    target.width = device.width;
    target.height = device.height;
    if (!backend->CreateTarget(target)) {
        MessageBoxA(nullptr, backend->LastError(), "ArgonUI backend failed to create a target",
                    MB_OK | MB_ICONERROR);
        backend->Shutdown();
        delete backend;
        return 1;
    }

    // --- Context --------------------------------------------------------
    Context ctx;
    ContextConfig config;
    config.dpiScale = host.ContentScale();
    config.submitAutomatically = true;
    ctx.Initialize(backend, config);

    argondemo::MenuState state;
    InputState io;

    LARGE_INTEGER freq, last;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);
    u64 frameIndex = 0;

    // --- Loop -----------------------------------------------------------
    while (host.Pump()) {
        if (host.Width() <= 0 || host.Height() <= 0) {
            Sleep(16);
            continue;  // minimised
        }

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        const float dt =
            static_cast<float>(static_cast<double>(now.QuadPart - last.QuadPart) /
                               static_cast<double>(freq.QuadPart));
        last = now;

        // Resize the swapchain when the window changed. Doing it every frame is
        // safe: the backend ignores a no-op resize.
        backend->ResizeTarget(static_cast<u32>(host.Width()), static_cast<u32>(host.Height()));

        io.ClearAll();
        host.UpdateInput(io);

        FrameState fs;
        fs.frameIndex = ++frameIndex;
        fs.deltaTime = dt;
        fs.time += dt;
        fs.displaySize = Vec2(static_cast<float>(host.Width()), static_cast<float>(host.Height()));
        fs.dpiScale = host.ContentScale();
        fs.focused = GetForegroundWindow() == host.Handle();
        fs.occluded = fs.displaySize.x <= 0.0f || fs.displaySize.y <= 0.0f;

        ClearTooltips();
        ctx.NewFrame(fs);
        ctx.SetMetricsVisible(state.showMetrics);

        ctx.BeginLayout(Rect(0.0f, 0.0f, fs.displaySize.x, fs.displaySize.y));
        argondemo::DrawMenu(ctx, state);
        ctx.EndLayout();

        ctx.Render();
    }

    ctx.Shutdown();
    return 0;
}
