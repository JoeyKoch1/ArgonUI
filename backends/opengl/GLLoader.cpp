// ArgonUI - backends/opengl/GLLoader.cpp
#include "backends/opengl/GLLoader.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace argon::ui::gl {
namespace {
Api g_api;
std::mutex g_mutex;

// glGetString returns pointers into driver-owned storage that stays valid until
// the context dies, but we copy them anyway: BackendInfo hands these out as
// const char*, and a host is entitled to outlive the context.
char g_version[160] = {0};
char g_vendor[160] = {0};
char g_renderer[160] = {0};

/// Resolve one entry point.
///
/// wglGetProcAddress is the modern source, but it is not well behaved: some
/// drivers return NULL for missing functions and a handful return one of the
/// sentinel values 1, 2, 3 or -1. Treat all of those as "not found" and fall
/// back to GetProcAddress on opengl32.dll, which is where the GL 1.1 set lives.
void* Resolve(HMODULE glModule, const char* name) {
    void* proc = reinterpret_cast<void*>(wglGetProcAddress(name));
    if (proc == nullptr || proc == reinterpret_cast<void*>(static_cast<intptr_t>(1)) ||
        proc == reinterpret_cast<void*>(static_cast<intptr_t>(2)) ||
        proc == reinterpret_cast<void*>(static_cast<intptr_t>(3)) ||
        proc == reinterpret_cast<void*>(static_cast<intptr_t>(-1))) {
        proc = reinterpret_cast<void*>(::GetProcAddress(glModule, name));
    }
    return proc;
}

void CopyString(char* dst, size_t capacity, const char* src) {
    if (src == nullptr) { dst[0] = '\0'; return; }
    std::strncpy(dst, src, capacity - 1);
    dst[capacity - 1] = '\0';
}

/// The one place that actually touches the driver. Callers hold g_mutex.
bool LoadLocked(std::string* err) {
    if (g_api.loaded) return true;

    // Fail fast and legibly rather than resolving a table of garbage: without a
    // current context wglGetProcAddress has nothing to dispatch against.
    if (wglGetCurrentContext() == nullptr) {
        if (err != nullptr) {
            *err = "gl::Load: no OpenGL context is current on this thread. ArgonUI's GL "
                   "backend requires one before Initialize() (see DeviceSource::CreateInternal "
                   "to have the backend create it).";
        }
        return false;
    }

    HMODULE glModule = GetModuleHandleA("opengl32.dll");
    if (glModule == nullptr) glModule = LoadLibraryA("opengl32.dll");
    if (glModule == nullptr) {
        if (err != nullptr) *err = "gl::Load: opengl32.dll could not be loaded";
        return false;
    }

    std::memset(&g_api, 0, sizeof(g_api));
    // Point the identification fields at our own storage immediately, so a
    // half-finished table still hands out empty strings rather than null.
    g_api.version = g_version;
    g_api.vendor = g_vendor;
    g_api.renderer = g_renderer;

    std::string missing;
#define ARGON_GL_LOAD(field, name)                                                \
    do {                                                                          \
        g_api.field = reinterpret_cast<Pfn##field>(Resolve(glModule, name));      \
        if (g_api.field == nullptr) missing += std::string(" ") + (name);         \
    } while (0)

    // GL 1.1 - exported by opengl32.dll, resolved the same way for uniformity.
    ARGON_GL_LOAD(GetString, "glGetString");
    ARGON_GL_LOAD(GetIntegerv, "glGetIntegerv");
    ARGON_GL_LOAD(GetError, "glGetError");
    ARGON_GL_LOAD(Enable, "glEnable");
    ARGON_GL_LOAD(Disable, "glDisable");
    ARGON_GL_LOAD(BlendFunc, "glBlendFunc");
    ARGON_GL_LOAD(Viewport, "glViewport");
    ARGON_GL_LOAD(Scissor, "glScissor");
    ARGON_GL_LOAD(DrawElements, "glDrawElements");
    ARGON_GL_LOAD(PixelStorei, "glPixelStorei");
    ARGON_GL_LOAD(ReadPixels, "glReadPixels");

    ARGON_GL_LOAD(GenTextures, "glGenTextures");
    ARGON_GL_LOAD(BindTexture, "glBindTexture");
    ARGON_GL_LOAD(TexImage2D, "glTexImage2D");
    ARGON_GL_LOAD(TexSubImage2D, "glTexSubImage2D");
    ARGON_GL_LOAD(TexParameteri, "glTexParameteri");
    ARGON_GL_LOAD(DeleteTextures, "glDeleteTextures");

    ARGON_GL_LOAD(GenBuffers, "glGenBuffers");
    ARGON_GL_LOAD(BindBuffer, "glBindBuffer");
    ARGON_GL_LOAD(BufferData, "glBufferData");
    ARGON_GL_LOAD(BufferSubData, "glBufferSubData");
    ARGON_GL_LOAD(DeleteBuffers, "glDeleteBuffers");

    // GL 2.0 / 3.0 / 3.3 - all extension functions on Windows.
    ARGON_GL_LOAD(GenVertexArrays, "glGenVertexArrays");
    ARGON_GL_LOAD(BindVertexArray, "glBindVertexArray");
    ARGON_GL_LOAD(DeleteVertexArrays, "glDeleteVertexArrays");
    ARGON_GL_LOAD(EnableVertexAttribArray, "glEnableVertexAttribArray");
    ARGON_GL_LOAD(DisableVertexAttribArray, "glDisableVertexAttribArray");
    ARGON_GL_LOAD(VertexAttribPointer, "glVertexAttribPointer");
    ARGON_GL_LOAD(VertexAttribIPointer, "glVertexAttribIPointer");

    ARGON_GL_LOAD(CreateShader, "glCreateShader");
    ARGON_GL_LOAD(ShaderSource, "glShaderSource");
    ARGON_GL_LOAD(CompileShader, "glCompileShader");
    ARGON_GL_LOAD(GetShaderiv, "glGetShaderiv");
    ARGON_GL_LOAD(GetShaderInfoLog, "glGetShaderInfoLog");
    ARGON_GL_LOAD(DeleteShader, "glDeleteShader");
    ARGON_GL_LOAD(CreateProgram, "glCreateProgram");
    ARGON_GL_LOAD(AttachShader, "glAttachShader");
    ARGON_GL_LOAD(LinkProgram, "glLinkProgram");
    ARGON_GL_LOAD(GetProgramiv, "glGetProgramiv");
    ARGON_GL_LOAD(GetProgramInfoLog, "glGetProgramInfoLog");
    ARGON_GL_LOAD(UseProgram, "glUseProgram");
    ARGON_GL_LOAD(DeleteProgram, "glDeleteProgram");
    ARGON_GL_LOAD(GetUniformLocation, "glGetUniformLocation");
    ARGON_GL_LOAD(Uniform1i, "glUniform1i");
    ARGON_GL_LOAD(Uniform1f, "glUniform1f");
    ARGON_GL_LOAD(Uniform4fv, "glUniform4fv");

    ARGON_GL_LOAD(GenFramebuffers, "glGenFramebuffers");
    ARGON_GL_LOAD(BindFramebuffer, "glBindFramebuffer");
    ARGON_GL_LOAD(FramebufferTexture2D, "glFramebufferTexture2D");
    ARGON_GL_LOAD(FramebufferRenderbuffer, "glFramebufferRenderbuffer");
    ARGON_GL_LOAD(GetFramebufferAttachmentParameteriv, "glGetFramebufferAttachmentParameteriv");
    ARGON_GL_LOAD(CheckFramebufferStatus, "glCheckFramebufferStatus");
    ARGON_GL_LOAD(DeleteFramebuffers, "glDeleteFramebuffers");
    ARGON_GL_LOAD(GenRenderbuffers, "glGenRenderbuffers");
    ARGON_GL_LOAD(BindRenderbuffer, "glBindRenderbuffer");
    ARGON_GL_LOAD(RenderbufferStorage, "glRenderbufferStorage");
    ARGON_GL_LOAD(DeleteRenderbuffers, "glDeleteRenderbuffers");

    ARGON_GL_LOAD(GenSamplers, "glGenSamplers");
    ARGON_GL_LOAD(SamplerParameteri, "glSamplerParameteri");
    ARGON_GL_LOAD(BindSampler, "glBindSampler");
    ARGON_GL_LOAD(DeleteSamplers, "glDeleteSamplers");

#undef ARGON_GL_LOAD

    if (!missing.empty()) {
        if (err != nullptr) {
            *err = "gl::Load: this driver does not expose the OpenGL entry points "
                   "ArgonUI needs (missing:" + missing + ")";
        }
        return false;
    }

    CopyString(g_version, sizeof(g_version),
               reinterpret_cast<const char*>(g_api.GetString(GL_VERSION)));
    CopyString(g_vendor, sizeof(g_vendor),
               reinterpret_cast<const char*>(g_api.GetString(GL_VENDOR)));
    CopyString(g_renderer, sizeof(g_renderer),
               reinterpret_cast<const char*>(g_api.GetString(GL_RENDERER)));

    g_api.version = g_version;
    g_api.vendor = g_vendor;
    g_api.renderer = g_renderer;

    if (g_version[0] != '\0') {
        std::sscanf(g_version, "%d.%d", &g_api.major, &g_api.minor);
    }
    g_api.has_3_3_core = (g_api.major > 3) || (g_api.major == 3 && g_api.minor >= 3);

    g_api.GetIntegerv(GL_MAX_TEXTURE_SIZE, &g_api.max_texture_size);

    g_api.loaded = true;
    return true;
}

} // namespace

bool Load(std::string* err) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return LoadLocked(err);
}

const Api& Get() {
    static std::once_flag flag;
    std::call_once(flag, [] { Load(nullptr); });

    // A first attempt that failed (almost always "no context current yet") must
    // not poison the process: a host may legitimately call Initialize() before
    // its context exists and try again afterwards. LoadLocked() returns
    // immediately once the table is good, so this costs one lock per call on
    // the happy path and nothing else.
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_api.loaded) { LoadLocked(nullptr); }

    return g_api;
}

} // namespace argon::ui::gl
