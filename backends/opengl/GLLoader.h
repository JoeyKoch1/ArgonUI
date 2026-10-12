// ArgonUI - backends/opengl/GLLoader.h
//
// A deliberately tiny OpenGL entry-point loader. No glad, no GLEW, no glfw.
//
// On Windows, opengl32.dll exports exactly the OpenGL 1.1 API. Everything
// ArgonUI needs past that - VAOs, GLSL 3.30, framebuffer objects, sampler
// objects - is an extension function that has to come out of wglGetProcAddress.
// That is three lines of code, so there is no reason to drag in a generator that
// emits 3500 declarations we will never call.
//
// Two rules:
//
//   * Get()/Load() must be called from a thread that has a **current** OpenGL
//     context. wglGetProcAddress dispatches through the driver, so it only
//     returns meaningful pointers once a context exists - and a context is
//     per-thread state. ArgonUI's backend calls this from Initialize(), with
//     the host's context (or the one we created) already made current.
//
//   * Entry points are process-global, not per-context, so the table is loaded
//     once and then shared by every context the process ever makes.
//
// The only link-time dependency is opengl32.lib, and only for the handful of
// WGL entry points that Microsoft exports for real (wglCreateContext,
// wglMakeCurrent, wglDeleteContext, wglGetProcAddress, wglGetCurrentContext).
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <GL/gl.h>

#include <cstddef>
#include <string>

// Enumerants the Windows SDK's <GL/gl.h> does not carry
// opengl32's header stops at GL 1.1 (plus a few ancient EXT aliases), so every
// token ArgonUI needs from 1.2 onwards is declared here. Each one is guarded:
// any driver header or future SDK that already provides it wins, and where the
// SDK disagrees (GL_BGRA, GL_CLAMP_TO_EDGE) the same guard keeps both spellings
// in agreement.

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif
#ifndef GL_BGR
#define GL_BGR 0x80E0
#endif
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_RG8
#define GL_RG8 0x822B
#endif
#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_ACTIVE_TEXTURE
#define GL_ACTIVE_TEXTURE 0x84E0
#endif
#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif
#ifndef GL_SAMPLE_BUFFERS
#define GL_SAMPLE_BUFFERS 0x80A8
#endif
#ifndef GL_SAMPLES
#define GL_SAMPLES 0x80A9
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER 0x8D41
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_DEPTH_STENCIL_ATTACHMENT
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME
#define GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME 0x8CD1
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_WIDTH
#define GL_FRAMEBUFFER_ATTACHMENT_WIDTH 0x8CE5
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_HEIGHT
#define GL_FRAMEBUFFER_ATTACHMENT_HEIGHT 0x8CE6
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_FRAMEBUFFER_SRGB
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_INFO_LOG_LENGTH
#define GL_INFO_LOG_LENGTH 0x8B84
#endif

// GL 2.0 scalar types that MS's gl.h does not declare.
#ifndef GLchar
typedef char GLchar;
#endif
#ifndef GLsizeiptr
typedef ptrdiff_t GLsizeiptr;
#endif
#ifndef GLintptr
typedef ptrdiff_t GLintptr;
#endif

namespace argon::ui::gl {
// The GL scalar types, re-exported so everything below (and the backend) can be
// spelled gl::GLuint without relying on qualified lookup falling back to the
// global namespace.
using GLenum = ::GLenum;
using GLbitfield = ::GLbitfield;
using GLuint = ::GLuint;
using GLint = ::GLint;
using GLsizei = ::GLsizei;
using GLfloat = ::GLfloat;
using GLboolean = ::GLboolean;
using GLchar = ::GLchar;

// Signatures
// APIENTRY on every one of these, including the 1.1 set: opengl32.dll exports
// them __stdcall, and leaving it off would corrupt the stack under x86.

using PfnGetString = const GLubyte* (APIENTRY*)();
using PfnGetIntegerv = void (APIENTRY*)(GLenum, GLint*);
using PfnGetError = GLenum (APIENTRY*)();

using PfnEnable = void (APIENTRY*)(GLenum);
using PfnDisable = void (APIENTRY*)(GLenum);
using PfnBlendFunc = void (APIENTRY*)(GLenum, GLenum);
using PfnViewport = void (APIENTRY*)(GLint, GLint, GLsizei, GLsizei);
using PfnScissor = void (APIENTRY*)(GLint, GLint, GLsizei, GLsizei);
using PfnDrawElements = void (APIENTRY*)(GLenum, GLsizei, GLenum, const void*);
using PfnPixelStorei = void (APIENTRY*)(GLenum, GLint);
using PfnReadPixels = void (APIENTRY*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);

using PfnGenTextures = void (APIENTRY*)(GLsizei, GLuint*);
using PfnBindTexture = void (APIENTRY*)(GLenum, GLuint);
using PfnTexImage2D = void (APIENTRY*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
using PfnTexSubImage2D = void (APIENTRY*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*);
using PfnTexParameteri = void (APIENTRY*)(GLenum, GLenum, GLint);
using PfnDeleteTextures = void (APIENTRY*)(GLsizei, const GLuint*);

using PfnGenBuffers = void (APIENTRY*)(GLsizei, GLuint*);
using PfnBindBuffer = void (APIENTRY*)(GLenum, GLuint);
using PfnBufferData = void (APIENTRY*)(GLenum, GLsizeiptr, const void*, GLenum);
using PfnBufferSubData = void (APIENTRY*)(GLenum, GLintptr, GLsizeiptr, const void*);
using PfnDeleteBuffers = void (APIENTRY*)(GLsizei, const GLuint*);

using PfnGenVertexArrays = void (APIENTRY*)(GLsizei, GLuint*);
using PfnBindVertexArray = void (APIENTRY*)(GLuint);
using PfnDeleteVertexArrays = void (APIENTRY*)(GLsizei, const GLuint*);
using PfnEnableVertexAttribArray = void (APIENTRY*)(GLuint);
using PfnDisableVertexAttribArray = void (APIENTRY*)(GLuint);
using PfnVertexAttribPointer = void (APIENTRY*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
using PfnVertexAttribIPointer = void (APIENTRY*)(GLuint, GLint, GLenum, GLsizei, const void*);

using PfnCreateShader = GLuint (APIENTRY*)(GLenum);
using PfnShaderSource = void (APIENTRY*)(GLuint, GLsizei, const GLchar* const*, const GLint*);
using PfnCompileShader = void (APIENTRY*)(GLuint);
using PfnGetShaderiv = void (APIENTRY*)(GLuint, GLenum, GLint*);
using PfnGetShaderInfoLog = void (APIENTRY*)(GLuint, GLsizei, GLsizei*, GLchar*);
using PfnDeleteShader = void (APIENTRY*)(GLuint);
using PfnCreateProgram = GLuint (APIENTRY*)();
using PfnAttachShader = void (APIENTRY*)(GLuint, GLuint);
using PfnLinkProgram = void (APIENTRY*)(GLuint);
using PfnGetProgramiv = void (APIENTRY*)(GLuint, GLenum, GLint*);
using PfnGetProgramInfoLog = void (APIENTRY*)(GLuint, GLsizei, GLsizei*, GLchar*);
using PfnUseProgram = void (APIENTRY*)(GLuint);
using PfnDeleteProgram = void (APIENTRY*)(GLuint);
using PfnGetUniformLocation = GLint (APIENTRY*)(GLuint, const GLchar*);
using PfnUniform1i = void (APIENTRY*)(GLint, GLint);
using PfnUniform1f = void (APIENTRY*)(GLint, GLfloat);
using PfnUniform4fv = void (APIENTRY*)(GLint, GLsizei, const GLfloat*);

using PfnGenFramebuffers = void (APIENTRY*)(GLsizei, GLuint*);
using PfnBindFramebuffer = void (APIENTRY*)(GLenum, GLuint);
using PfnFramebufferTexture2D = void (APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
using PfnFramebufferRenderbuffer = void (APIENTRY*)(GLenum, GLenum, GLenum, GLuint);
using PfnGetFramebufferAttachmentParameteriv = void (APIENTRY*)(GLenum, GLenum, GLenum, GLint*);
using PfnCheckFramebufferStatus = GLenum (APIENTRY*)(GLenum);
using PfnDeleteFramebuffers = void (APIENTRY*)(GLsizei, const GLuint*);
using PfnGenRenderbuffers = void (APIENTRY*)(GLsizei, GLuint*);
using PfnBindRenderbuffer = void (APIENTRY*)(GLenum, GLuint);
using PfnRenderbufferStorage = void (APIENTRY*)(GLenum, GLenum, GLsizei, GLsizei);
using PfnDeleteRenderbuffers = void (APIENTRY*)(GLsizei, const GLuint*);
using PfnGenSamplers = void (APIENTRY*)(GLsizei, GLuint*);
using PfnSamplerParameteri = void (APIENTRY*)(GLuint, GLenum, GLint);
using PfnBindSampler = void (APIENTRY*)(GLuint, GLuint);
using PfnDeleteSamplers = void (APIENTRY*)(GLsizei, const GLuint*);

// The table

struct Api {
    // -- Identification ----------------------------------------------------
    bool loaded = false;
    /// True when the driver reports at least 3.3, which is ArgonUI's floor
    /// (VAOs are non-negotiable; the shader targets #version 330 core).
    bool has_3_3_core = false;
    int major = 0;
    int minor = 0;
    /// GL_VERSION, GL_VENDOR, GL_RENDERER as returned by glGetString. They point
    /// at storage owned by the loader and stay valid for the process lifetime.
    const char* version = "";
    const char* vendor = "";
    const char* renderer = "";
    GLint max_texture_size = 0;

    // -- GL 1.1 ------------------------------------------------------------
    PfnGetString GetString;
    PfnGetIntegerv GetIntegerv;
    PfnGetError GetError;
    PfnEnable Enable;
    PfnDisable Disable;
    PfnBlendFunc BlendFunc;
    PfnViewport Viewport;
    PfnScissor Scissor;
    PfnDrawElements DrawElements;
    PfnPixelStorei PixelStorei;
    PfnReadPixels ReadPixels;

    // -- Textures (GL 1.1) -------------------------------------------------
    PfnGenTextures GenTextures;
    PfnBindTexture BindTexture;
    PfnTexImage2D TexImage2D;
    PfnTexSubImage2D TexSubImage2D;
    PfnTexParameteri TexParameteri;
    PfnDeleteTextures DeleteTextures;

    // -- Buffers (GL 1.1) --------------------------------------------------
    PfnGenBuffers GenBuffers;
    PfnBindBuffer BindBuffer;
    PfnBufferData BufferData;
    PfnBufferSubData BufferSubData;
    PfnDeleteBuffers DeleteBuffers;

    // -- Vertex arrays (GL 3.0) --------------------------------------------
    PfnGenVertexArrays GenVertexArrays;
    PfnBindVertexArray BindVertexArray;
    PfnDeleteVertexArrays DeleteVertexArrays;
    PfnEnableVertexAttribArray EnableVertexAttribArray;
    PfnDisableVertexAttribArray DisableVertexAttribArray;
    PfnVertexAttribPointer VertexAttribPointer;
    /// Integer attributes MUST go through this, not VertexAttribPointer.
    PfnVertexAttribIPointer VertexAttribIPointer;

    // -- GLSL programs (GL 2.0 / 4.0 for the query entry points) ---------
    PfnCreateShader CreateShader;
    PfnShaderSource ShaderSource;
    PfnCompileShader CompileShader;
    PfnGetShaderiv GetShaderiv;
    PfnGetShaderInfoLog GetShaderInfoLog;
    PfnDeleteShader DeleteShader;
    PfnCreateProgram CreateProgram;
    PfnAttachShader AttachShader;
    PfnLinkProgram LinkProgram;
    PfnGetProgramiv GetProgramiv;
    PfnGetProgramInfoLog GetProgramInfoLog;
    PfnUseProgram UseProgram;
    PfnDeleteProgram DeleteProgram;
    PfnGetUniformLocation GetUniformLocation;
    PfnUniform1i Uniform1i;
    /// Loaded for completeness: the 3.30 pipeline needs no float uniform today.
    PfnUniform1f Uniform1f;
    PfnUniform4fv Uniform4fv;

    // -- Framebuffers / renderbuffers (GL 3.0) -----------------------------
    PfnGenFramebuffers GenFramebuffers;
    PfnBindFramebuffer BindFramebuffer;
    PfnFramebufferTexture2D FramebufferTexture2D;
    PfnFramebufferRenderbuffer FramebufferRenderbuffer;
    PfnGetFramebufferAttachmentParameteriv GetFramebufferAttachmentParameteriv;
    PfnCheckFramebufferStatus CheckFramebufferStatus;
    PfnDeleteFramebuffers DeleteFramebuffers;
    PfnGenRenderbuffers GenRenderbuffers;
    PfnBindRenderbuffer BindRenderbuffer;
    PfnRenderbufferStorage RenderbufferStorage;
    PfnDeleteRenderbuffers DeleteRenderbuffers;

    // -- Sampler objects (GL 3.3) ------------------------------------------
    PfnGenSamplers GenSamplers;
    PfnSamplerParameteri SamplerParameteri;
    PfnBindSampler BindSampler;
    PfnDeleteSamplers DeleteSamplers;
};

/// The process-wide table. Loads on first use; must be called with a current GL
/// context. See the note at the top of this header.
const Api& Get();

/// Explicit (re)load. `err` receives a human-readable reason on failure.
/// Returns true once the table is usable; safe to call again after a failure,
/// which lets a host call Initialize() before its context exists and
/// try again afterwards.
bool Load(std::string* err);

} // namespace argon::ui::gl
