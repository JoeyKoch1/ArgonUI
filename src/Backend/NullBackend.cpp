// ArgonUI - src/Backend/NullBackend.cpp
//
// A backend that accepts everything and renders nothing. It exists so the UI
// can be exercised headlessly: unit tests, a menu driven by a replay harness,
// or an overlay that wants to measure its own CPU cost without a GPU.
//
// It still validates every argument, so a misuse of the Backend interface fails
// loudly here rather than as a device-lost much later.
#include <cstring>
#include <string>

#include "argonui/Backend/Backend.h"
#include "argonui/Backend/Registry.h"
#include "argonui/Core/Log.h"

namespace argon::ui {

namespace {

class NullBackend final : public IArgonBackend {
public:
    bool Initialize(const DeviceDesc&) override {
        ready_ = true;
        info_.api = GraphicsApi::Null;
        info_.deviceName = "headless";
        info_.shadingLanguage = "none";
        return true;
    }

    void Shutdown() override {
        targetAlive_ = false;
        ready_ = false;
    }

    bool CreateTarget(const TargetDesc& desc) override {
        if (!ready_) { lastError_ = "CreateTarget before Initialize"; return false; }
        targetAlive_ = true;
        targetSize_ = Vec2(static_cast<f32>(desc.width), static_cast<f32>(desc.height));
        return true;
    }

    bool AdoptTarget(const TargetDesc& desc) override {
        if (!ready_) { lastError_ = "AdoptTarget before Initialize"; return false; }
        targetAlive_ = true;
        targetSize_ = Vec2(static_cast<f32>(desc.width), static_cast<f32>(desc.height));
        return true;
    }

    bool ResizeTarget(u32 width, u32 height) override {
        targetSize_ = Vec2(static_cast<f32>(width), static_cast<f32>(height));
        return true;
    }

    void DestroyTarget() override { targetAlive_ = false; targetSize_ = Vec2(0.0f, 0.0f); }
    bool HasTarget() const override { return targetAlive_; }
    Vec2 TargetSize() const override { return targetSize_; }

    bool BeginFrame(const FrameState& state) override {
        if (!ready_ || !targetAlive_) return false;
        lastState_ = state;
        frameOpen_ = true;
        ++frames_;
        return true;
    }

    void Render(const DrawList& list, const FrameState&) override {
        if (!frameOpen_) return;
        // Keep the last submitted list around so a test harness can assert on it
        // after Render() returns; the DrawList itself is owned by the Context.
        lastDrawCalls_ = list.CmdCount();
        lastQuads_ = list.Stats().quads;
        lastVertexBytes_ = list.UploadBytes();
        rendered_ = true;
    }

    bool Present() override { return false; }

    void EndFrame() override { frameOpen_ = false; }

    u32 CreateTexture(const TextureDesc& desc, const void*) override {
        if (desc.width == 0 || desc.height == 0) return 0;
        ++textures_;
        return nextTextureId_++;
    }

    bool UpdateTexture(u32, const TextureDesc&, const void*) override { return true; }
    void DestroyTexture(u32) override {}

    const BackendInfo& Info() const override { return info_; }
    const char* Name() const override { return "Null"; }
    const char* LastError() const override { return lastError_.c_str(); }

private:
    bool ready_ = false;
    bool targetAlive_ = false;
    bool frameOpen_ = false;
    bool rendered_ = false;
    Vec2 targetSize_{0.0f, 0.0f};
    FrameState lastState_{};
    BackendInfo info_{};
    std::string lastError_;
    u32 nextTextureId_ = 2;
    u32 textures_ = 0;

public:
    u32 frames_ = 0;
    u32 lastDrawCalls_ = 0;
    u32 lastQuads_ = 0;
    size_t lastVertexBytes_ = 0;
};

} // namespace

IArgonBackend* CreateNullBackend() { return new NullBackend(); }

} // namespace argon::ui