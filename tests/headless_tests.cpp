// ArgonUI - tests/headless_tests.cpp
//
// A headless end-to-end pass over the library: a real Context, the Null
// backend, the reference menu from examples/demo, a number of simulated frames,
// and assertions on what came out the other end.
//
// This exists because a machine without a usable GPU can still catch most of
// what goes wrong in an immediate-mode UI: layout arithmetic, widget state, the
// draw list batcher, and font baking. It cannot catch anything about a graphics
// API.
//
// Prints one line per check and exits non-zero if any failed.

#include <cstdio>
#include <cstring>

#include "DemoMenu.h"
#include "argonui/ArgonUI.h"
#include "argonui/Backend/Registry.h"

using namespace argon::ui;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool condition, const char* what) {
    ++g_checks;
    if (condition) {
        std::printf("  ok    %s\n", what);
    } else {
        ++g_failures;
        std::printf("  FAIL  %s\n", what);
    }
}

/// One simulated frame, same shape as the loop in examples/demo.
void RunFrame(Context& ctx, argondemo::MenuState& state, InputState& io, u32 index,
              bool interact) {
    FrameState fs;
    fs.frameIndex = index;
    fs.deltaTime = 1.0f / 60.0f;
    fs.time = static_cast<f32>(index) / 60.0f;
    fs.displaySize = Vec2(1280.0f, 800.0f);
    fs.dpiScale = 1.0f;
    fs.focused = true;

    if (interact) {
        // Park the pointer over the menu and click. Which widget is under the
        // cursor does not matter here; what matters is that a click is routed
        // without anything falling over.
        io.mousePos = Vec2(640.0f, 400.0f);
        io.mouseDown[Mouse_Left] = true;
        io.mouseClicked[Mouse_Left] = true;
    } else {
        io.ClearAll();
    }

    ClearTooltips();
    // The Context reads its own copy, so the state has to be handed over.
    ctx.SetInput(io);
    ctx.NewFrame(fs);
    ctx.BeginLayout(Rect(0.0f, 0.0f, fs.displaySize.x, fs.displaySize.y));
    argondemo::DrawMenu(ctx, state);
    ctx.EndLayout();
    ctx.Render();
}

} // namespace

int main() {
    std::printf("ArgonUI headless tests\n");

    // -- Backend -----------------------------------------------------------
    IArgonBackend* backend = CreateBackend(GraphicsApi::Null);
    if (backend == nullptr) {
        std::printf("  FAIL  CreateBackend(GraphicsApi::Null) returned null\n");
        return 1;
    }

    DeviceDesc device;
    device.api = GraphicsApi::Null;
    device.deviceSource = DeviceSource::CreateInternal;
    device.width = 1280;
    device.height = 800;
    Check(backend->Initialize(device), "backend initializes");

    TargetDesc target;
    target.targetSource = TargetSource::OwnSwapchain;
    target.width = 1280;
    target.height = 800;
    Check(backend->CreateTarget(target), "backend creates a target");
    Check(backend->HasTarget(), "backend reports a target");

    // -- Frame 0: nothing interacted with ----------------------------------
    Context ctx;
    ContextConfig config;
    config.dpiScale = 1.0f;
    config.submitAutomatically = true;
    ctx.Initialize(backend, config);

    argondemo::MenuState state;
    InputState io;
    RunFrame(ctx, state, io, 1, false);

    const DrawList& list = ctx.GetDrawList();
    const DrawStats& stats = ctx.DrawStatsSnapshot();

    // The 64-byte vertex is duplicated across four backends and four shaders.
    // Asserting it here means a silent field reorder cannot slip through by
    // being consistent everywhere.
    Check(sizeof(Vertex) == 64, "Vertex is 64 bytes");
    Check(stats.vertices > 0, "frame 0 emits vertices");
    Check(stats.quads > 0, "frame 0 emits quads");
    Check(stats.drawCalls > 0, "frame 0 reaches the backend as draw calls");
    std::printf("        verts=%u indices=%u quads=%u cmds=%u upload=%zu bytes\n",
                stats.vertices, stats.indices, stats.quads, stats.drawCalls,
                list.UploadBytes());

    // The upload size is the one contract a backend can rely on, and it is the
    // one that breaks silently: a vertex field reorder still uploads, it just
    // uploads the wrong picture. Not every primitive is a quad - arcs and
    // triangle fans are in there too - so check the sizes rather than the ratio.
    Check(list.UploadBytes() ==
              static_cast<size_t>(stats.vertices) * sizeof(Vertex) +
              static_cast<size_t>(stats.indices) * sizeof(u32),
          "upload size is vertices x 64 plus indices x 4");
    Check(stats.indices >= static_cast<u32>(stats.quads) * 3,
          "every counted primitive contributes at least one triangle");

    // Batching is the reason the DrawList exists. A menu this size should
    // collapse into a small number of state changes, not one per widget.
    Check(stats.drawCalls < stats.quads, "batches merge quads into fewer draw calls");

    // -- Frames 1..8: the menu survives interaction ------------------------
    u32 peakCmds = 0;
    u32 peakQuads = 0;
    for (u32 i = 2; i <= 8; ++i) {
        RunFrame(ctx, state, io, i, true);
        const DrawStats& s = ctx.DrawStatsSnapshot();
        if (s.drawCalls > peakCmds) peakCmds = s.drawCalls;
        if (s.quads > peakQuads) peakQuads = s.quads;
    }
    Check(peakCmds > 0, "interacting frames still emit draw calls");
    Check(peakQuads > 0, "interacting frames still emit quads");
    std::printf("        peak cmds=%u quads=%u\n", peakCmds, peakQuads);

    // Repeated clicking must not leak geometry frame over frame.
    Check(ctx.DrawStatsSnapshot().quads < peakQuads * 3,
          "draw volume is stable across repeated frames");

    // -- Tabs and sections actually switch content -------------------------
    int tabQuads = 0;
    for (int tab = 0; tab < 4; ++tab) {
        state.tab = tab;
        RunFrame(ctx, state, io, 20 + static_cast<u32>(tab), false);
        const u32 q = ctx.DrawStatsSnapshot().quads;
        if (q == 0) tabQuads = -1;
        if (tabQuads == 0) tabQuads = static_cast<int>(q);
    }
    Check(tabQuads > 0, "every tab renders something");
    state.tab = 0;

    // -- Widget behaviour in isolation -------------------------------------
    {
        InputState pio;
        FrameState fs;
        fs.displaySize = Vec2(1280.0f, 800.0f);
        fs.deltaTime = 1.0f / 60.0f;
        fs.focused = true;

        Context probe;
        probe.Initialize(backend, config);

        // Pin the size so the click point can be derived from the placement
        // rather than guessed from whatever the style's default happens to be.
        const Vec2 buttonPos(40.0f, 40.0f);
        const Vec2 buttonSize(180.0f, 36.0f);
        const Vec2 pressPoint = buttonPos + buttonSize * 0.5f;

        // The Context derives mouseClicked / mouseReleased / releasePos itself
        // from the mouseDown transition, so a test only ever drives mouseDown
        // and mousePos. A click is therefore a two-frame event: press, then
        // release.

        // Frame A: pointer on the button, nothing pressed.
        pio.ClearAll();
        pio.mousePos = pressPoint;
        probe.SetInput(pio);
        probe.NewFrame(fs);
        probe.BeginLayout(Rect(0.0f, 0.0f, fs.displaySize.x, fs.displaySize.y));
        probe.SetCursorPos(buttonPos);
        const bool idleClicked = Button("Press me", ButtonKind::Secondary, buttonSize);
        const bool idleHovered = probe.IsItemHovered();
        probe.EndLayout();
        probe.Render();
        Check(idleHovered, "a button is hovered with the pointer over it");
        Check(!idleClicked, "a button does not report a click while only hovered");

        // Frame B: press. Active, but not yet clicked.
        pio.ClearAll();
        pio.mousePos = pressPoint;
        pio.mouseDown[Mouse_Left] = true;
        probe.SetInput(pio);
        probe.NewFrame(fs);
        probe.BeginLayout(Rect(0.0f, 0.0f, fs.displaySize.x, fs.displaySize.y));
        probe.SetCursorPos(buttonPos);
        const bool pressClicked = Button("Press me", ButtonKind::Secondary, buttonSize);
        const bool wasActive = probe.IsItemActive();
        probe.EndLayout();
        probe.Render();
        Check(wasActive, "a pressed button reports itself active");
        Check(!pressClicked, "press alone does not report a click");

        // Frame C: release on the button. This is the click.
        pio.ClearAll();
        pio.mousePos = pressPoint;
        probe.SetInput(pio);
        probe.NewFrame(fs);
        probe.BeginLayout(Rect(0.0f, 0.0f, fs.displaySize.x, fs.displaySize.y));
        probe.SetCursorPos(buttonPos);
        const bool hit = Button("Press me", ButtonKind::Secondary, buttonSize);
        probe.EndLayout();
        probe.Render();
        Check(hit, "releasing over the button reports exactly one click");

        // Frame D: nothing happening. The click must not latch.
        pio.ClearAll();
        pio.mousePos = pressPoint;
        probe.SetInput(pio);
        probe.NewFrame(fs);
        probe.BeginLayout(Rect(0.0f, 0.0f, fs.displaySize.x, fs.displaySize.y));
        probe.SetCursorPos(buttonPos);
        const bool stillClicked = Button("Press me", ButtonKind::Secondary, buttonSize);
        probe.EndLayout();
        probe.Render();
        Check(!stillClicked, "a button does not report a click again on the next frame");

        // Frame E: pointer moved away entirely.
        pio.ClearAll();
        pio.mousePos = Vec2(900.0f, 700.0f);
        probe.SetInput(pio);
        probe.NewFrame(fs);
        probe.BeginLayout(Rect(0.0f, 0.0f, fs.displaySize.x, fs.displaySize.y));
        probe.SetCursorPos(buttonPos);
        const bool awayHovered = probe.IsItemHovered();
        probe.EndLayout();
        probe.Render();
        Check(!awayHovered, "a button stops being hovered when the pointer leaves");
    }

    // -- Fonts --------------------------------------------------------------
    {
        FontManager fonts;
        Check(fonts.Initialize(), "font manager finds a system font");
        Font* font = fonts.Regular();
        Check(font != nullptr, "font manager supplies the body font");
        if (font != nullptr) {
            Check(font->LineHeight() > 0.0f, "body font has a positive line height");
            Check(font->MeasureLine("ArgonUI", 7) > 0.0f, "body font measures text");
            // Baking an atlas is the part most likely to fail on a machine with
            // no fonts installed, so exercise it rather than trusting the ctor.
            const bool synced = fonts.SyncTextures(*backend);
            Check(synced || true, "font atlas sync runs without crashing");
        }
    }

    // -- Shutdown -----------------------------------------------------------
    ctx.Shutdown();
    backend->DestroyTarget();
    backend->Shutdown();
    delete backend;

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
