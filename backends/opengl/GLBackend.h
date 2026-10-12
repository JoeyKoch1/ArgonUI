// ArgonUI - backends/opengl/GLBackend.h
//
// The OpenGL backend. One program, one VAO, one (vertex, index) buffer pair and
// one draw call per DrawCmd - exactly the shape of the D3D11 backend, with a
// GLSL port of shaders/argonui.hlsl.
//
// No third-party dependency: the only library linked is opengl32.lib, and every
// entry point past OpenGL 1.1 comes out of wglGetProcAddress through
// backends/opengl/GLLoader.h.
//
// CONTEXT POLICY - the one thing about GL that is unlike every other backend:
//
//   * Initialize() requires a context that is CURRENT ON THE CALLING THREAD.
//     A GL context is per-thread state, so ArgonUI never creates one on a
//     worker and never draws with one the host did not set up. Every call after
//     Initialize - Render, textures, Shutdown - obeys the same rule.
//
//   * DeviceSource::External       - the host already made its context current.
//                                   We probe and validate it; we never destroy
//                                   a context we did not create.
//   * DeviceSource::CreateInternal - we build a 3.3 core profile context for
//                                   DeviceDesc::windowHandle over WGL, make it
//                                   current, and delete it in Shutdown().
//
//   * Present() always returns false. OpenGL has no swapchain and no Present;
//     the host owns SwapBuffers. For an overlay that is exactly right - we must
//     not present somebody's window from inside a hook.
//
//   * TargetSource::OwnSwapchain     - the host's window system owns the swap
//                                     chain, so we draw into the DEFAULT
//                                     framebuffer (name 0).
//   * TargetSource::ExternalSwapchain - TargetDesc::externalSwapchain carries a
//                                     GLuint framebuffer name (0 = default).
//   * TargetSource::OffscreenTexture - a colour texture plus an FBO we own.
//
// ANTI-ALIASING: ArgonUI anti-aliases analytically in the shader (fwidth on the
// SDF), so the framebuffer must be SINGLE sampled or every edge is filtered
// twice. Our own FBO always is. A host that hands us a multisampled default
// framebuffer gets slightly softer edges, and Initialize() says so in the log.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <string>
#include <unordered_map>

#include "argonui/Backend/Backend.h"
#include "backends/opengl/GLLoader.h"

namespace argon::ui {
class GLBackend final : public IArgonBackend {
public:
    GLBackend();
    ~GLBackend() override;

    // -- IArgonBackend -----------------------------------------------------
    bool Initialize(const DeviceDesc& desc) override;
    void Shutdown() override;

    bool CreateTarget(const TargetDesc& desc) override;
    bool AdoptTarget(const TargetDesc& desc) override;
    bool ResizeTarget(u32 width, u32 height) override;
    void DestroyTarget() override;
    bool HasTarget() const override { return hasTarget_; }
    Vec2 TargetSize() const override { return targetSize_; }

    bool BeginFrame(const FrameState& state) override;
    void Render(const DrawList& list, const FrameState& state) override;
    bool Present() override;
    void EndFrame() override;

    u32 CreateTexture(const TextureDesc& desc, const void* pixels = nullptr) override;
    bool UpdateTexture(u32 handle, const TextureDesc& desc, const void* pixels) override;
    void DestroyTexture(u32 handle) override;

    const BackendInfo& Info() const override { return info_; }
    const char* Name() const override { return "OpenGL"; }
    const char* LastError() const override { return lastError_.c_str(); }

    // -- Escape hatches for hosts that interleave their own draws ---------
    /// GL_RENDERER / GL_VENDOR / GL_VERSION, for an overlay's debug tab.
    const char* GlVersion() const;
    u32 DrawCallCount() const { return drawCallCount_; }

    /// glReadPixels off the current framebuffer binding. `channels` is 3 (RGB)
    /// or 4 (RGBA). GL reads bottom-up; flipY=true rewrites `dst` top-down, so
    /// the result matches what the UI drew. Needs a current context, like
    /// everything else here.
    bool ReadTargetPixels(u32 x, u32 y, u32 width, u32 height, u32 channels, void* dst,
                          bool flipY = true);

private:
    struct TextureRecord {
        gl::GLuint name = 0;
        TextureDesc desc{};
    };

    // -- Setup -------------------------------------------------------------
    bool CompileShaders();
    bool LinkProgram();
    bool CreatePipeline();
    bool CreateStaticResources();
    bool CreateModernContext(HWND window);
    void DestroyModernContext();
    bool CheckGl(const char* what);

    // -- Geometry ----------------------------------------------------------
    bool ConfigureVertexArray();
    bool EnsureVertexCapacity(size_t vertices, size_t indices);

    // -- Textures ----------------------------------------------------------
    bool CreateTextureInternal(const TextureDesc& desc, const void* pixels, TextureRecord& out);
    bool UploadTextureData(const TextureDesc& desc, const void* pixels, TextureRecord& rec);
    const TextureRecord* FindTexture(u32 handle) const;

    // -- Target ------------------------------------------------------------
    bool CreateDefaultFramebufferTarget(u32 width, u32 height);
    bool CreateOffscreenTarget(u32 width, u32 height);
    bool RebuildFramebufferView(u32 fallbackWidth, u32 fallbackHeight);
    bool QueryDefaultFramebufferSize(u32& width, u32& height);
    void ApplyTargetSize(u32 width, u32 height);

    // -- Frame -------------------------------------------------------------
    void SetBlend(BlendMode mode);
    void SetTexture(u32 handle);
    bool SetScissor(const Rect& clip);
    void Fail(const char* what);

    // -- Device ------------------------------------------------------------
    bool initialized_ = false;
    bool ownsContext_ = false;
    void* windowHandle_ = nullptr;   ///< HWND when the host gave us one
    u32 deviceWidth_ = 0;            ///< DeviceDesc fallback for the target size
    u32 deviceHeight_ = 0;

    // The context we created ourselves, and therefore must delete in Shutdown().
    HGLRC context_ = nullptr;
    HGLRC previousContext_ = nullptr;
    HDC contextDC_ = nullptr;

    // -- Target ------------------------------------------------------------
    TargetSource targetSource_ = TargetSource::OwnSwapchain;
    gl::GLuint targetFramebuffer_ = 0;   ///< what Render() binds (0 = default)
    gl::GLuint externalFramebuffer_ = 0; ///< the host's name, when we wrapped it
    gl::GLuint framebufferView_ = 0;     ///< FBO view we own over a foreign one
    gl::GLuint offscreenTexture_ = 0;   ///< offscreen colour attachment
    gl::GLuint offscreenDepth_ = 0;     ///< offscreen depth-stencil attachment
    bool hasTarget_ = false;
    u32 targetWidth_ = 0;
    u32 targetHeight_ = 0;
    Vec2 targetSize_{0.0f, 0.0f};

    // -- Pipeline ----------------------------------------------------------
    GLuint program_ = 0;
    GLuint vertexShader_ = 0;
    GLuint fragmentShader_ = 0;
    GLint uniformViewport_ = -1;
    GLint uniformTexture_ = -1;

    GLuint vertexArray_ = 0;
    GLuint vertexBuffer_ = 0;
    GLuint indexBuffer_ = 0;
    size_t vertexBufferSize_ = 0;  ///< bytes
    size_t indexBufferSize_ = 0;   ///< bytes

    /// Two sampler objects: LINEAR and NEAREST. They override the texture's own
    /// parameters so a host cannot change how ArgonUI samples, and they let a
    /// NEAREST glyph atlas and a LINEAR photo share one texture unit.
    GLuint linearSampler_ = 0;
    GLuint nearestSampler_ = 0;

    /// Handle 1: the built-in 1x1 white. Handle 0 resolves to it as well.
    TextureRecord whiteTexture_{};

    // -- Textures ----------------------------------------------------------
    std::unordered_map<u32, TextureRecord> textures_;
    u32 nextTextureId_ = 2;

    // -- Frame -------------------------------------------------------------
    bool frameBegun_ = false;
    u32 drawCallCount_ = 0;
    BlendMode boundBlend_ = BlendMode::Alpha;
    bool blendDirty_ = true;
    u32 boundTexture_ = 0;
    bool textureDirty_ = true;
    /// Scissor box last handed to glScissor, in pixels. -1 means "none", which
    /// can never equal a real box.
    Rect boundScissor_{-1.0f, -1.0f, -1.0f, -1.0f};

    BackendInfo info_{};
    std::string lastError_;
};

/// Factory used by Backend/Registry.cpp. Never returns nullptr.
IArgonBackend* CreateOpenGLBackend();

} // namespace argon::ui
