#include "model_maker/opengl_render_backend.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// ── OpenGL types and constants (header-free, no GL/gl.h needed) ──
using GLenum = unsigned int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLint = int;
using GLsizei = int;
using GLuint = unsigned int;
using GLfloat = float;
using GLchar = char;
using GLsizeiptr = ptrdiff_t;
using GLintptr = ptrdiff_t;

namespace GLConst {
    constexpr GLenum FLOAT_TYPE = 0x1406;
    constexpr GLenum UNSIGNED_BYTE_TYPE = 0x1401;
    constexpr GLenum UNSIGNED_INT_TYPE = 0x1405;
    constexpr GLenum ARRAY_BUFFER = 0x8892;
    constexpr GLenum ELEMENT_ARRAY_BUFFER = 0x8893;
    constexpr GLenum STATIC_DRAW = 0x88E4;
    constexpr GLenum LINES = 0x0001;
    constexpr GLenum BLEND_MODE = 0x0BE2;
    constexpr GLenum SRC_ALPHA = 0x0302;
    constexpr GLenum ONE_MINUS_SRC_ALPHA = 0x0303;
    constexpr GLbitfield COLOR_BUFFER_BIT = 0x4000;
    constexpr GLbitfield DEPTH_BUFFER_BIT = 0x0100;
    constexpr GLenum VERTEX_SHADER = 0x8B31;
    constexpr GLenum FRAGMENT_SHADER = 0x8B30;
    constexpr GLenum COMPILE_STATUS = 0x8B81;
    constexpr GLenum LINK_STATUS = 0x8B82;
    constexpr GLboolean GL_FALSE = 0;
    constexpr GLboolean GL_TRUE = 1;
    // FBO
    constexpr GLenum FRAMEBUFFER = 0x8D40;
    constexpr GLenum RENDERBUFFER = 0x8D41;
    constexpr GLenum COLOR_ATTACHMENT0 = 0x8CE0;
    constexpr GLenum FRAMEBUFFER_COMPLETE = 0x8CD5;
    constexpr GLenum RGBA = 0x1908;
    constexpr GLenum BGRA_EXT = 0x80E1;  // GL_BGRA
} // namespace GLConst

// ── OpenGL function types ─────────────────────────────────────────
using PFNWGLCREATECONTEXTATTRIBSARBPROC = HGLRC (WINAPI *)(HDC, HGLRC, const int*);
using PFNGLGENVERTEXARRAYSPROC = void (APIENTRY *)(GLsizei, GLuint*);
using PFNGLBINDVERTEXARRAYPROC = void (APIENTRY *)(GLuint);
using PFNGLDELETEVERTEXARRAYSPROC = void (APIENTRY *)(GLsizei, const GLuint*);
using PFNGLGENBUFFERSPROC = void (APIENTRY *)(GLsizei, GLuint*);
using PFNGLBINDBUFFERPROC = void (APIENTRY *)(GLenum, GLuint);
using PFNGLBUFFERDATAPROC = void (APIENTRY *)(GLenum, GLsizeiptr, const void*, GLenum);
using PFNGLBUFFERSUBDATAPROC = void (APIENTRY *)(GLenum, GLintptr, GLsizeiptr, const void*);
using PFNGLDELETEBUFFERSPROC = void (APIENTRY *)(GLsizei, const GLuint*);
using PFNGLCREATESHADERPROC = GLuint (APIENTRY *)(GLenum);
using PFNGLSHADERSOURCEPROC = void (APIENTRY *)(GLuint, GLsizei, const GLchar* const*, const GLint*);
using PFNGLCOMPILESHADERPROC = void (APIENTRY *)(GLuint);
using PFNGLGETSHADERIVPROC = void (APIENTRY *)(GLuint, GLenum, GLint*);
using PFNGLGETSHADERINFOLOGPROC = void (APIENTRY *)(GLuint, GLsizei, GLsizei*, GLchar*);
using PFNGLCREATEPROGRAMPROC = GLuint (APIENTRY *)();
using PFNGLATTACHSHADERPROC = void (APIENTRY *)(GLuint, GLuint);
using PFNGLLINKPROGRAMPROC = void (APIENTRY *)(GLuint);
using PFNGLGETPROGRAMIVPROC = void (APIENTRY *)(GLuint, GLenum, GLint*);
using PFNGLGETPROGRAMINFOLOGPROC = void (APIENTRY *)(GLuint, GLsizei, GLsizei*, GLchar*);
using PFNGLUSEPROGRAMPROC = void (APIENTRY *)(GLuint);
using PFNGLDELETESHADERPROC = void (APIENTRY *)(GLuint);
using PFNGLDELETEPROGRAMPROC = void (APIENTRY *)(GLuint);
using PFNGLGETUNIFORMLOCATIONPROC = GLint (APIENTRY *)(GLuint, const GLchar*);
using PFNGLUNIFORMMATRIX4FVPROC = void (APIENTRY *)(GLint, GLsizei, GLboolean, const GLfloat*);
using PFNGLUNIFORM2FPROC = void (APIENTRY *)(GLint, GLfloat, GLfloat);
using PFNGLVERTEXATTRIBPOINTERPROC = void (APIENTRY *)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
using PFNGLENABLEVERTEXATTRIBARRAYPROC = void (APIENTRY *)(GLuint);
using PFNGLDISABLEVERTEXATTRIBARRAYPROC = void (APIENTRY *)(GLuint);
using PFNGLCLEARPROC = void (APIENTRY *)(GLbitfield);
using PFNGLCLEARCOLORPROC = void (APIENTRY *)(GLfloat, GLfloat, GLfloat, GLfloat);
using PFNGLVIEWPORTPROC = void (APIENTRY *)(GLint, GLint, GLsizei, GLsizei);
using PFNGLDRAWELEMENTSPROC = void (APIENTRY *)(GLenum, GLsizei, GLenum, const void*);
using PFNGLENABLEPROC = void (APIENTRY *)(GLenum);
using PFNGLDISABLEPROC = void (APIENTRY *)(GLenum);
using PFNGLBLENDFUNCPROC = void (APIENTRY *)(GLenum, GLenum);
using PFNGLFINISHPROC = void (APIENTRY *)();
// FBO functions
using PFNGLGENFRAMEBUFFERSPROC = void (APIENTRY *)(GLsizei, GLuint*);
using PFNGLBINDFRAMEBUFFERPROC = void (APIENTRY *)(GLenum, GLuint);
using PFNGLFRAMEBUFFERTEXTURE2DPROC = void (APIENTRY *)(GLenum, GLenum, GLenum, GLuint, GLint);
using PFNGLCHECKFRAMEBUFFERSTATUSPROC = GLenum (APIENTRY *)(GLenum);
using PFNGLGENRENDERBUFFERSPROC = void (APIENTRY *)(GLsizei, GLuint*);
using PFNGLBINDRENDERBUFFERPROC = void (APIENTRY *)(GLenum, GLuint);
using PFNGLRENDERBUFFERSTORAGEPROC = void (APIENTRY *)(GLenum, GLenum, GLsizei, GLsizei);
using PFNGLFRAMEBUFFERRENDERBUFFERPROC = void (APIENTRY *)(GLenum, GLenum, GLenum, GLuint);
using PFNGLDELETEFRAMEBUFFERSPROC = void (APIENTRY *)(GLsizei, const GLuint*);
using PFNGLDELETERENDERBUFFERSPROC = void (APIENTRY *)(GLsizei, const GLuint*);
using PFNGLREADPIXELSPROC = void (APIENTRY *)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
using PFNGLREADBUFFERPROC = void (APIENTRY *)(GLenum);

// ── Dynamically loaded function pointers ──────────────────────────
namespace {
PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = nullptr;
PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = nullptr;
PFNGLBINDVERTEXARRAYPROC glBindVertexArray = nullptr;
PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays = nullptr;
PFNGLGENBUFFERSPROC glGenBuffers = nullptr;
PFNGLBINDBUFFERPROC glBindBuffer = nullptr;
PFNGLBUFFERDATAPROC glBufferData = nullptr;
PFNGLBUFFERSUBDATAPROC glBufferSubData = nullptr;
PFNGLDELETEBUFFERSPROC glDeleteBuffers = nullptr;
PFNGLCREATESHADERPROC glCreateShader = nullptr;
PFNGLSHADERSOURCEPROC glShaderSource = nullptr;
PFNGLCOMPILESHADERPROC glCompileShader = nullptr;
PFNGLGETSHADERIVPROC glGetShaderiv = nullptr;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = nullptr;
PFNGLCREATEPROGRAMPROC glCreateProgram = nullptr;
PFNGLATTACHSHADERPROC glAttachShader = nullptr;
PFNGLLINKPROGRAMPROC glLinkProgram = nullptr;
PFNGLGETPROGRAMIVPROC glGetProgramiv = nullptr;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = nullptr;
PFNGLUSEPROGRAMPROC glUseProgram = nullptr;
PFNGLDELETESHADERPROC glDeleteShader = nullptr;
PFNGLDELETEPROGRAMPROC glDeleteProgram = nullptr;
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = nullptr;
PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = nullptr;
PFNGLUNIFORM2FPROC glUniform2f = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = nullptr;
PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray = nullptr;
PFNGLCLEARPROC glClear = nullptr;
PFNGLCLEARCOLORPROC glClearColor = nullptr;
PFNGLVIEWPORTPROC glViewport = nullptr;
PFNGLDRAWELEMENTSPROC glDrawElements = nullptr;
PFNGLENABLEPROC glEnable = nullptr;
PFNGLDISABLEPROC glDisable = nullptr;
PFNGLBLENDFUNCPROC glBlendFunc = nullptr;
PFNGLFINISHPROC glFinish = nullptr;
// FBO
PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers = nullptr;
PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D = nullptr;
PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus = nullptr;
PFNGLGENRENDERBUFFERSPROC glGenRenderbuffers = nullptr;
PFNGLBINDRENDERBUFFERPROC glBindRenderbuffer = nullptr;
PFNGLRENDERBUFFERSTORAGEPROC glRenderbufferStorage = nullptr;
PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer = nullptr;
PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers = nullptr;
PFNGLDELETERENDERBUFFERSPROC glDeleteRenderbuffers = nullptr;
PFNGLREADPIXELSPROC glReadPixels = nullptr;
PFNGLREADBUFFERPROC glReadBuffer = nullptr;

HMODULE openglModule = nullptr;
bool hasWindowsGL = false;

// ── Shader sources ───────────────────────────────────────────────
const char* vertexShaderSource = R"GLSL(
#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec4 colorIn;
uniform mat4 mvp;
uniform vec2 screenSize;
out vec4 fragColor;
void main() {
    vec4 clip = mvp * vec4(position, 1.0);
    vec2 pixelCorrect = vec2(1.0) / screenSize;
    clip.xy += clip.w * pixelCorrect * 0.5;
    gl_Position = clip;
    fragColor = colorIn;
}
)GLSL";

const char* fragmentShaderSource = R"GLSL(
#version 330 core
in vec4 fragColor;
out vec4 outColor;
void main() {
    outColor = fragColor;
}
)GLSL";

// ── WGL / loading helpers ────────────────────────────────────────

bool loadWglExtensions(HDC hdc) {
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int format = ChoosePixelFormat(hdc, &pfd);
    if (!format) return false;
    if (!SetPixelFormat(hdc, format, &pfd)) {
        if (GetPixelFormat(hdc) == 0) return false;
    }

    HGLRC tempContext = wglCreateContext(hdc);
    if (!tempContext) return false;
    if (!wglMakeCurrent(hdc, tempContext)) { wglDeleteContext(tempContext); return false; }

    wglCreateContextAttribsARB = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
        wglGetProcAddress("wglCreateContextAttribsARB"));

    auto loadProc = [](const char* name) -> void* {
        void* proc = reinterpret_cast<void*>(wglGetProcAddress(name));
        if (!proc && openglModule) proc = reinterpret_cast<void*>(GetProcAddress(openglModule, name));
        return proc;
    };

    #define LOAD_GL(fn) fn = reinterpret_cast<decltype(fn)>(loadProc(#fn))
    LOAD_GL(glGenVertexArrays);
    LOAD_GL(glBindVertexArray);
    LOAD_GL(glDeleteVertexArrays);
    LOAD_GL(glGenBuffers);
    LOAD_GL(glBindBuffer);
    LOAD_GL(glBufferData);
    LOAD_GL(glBufferSubData);
    LOAD_GL(glDeleteBuffers);
    LOAD_GL(glCreateShader);
    LOAD_GL(glShaderSource);
    LOAD_GL(glCompileShader);
    LOAD_GL(glGetShaderiv);
    LOAD_GL(glGetShaderInfoLog);
    LOAD_GL(glCreateProgram);
    LOAD_GL(glAttachShader);
    LOAD_GL(glLinkProgram);
    LOAD_GL(glGetProgramiv);
    LOAD_GL(glGetProgramInfoLog);
    LOAD_GL(glUseProgram);
    LOAD_GL(glDeleteShader);
    LOAD_GL(glDeleteProgram);
    LOAD_GL(glGetUniformLocation);
    LOAD_GL(glUniformMatrix4fv);
    LOAD_GL(glUniform2f);
    LOAD_GL(glVertexAttribPointer);
    LOAD_GL(glEnableVertexAttribArray);
    LOAD_GL(glDisableVertexAttribArray);
    LOAD_GL(glClear);
    LOAD_GL(glClearColor);
    LOAD_GL(glViewport);
    LOAD_GL(glDrawElements);
    LOAD_GL(glEnable);
    LOAD_GL(glDisable);
    LOAD_GL(glBlendFunc);
    LOAD_GL(glFinish);
    // FBO
    LOAD_GL(glGenFramebuffers);
    LOAD_GL(glBindFramebuffer);
    LOAD_GL(glFramebufferTexture2D);
    LOAD_GL(glCheckFramebufferStatus);
    LOAD_GL(glGenRenderbuffers);
    LOAD_GL(glBindRenderbuffer);
    LOAD_GL(glRenderbufferStorage);
    LOAD_GL(glFramebufferRenderbuffer);
    LOAD_GL(glDeleteFramebuffers);
    LOAD_GL(glDeleteRenderbuffers);
    LOAD_GL(glReadPixels);
    LOAD_GL(glReadBuffer);
    #undef LOAD_GL

    wglMakeCurrent(hdc, nullptr);
    wglDeleteContext(tempContext);
    return glCreateShader && glCreateProgram && glGenVertexArrays && glGenBuffers
           && glGenFramebuffers && glBindFramebuffer;
}

HGLRC createGL33Context(HDC hdc) {
    if (!wglCreateContextAttribsARB) return nullptr;
    const int attribs[] = { 0x2091, 3, 0x2092, 3, 0x9126, 0x0001, 0, 0 };
    return wglCreateContextAttribsARB(hdc, nullptr, attribs);
}

bool compileShader(GLenum type, const char* source, GLuint& shader) {
    shader = glCreateShader(type);
    if (!shader) return false;
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GLConst::GL_FALSE;
    glGetShaderiv(shader, GLConst::COMPILE_STATUS, &compiled);
    if (compiled != GLConst::GL_TRUE) { glDeleteShader(shader); shader = 0; return false; }
    return true;
}

bool linkProgram(GLuint vs, GLuint fs, GLuint& program, GLint& mvpLoc, GLint& screenLoc) {
    program = glCreateProgram();
    if (!program) return false;
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    GLint linked = GLConst::GL_FALSE;
    glGetProgramiv(program, GLConst::LINK_STATUS, &linked);
    if (linked != GLConst::GL_TRUE) { glDeleteProgram(program); program = 0; return false; }
    mvpLoc = glGetUniformLocation(program, "mvp");
    screenLoc = glGetUniformLocation(program, "screenSize");
    return true;
}

} // anonymous namespace

namespace mm {

// ── MVP matrix computation ───────────────────────────────────────
namespace {

void computeMVPMatrix(const Camera& camera, int width, int height, float* out) {
    Vec3 rX = camera.viewTransform({1.0, 0.0, 0.0});
    Vec3 rY = camera.viewTransform({0.0, 1.0, 0.0});
    Vec3 rZ = camera.viewTransform({0.0, 0.0, 1.0});

    // ppu'yu kameradan ORNEKLE: sabit 65.0 degeri viewport buyuklugune ve zoom
    // gecmisine gore degisen gercek GDI olcegiyle uyusmuyor, GL cizgileri grid'e
    // gore yanlis yerlere dusuyordu ("koordinatlar bozulmus" + GDI geri bildirim
    // katmaninin ortulmesi). Iki izdusum farki = tam ppu (kamera ic yapisindan
    // bagimsiz, 2B/3B her iki modda).
    const Vec2 originScreen = camera.project({0.0, 0.0, 0.0}, width, height);
    const Vec2 unitXScreen = camera.project({1.0, 0.0, 0.0}, width, height);
    const Vec2 unitYScreen = camera.project({0.0, 1.0, 0.0}, width, height);
    const double scale = std::max(
        std::hypot(unitXScreen.x - originScreen.x, unitXScreen.y - originScreen.y),
        std::hypot(unitYScreen.x - originScreen.x, unitYScreen.y - originScreen.y));
    Vec3 rotCenter{
        (static_cast<double>(width) * 0.5 - originScreen.x) / scale,
        (originScreen.y - static_cast<double>(height) * 0.5) / scale,
        0.0
    };

    float view[16] = {
        static_cast<float>(rX.x), static_cast<float>(rX.y), static_cast<float>(rX.z), 0,
        static_cast<float>(rY.x), static_cast<float>(rY.y), static_cast<float>(rY.z), 0,
        static_cast<float>(rZ.x), static_cast<float>(rZ.y), static_cast<float>(rZ.z), 0,
        static_cast<float>(-rotCenter.x), static_cast<float>(-rotCenter.y),
        static_cast<float>(-rotCenter.z), 1
    };

    float scaleX = static_cast<float>(2.0 * scale / static_cast<double>(width));
    float scaleY = static_cast<float>(-2.0 * scale / static_cast<double>(height));
    float proj[16] = { scaleX, 0, 0, 0, 0, scaleY, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

    std::memset(out, 0, 16 * sizeof(float));
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            float sum = 0;
            for (int k = 0; k < 4; ++k)
                sum += proj[k * 4 + r] * view[c * 4 + k];
            out[c * 4 + r] = sum;
        }
    }
}

std::uint32_t toRGBA8(std::uint32_t rgb24) {
    const auto r = static_cast<std::uint8_t>((rgb24 >> 16) & 0xFF);
    const auto g = static_cast<std::uint8_t>((rgb24 >> 8) & 0xFF);
    const auto b = static_cast<std::uint8_t>(rgb24 & 0xFF);
    return (0xFFu << 24) | (static_cast<std::uint32_t>(b) << 16) |
           (static_cast<std::uint32_t>(g) << 8) | r;
}

using GpuLineVertex = GpuLineBatch::GpuVertex;

} // anonymous namespace

// ── Constructor / Destructor ──────────────────────────────────────
OpenGLRenderBackend::OpenGLRenderBackend() = default;
OpenGLRenderBackend::~OpenGLRenderBackend() { shutdown(); }

// ── Lifecycle ────────────────────────────────────────────────────
bool OpenGLRenderBackend::initialize(void* windowHandle, int initialWidth, int initialHeight) {
    if (initialized_) return true;

    windowHandle_ = windowHandle;
    width_ = initialWidth;
    height_ = initialHeight;
    HWND hwnd = static_cast<HWND>(windowHandle);

    openglModule = LoadLibraryA("opengl32.dll");
    if (!openglModule) return false;

    HDC hdc = GetDC(hwnd);
    if (!hdc) { FreeLibrary(openglModule); openglModule = nullptr; return false; }
    deviceContext_ = hdc;

    if (!loadWglExtensions(hdc)) {
        ReleaseDC(hwnd, hdc); deviceContext_ = nullptr;
        FreeLibrary(openglModule); openglModule = nullptr;
        return false;
    }

    glContext_ = createGL33Context(hdc);
    if (!glContext_) {
        ReleaseDC(hwnd, hdc); deviceContext_ = nullptr;
        FreeLibrary(openglModule); openglModule = nullptr;
        return false;
    }

    if (!wglMakeCurrent(hdc, static_cast<HGLRC>(glContext_))) {
        wglDeleteContext(static_cast<HGLRC>(glContext_)); glContext_ = nullptr;
        ReleaseDC(hwnd, hdc); deviceContext_ = nullptr;
        FreeLibrary(openglModule); openglModule = nullptr;
        return false;
    }

    if (!compileShaders()) {
        wglMakeCurrent(hdc, nullptr);
        wglDeleteContext(static_cast<HGLRC>(glContext_)); glContext_ = nullptr;
        ReleaseDC(hwnd, hdc); deviceContext_ = nullptr;
        FreeLibrary(openglModule); openglModule = nullptr;
        return false;
    }

    // Context'i current birakma: ayni pencerede GL context'i current iken GDI
    // cizimi (BeginPaint) bazi suruculerde bozuluyor — GDI varsayilan modda
    // cizgiler tamamen kayboluyordu. beginFrame() cizim icin tekrar alir,
    // endFrame()'den sonra yine birakir.
    wglMakeCurrent(hdc, nullptr);

    // Create FBO for offscreen rendering
    ensureFbo(width_, height_);

    glClearColor(0.0588f, 0.0706f, 0.102f, 1.0f); // RGB(15,18,26)
    initialized_ = true;
    hasWindowsGL = true;
    return true;
}

void OpenGLRenderBackend::shutdown() {
    if (!initialized_) return;
    HDC hdc = static_cast<HDC>(deviceContext_);
    if (hdc) wglMakeCurrent(hdc, nullptr);
    cleanupGL();
    if (glContext_) { wglDeleteContext(static_cast<HGLRC>(glContext_)); glContext_ = nullptr; }
    if (deviceContext_ && windowHandle_) {
        ReleaseDC(static_cast<HWND>(windowHandle_), static_cast<HDC>(deviceContext_));
        deviceContext_ = nullptr;
    }
    if (openglModule) { FreeLibrary(openglModule); openglModule = nullptr; }
    initialized_ = false;
    hasWindowsGL = false;
}

void OpenGLRenderBackend::resize(int width, int height) {
    if (width != width_ || height != height_) {
        width_ = width; height_ = height;
        fboDirty_ = true;  // defer FBO recreation to beginFrame
    }
}

// ── FBO management ────────────────────────────────────────────────
void OpenGLRenderBackend::ensureFbo(int width, int height) {
    if (fboWidth_ == width && fboHeight_ == height && fbo_ != 0) return;

    // Delete old FBO resources
    if (fbo_ != 0) { glDeleteFramebuffers(1, &fbo_); fbo_ = 0; }
    if (fboColor_ != 0) { glDeleteRenderbuffers(1, &fboColor_); fboColor_ = 0; }
    if (fboDepth_ != 0) { glDeleteRenderbuffers(1, &fboDepth_); fboDepth_ = 0; }

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GLConst::FRAMEBUFFER, fbo_);

    // Color attachment
    glGenRenderbuffers(1, &fboColor_);
    glBindRenderbuffer(GLConst::RENDERBUFFER, fboColor_);
    glRenderbufferStorage(GLConst::RENDERBUFFER, GLConst::RGBA, width, height);
    glFramebufferRenderbuffer(GLConst::FRAMEBUFFER, GLConst::COLOR_ATTACHMENT0,
                              GLConst::RENDERBUFFER, fboColor_);

    // Depth attachment
    glGenRenderbuffers(1, &fboDepth_);
    glBindRenderbuffer(GLConst::RENDERBUFFER, fboDepth_);
    glRenderbufferStorage(GLConst::RENDERBUFFER,
                          0x81A6 /*GL_DEPTH_COMPONENT24*/, width, height);
    glFramebufferRenderbuffer(GLConst::FRAMEBUFFER, 0x8D00 /*GL_DEPTH_ATTACHMENT*/,
                              GLConst::RENDERBUFFER, fboDepth_);

    GLenum status = glCheckFramebufferStatus(GLConst::FRAMEBUFFER);
    if (status != GLConst::FRAMEBUFFER_COMPLETE) {
        glDeleteFramebuffers(1, &fbo_); fbo_ = 0;
        glDeleteRenderbuffers(1, &fboColor_); fboColor_ = 0;
        glDeleteRenderbuffers(1, &fboDepth_); fboDepth_ = 0;
    }

    glBindFramebuffer(GLConst::FRAMEBUFFER, 0);
    fboWidth_ = width;
    fboHeight_ = height;
}

// ── Frame management ─────────────────────────────────────────────
bool OpenGLRenderBackend::beginFrame(const FrameInfo& /* info */) {
    if (!initialized_) return false;
    HDC hdc = static_cast<HDC>(deviceContext_);
    if (!wglMakeCurrent(hdc, static_cast<HGLRC>(glContext_))) return false;

    // Render directly to window framebuffer on top of GDI content
    glBindFramebuffer(GLConst::FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
    // No clear — GDI already drew background

    if (!perFrameMetricsReset_) {
        drawCalls_ = 0; uploadBytes_ = 0; renderedLines_ = 0;
        perFrameMetricsReset_ = true;
    }
    return true;
}

void OpenGLRenderBackend::endFrame() {
    if (!initialized_) return;
    glFinish();
    // Context'i SERBEST BIRAK: current birakilirsa ayni UI thread'inde calisan
    // Qt kendi cizimini bu GL context'i altinda yapar ve ekran bozulur
    // ("F9'a basinca ekran bozuldu"). beginFrame() bir sonraki karede tekrar
    // alir — GL yalnizca cizim suresince current kalir.
    wglMakeCurrent(static_cast<HDC>(deviceContext_), nullptr);
    perFrameMetricsReset_ = false;
}

// ── Blit buffer ───────────────────────────────────────────────────
void OpenGLRenderBackend::ensureBlitBuffer(int width, int height) {
    std::size_t needed = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    if (blitBuffer_.size() != needed) {
        blitBuffer_.resize(needed);
        if (blitDib_) { DeleteObject(static_cast<HBITMAP>(blitDib_)); blitDib_ = nullptr; }
    }
}

bool OpenGLRenderBackend::blitToDC(void* target, int width, int height) {
    if (blitBuffer_.empty() || width <= 0 || height <= 0) return false;
    HDC hdcTarget = static_cast<HDC>(target);

    std::size_t needed = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    if (blitBuffer_.size() < needed) return false;

    // glReadPixels reads bottom-to-top; use positive height for bottom-up DIB
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdcTarget, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hBmp || !bits) {
        if (hBmp) DeleteObject(hBmp);
        return false;
    }

    std::memcpy(bits, blitBuffer_.data(), needed);

    HDC memDC = CreateCompatibleDC(hdcTarget);
    HGDIOBJ oldBmp = SelectObject(memDC, hBmp);
    // AlphaBlend: seffaf FBO zemini (A=0) alttaki GDI icerigini korur,
    // opak cizgiler (A=255) ustune biner.
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, 0};
    AlphaBlend(hdcTarget, 0, 0, width, height, memDC, 0, 0, width, height, blend);
    SelectObject(memDC, oldBmp);
    DeleteDC(memDC);
    DeleteObject(hBmp);
    return true;
}

bool OpenGLRenderBackend::renderBatchToDc(
    const std::vector<std::pair<std::size_t, WireframeModel>>& models,
    const Camera& camera, int width, int height, void* targetHdc) {
    if (!initialized_ || models.empty()) return false;
    static int glDiagCount = 0;
    const auto glDiag = [](const char* step) {
        if (glDiagCount >= 8) return;
        FILE* diag = fopen("model-maker-render.log", "a");
        if (diag) { fprintf(diag, "GLDIAG %d %s\n", glDiagCount, step); fclose(diag); }
    };
    HDC hdc = static_cast<HDC>(deviceContext_);
    if (!wglMakeCurrent(hdc, static_cast<HGLRC>(glContext_))) {
        ++glDiagCount; glDiag("MAKECURRENT-FAIL"); return false;
    }
    glDiag("CURRENT-OK");

    drawCalls_ = 0; uploadBytes_ = 0; renderedLines_ = 0;

    ensureFbo(width, height);
    if (fbo_ == 0) { ++glDiagCount; glDiag("FBO-FAIL"); wglMakeCurrent(hdc, nullptr); return false; }
    glDiag("FBO-OK");
    glBindFramebuffer(GLConst::FRAMEBUFFER, fbo_);
    glViewport(0, 0, width, height);
    // Seffaf zemin: GDI icerigi AlphaBlend ile alttan gorunur, yalniz
    // cizgiler kompozite edilir.
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GLConst::COLOR_BUFFER_BIT);

    ensureBatch(models);
    glDiag("BATCH-OK");
    if (lineBatch_.indexCount != 0) {
        renderBatch(lineBatch_, camera, width, height);
        drawCalls_ = 1;
        renderedLines_ = lineBatch_.indexCount / 2;
    }

    glFinish();
    ensureBlitBuffer(width, height);
    glReadPixels(0, 0, width, height, GLConst::BGRA_EXT, GLConst::UNSIGNED_BYTE_TYPE,
                 blitBuffer_.data());
    // Readback orneklemesi: merkez piksel + kose + ilk saydamlik taramasi
    if (glDiagCount < 5 && !blitBuffer_.empty()) {
        const auto px = [&](std::size_t idx) {
            return static_cast<unsigned>(blitBuffer_[idx]);
        };
        const std::size_t center = (static_cast<std::size_t>(height / 2) * width + width / 2) * 4;
        int opaque = 0; const std::size_t scan = std::min<std::size_t>(blitBuffer_.size(), 4 * 20000);
        for (std::size_t i = 3; i < scan; i += 4) if (blitBuffer_[i] > 32) ++opaque;
        FILE* diag = fopen("model-maker-render.log", "a");
        if (diag) {
            fprintf(diag,
                "GLDIAG %d READBACK center=%u,%u,%u,%u opaqueSampled=%d/%zu lines=%zu\n",
                glDiagCount, px(center), px(center + 1), px(center + 2), px(center + 3),
                opaque, scan / 4, renderedLines_);
            fclose(diag);
        }
    }
    glBindFramebuffer(GLConst::FRAMEBUFFER, 0);
    wglMakeCurrent(hdc, nullptr);

    const bool blitOk = blitToDC(targetHdc, width, height);
    glDiag(blitOk ? "BLIT-OK" : "BLIT-FAIL");
    ++glDiagCount;
    return blitOk;
}

// ── Shader compilation ───────────────────────────────────────────
bool OpenGLRenderBackend::compileShaders() {
    GLuint vs = 0, fs = 0;
    if (!compileShader(GLConst::VERTEX_SHADER, vertexShaderSource, vs)) return false;
    if (!compileShader(GLConst::FRAGMENT_SHADER, fragmentShaderSource, fs)) {
        glDeleteShader(vs); return false;
    }
    GLuint program = 0;
    GLint mvpLoc = -1, screenLoc = -1;
    if (!linkProgram(vs, fs, program, mvpLoc, screenLoc)) {
        glDeleteShader(vs); glDeleteShader(fs); return false;
    }
    glDeleteShader(vs); glDeleteShader(fs);
    shaderProgram_ = program;
    uniformMvp_ = mvpLoc;
    uniformScreenSize_ = screenLoc;
    return true;
}

// ── GPU batch rendering ──────────────────────────────────────────
void OpenGLRenderBackend::renderWireframeBatch(
    const std::vector<std::pair<std::size_t, WireframeModel>>& models,
    const Camera& camera) {
    if (!initialized_ || models.empty()) return;
    ensureBatch(models);
    if (lineBatch_.indexCount == 0) return;
    renderBatch(lineBatch_, camera, width_, height_);
    perFrameMetricsReset_ = false;
    drawCalls_ = 1;
    renderedLines_ = lineBatch_.indexCount / 2;
}

void OpenGLRenderBackend::ensureBatch(
    const std::vector<std::pair<std::size_t, WireframeModel>>& models) {
    std::size_t totalEdges = 0;
    for (const auto& [idx, model] : models) totalEdges += model.edges().size();
    std::size_t tag = models.size() ^ (totalEdges << 16);
    if (tag == lineBatch_.versionTag && !lineBatch_.dirty) return;

    lineBatch_.versionTag = tag;
    lineBatch_.dirty = true;
    lineBatch_.modelCount = models.size();

    std::size_t totalVertices = 0, totalIndices = 0;
    for (const auto& [idx, model] : models) {
        if (model.edges().empty()) continue;
        totalVertices += model.vertices().size();
        totalIndices += model.edges().size() * 2;
    }

    lineBatch_.vertices.clear();
    lineBatch_.vertices.reserve(totalVertices);
    lineBatch_.indices.clear();
    lineBatch_.indices.reserve(totalIndices);

    std::size_t vertexBase = 0;
    for (const auto& [modelIdx, model] : models) {
        const auto& vertices = model.vertices();
        const auto& edges = model.edges();
        if (vertices.empty() || edges.empty()) continue;
        const auto props = model.properties();
        const std::uint32_t rgba = toRGBA8(props.effectiveColor);
        for (const auto& v : vertices) {
            GpuLineVertex gv;
            gv.x = static_cast<float>(v.x);
            gv.y = static_cast<float>(v.y);
            gv.z = static_cast<float>(v.z);
            gv.color = rgba;
            lineBatch_.vertices.push_back(gv);
        }
        for (const auto& edge : edges) {
            lineBatch_.indices.push_back(static_cast<std::uint32_t>(vertexBase + edge.from));
            lineBatch_.indices.push_back(static_cast<std::uint32_t>(vertexBase + edge.to));
        }
        vertexBase += vertices.size();
    }
    lineBatch_.indexCount = lineBatch_.indices.size();
    uploadBatch(lineBatch_);
}

void OpenGLRenderBackend::uploadBatch(GpuLineBatch& batch) {
    if (batch.vao == 0) {
        glGenVertexArrays(1, &batch.vao);
        glGenBuffers(1, &batch.vbo);
        glGenBuffers(1, &batch.ebo);
        ++rebuildCount_;
    }
    glBindVertexArray(batch.vao);
    glBindBuffer(GLConst::ARRAY_BUFFER, batch.vbo);
    std::size_t vertexBytes = batch.vertices.size() * sizeof(GpuLineBatch::GpuVertex);
    if (!batch.vertices.empty()) {
        glBufferData(GLConst::ARRAY_BUFFER, static_cast<GLsizeiptr>(vertexBytes),
                     batch.vertices.data(), GLConst::STATIC_DRAW);
        uploadBytes_ += vertexBytes;
    }
    glVertexAttribPointer(0, 3, GLConst::FLOAT_TYPE, GLConst::GL_FALSE,
                          sizeof(GpuLineBatch::GpuVertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GLConst::UNSIGNED_BYTE_TYPE, GLConst::GL_TRUE,
                          sizeof(GpuLineBatch::GpuVertex), reinterpret_cast<void*>(12));
    glEnableVertexAttribArray(1);
    glBindBuffer(GLConst::ELEMENT_ARRAY_BUFFER, batch.ebo);
    std::size_t indexBytes = batch.indices.size() * sizeof(std::uint32_t);
    if (!batch.indices.empty()) {
        glBufferData(GLConst::ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indexBytes),
                     batch.indices.data(), GLConst::STATIC_DRAW);
        uploadBytes_ += indexBytes;
    }
    glBindVertexArray(0);
    batch.dirty = false;
}

void OpenGLRenderBackend::renderBatch(const GpuLineBatch& batch, const Camera& camera,
                                       int width, int height) {
    if (batch.indexCount == 0 || batch.vao == 0) return;
    glUseProgram(shaderProgram_);
    float mvp[16];
    computeMVPMatrix(camera, width, height, mvp);
    static int mvpDiagCount = 0;
    if (mvpDiagCount < 5) {
        ++mvpDiagCount;
        const float vx = batch.vertices.empty() ? 0.0f : batch.vertices[0].x;
        const float vy = batch.vertices.empty() ? 0.0f : batch.vertices[0].y;
        const float vz = batch.vertices.empty() ? 0.0f : batch.vertices[0].z;
        const float cx = mvp[0] * vx + mvp[4] * vy + mvp[8] * vz + mvp[12];
        const float cy = mvp[1] * vx + mvp[5] * vy + mvp[9] * vz + mvp[13];
        const float cw = mvp[3] * vx + mvp[7] * vy + mvp[11] * vz + mvp[15];
        FILE* diag = fopen("model-maker-render.log", "a");
        if (diag) {
            fprintf(diag, "GLDIAG MVP scaleXY=(%.3f,%.3f) trans=(%.1f,%.1f) "
                    "v0=(%.2f,%.2f,%.2f) clip=(%.2f,%.2f,w=%.2f) idx=%zu\n",
                    mvp[0], mvp[5], mvp[12], mvp[13], vx, vy, vz,
                    cw != 0.0f ? cx / cw : cx, cw != 0.0f ? cy / cw : cy, cw,
                    batch.indexCount);
            fclose(diag);
        }
    }
    glUniformMatrix4fv(uniformMvp_, 1, GLConst::GL_FALSE, mvp);
    glUniform2f(uniformScreenSize_, static_cast<GLfloat>(width), static_cast<GLfloat>(height));
    glEnable(GLConst::BLEND_MODE);
    glBlendFunc(GLConst::SRC_ALPHA, GLConst::ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(batch.vao);
    glDrawElements(GLConst::LINES, static_cast<GLsizei>(batch.indexCount),
                   GLConst::UNSIGNED_INT_TYPE, nullptr);
    glBindVertexArray(0);
    glDisable(GLConst::BLEND_MODE);
    glUseProgram(0);
}

// ── Cleanup ──────────────────────────────────────────────────────
void OpenGLRenderBackend::cleanupGL() {
    if (!hasWindowsGL) return;
    if (lineBatch_.vao) { glDeleteVertexArrays(1, &lineBatch_.vao); lineBatch_.vao = 0; }
    if (lineBatch_.vbo) { glDeleteBuffers(1, &lineBatch_.vbo); lineBatch_.vbo = 0; }
    if (lineBatch_.ebo) { glDeleteBuffers(1, &lineBatch_.ebo); lineBatch_.ebo = 0; }
    if (shaderProgram_) { glDeleteProgram(shaderProgram_); shaderProgram_ = 0; }
    if (fbo_) { glDeleteFramebuffers(1, &fbo_); fbo_ = 0; }
    if (fboColor_) { glDeleteRenderbuffers(1, &fboColor_); fboColor_ = 0; }
    if (fboDepth_) { glDeleteRenderbuffers(1, &fboDepth_); fboDepth_ = 0; }
    fboWidth_ = fboHeight_ = 0;
}

// ── GDI-style primitives (stubs) ──
RenderPenHandle OpenGLRenderBackend::createPen(std::uint32_t, int, RenderPenStyle) { return {0}; }
RenderBrushHandle OpenGLRenderBackend::createSolidBrush(std::uint32_t) { return {0}; }
RenderFontHandle OpenGLRenderBackend::createFont(int, bool, const wchar_t*) { return {0}; }
void OpenGLRenderBackend::deletePen(RenderPenHandle) {}
void OpenGLRenderBackend::deleteBrush(RenderBrushHandle) {}
void OpenGLRenderBackend::deleteFont(RenderFontHandle) {}
void OpenGLRenderBackend::drawLine(RenderPoint, RenderPoint, RenderPenHandle) {}
void OpenGLRenderBackend::drawPolyline(const RenderPoint*, int, RenderPenHandle) {}
void OpenGLRenderBackend::drawPolygon(const RenderPoint*, int, RenderBrushHandle, RenderPenHandle) {}
void OpenGLRenderBackend::drawRectangle(const RenderRect&, RenderPenHandle) {}
void OpenGLRenderBackend::drawFilledRect(const RenderRect&, RenderBrushHandle) {}
void OpenGLRenderBackend::drawEllipse(RenderPoint, int, int, RenderPenHandle) {}
void OpenGLRenderBackend::drawRoundedRect(const RenderRect&, int, RenderBrushHandle, RenderPenHandle) {}
void OpenGLRenderBackend::drawAlphaPolygon(const RenderPoint*, int, std::uint32_t, int) {}
void OpenGLRenderBackend::drawAlphaRect(const RenderRect&, std::uint32_t, int) {}
void OpenGLRenderBackend::drawText(int, int, const wchar_t*, std::uint32_t, RenderFontHandle) {}
bool OpenGLRenderBackend::presentRasterZoom(const FrameInfo&) { return false; }

std::unique_ptr<IRenderBackend> createOpenGLRenderBackend() {
    return std::make_unique<OpenGLRenderBackend>();
}

} // namespace mm
