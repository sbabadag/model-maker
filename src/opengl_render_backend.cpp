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
    constexpr GLenum DYNAMIC_DRAW = 0x88E8;
    constexpr GLenum UNIFORM_BUFFER = 0x8A11;
    constexpr GLenum LINES = 0x0001;
    constexpr GLenum TRIANGLES = 0x0004;
    constexpr GLenum DEPTH_TEST = 0x0B71;
    constexpr GLenum DEPTH_FUNC = 0x0B74;
    constexpr GLenum LEQUAL = 0x0203;
    constexpr GLenum POLYGON_OFFSET_FILL = 0x8037;
    constexpr GLenum PIXEL_PACK_BUFFER = 0x88EB;
    constexpr GLenum STREAM_READ = 0x88E1;
    constexpr GLenum READ_ONLY = 0x88B8;
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
using PFNGLUNIFORM1FPROC = void (APIENTRY *)(GLint, GLfloat);
using PFNGLDEPTHFUNCPROC = void (APIENTRY *)(GLenum);
using PFNGLDEPTHMASKPROC = void (APIENTRY *)(GLboolean);
using PFNGLPOLYGONOFFSETPROC = void (APIENTRY *)(GLfloat, GLfloat);
using PFNGLMAPBUFFERPROC = void* (APIENTRY *)(GLenum, GLenum);
using PFNGLUNMAPBUFFERPROC = GLboolean (APIENTRY *)(GLenum);
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
// MinGW basliklarinda PFNGLBINDBUFFERBASEPROC tanimli degil — elle bildir.
using PFNGLBINDBUFFERBASEPROC = void(APIENTRY*)(GLenum, GLuint, GLuint);
PFNGLBINDBUFFERBASEPROC glBindBufferBase = nullptr;
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
PFNGLUNIFORM1FPROC glUniform1f = nullptr;
PFNGLDEPTHFUNCPROC glDepthFunc = nullptr;
PFNGLDEPTHMASKPROC glDepthMask = nullptr;
PFNGLPOLYGONOFFSETPROC glPolygonOffset = nullptr;
PFNGLMAPBUFFERPROC glMapBuffer = nullptr;
PFNGLUNMAPBUFFERPROC glUnmapBuffer = nullptr;
PFNGLFINISHPROC glFinish = nullptr;
// MinGW basliklarinda PFNGLGETERRORPROC tanimli degil — elle bildir.
using PFNGLGETERRORPROC = GLenum(APIENTRY*)(void);
PFNGLGETERRORPROC glGetError = nullptr;
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
layout(std140) uniform CameraBlock {
    mat4 mvp; // F3: UBO — kamera matrisi kalici tamponda
};
out vec4 fragColor;
void main() {
    gl_Position = mvp * vec4(position, 1.0);
    fragColor = colorIn;
}
)GLSL";

const char* fragmentShaderSource = R"GLSL(
#version 330 core
in vec4 fragColor;
layout(location = 0) out vec4 outColor;
uniform float uAlpha;
void main() {
    outColor = vec4(fragColor.rgb, fragColor.a * uAlpha);
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
    LOAD_GL(glBindBufferBase);
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
    LOAD_GL(glUniform1f);
    LOAD_GL(glDepthFunc);
    LOAD_GL(glDepthMask);
    LOAD_GL(glPolygonOffset);
    LOAD_GL(glMapBuffer);
    LOAD_GL(glUnmapBuffer);
    LOAD_GL(glFinish);
    LOAD_GL(glGetError);
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

void computeMVPMatrix(const Camera& camera, int width, int height, float* out,
                      bool useProjection2D) {
    // BAZ-ORNEKLEMELI AFFINE: viewTransform/rotasyon bilesenlerine guvenme —
    // 2B kameranin viewTransform'u birim vektorlerde ortogonal olmayan
    // bilesenler tasiyor ve GL cizgileri GDI konumundan kayiyordu ("cizgiler
    // kayboluyor" = yanlis yerde ciziliyor). Bunun yerine 4 baz noktasinin
    // EKRAN izdusumunu ornekle ve dunya→NDC affine haritasini dogrudan kur:
    // tanim geregi GDI ile birebir ayni (ayni project() fonksiyonu).
    const auto toNdc = [&](Vec2 s) -> Vec2 {
        return { 2.0 * s.x / static_cast<double>(width) - 1.0,
                 1.0 - 2.0 * s.y / static_cast<double>(height) };
    };
    // 2B modda GDI project2D kullanir; 3B'de project. Yanlis izdusumle
    // orneklenen matris 2B cizgileri GDI'ya gore kaydiriyordu ("cift cizgi").
    const auto sample = [&](const Vec3& p) -> Vec2 {
        return useProjection2D ? toNdc(camera.project2D(p, width, height))
                               : toNdc(camera.project(p, width, height));
    };
    const Vec2 o  = sample({0.0, 0.0, 0.0});
    const Vec2 ex = sample({1.0, 0.0, 0.0});
    const Vec2 ey = sample({0.0, 1.0, 0.0});
    const Vec2 ez = sample({0.0, 0.0, 1.0});
    // Sutun-bas (column-major) affine: clip = M * [x, y, z, 1]
    out[0] = static_cast<float>(ex.x - o.x);
    out[4] = static_cast<float>(ey.x - o.x);
    out[8] = static_cast<float>(ez.x - o.x);
    out[12] = static_cast<float>(o.x);
    out[1] = static_cast<float>(ex.y - o.y);
    out[5] = static_cast<float>(ey.y - o.y);
    out[9] = static_cast<float>(ez.y - o.y);
    out[13] = static_cast<float>(o.y);
    out[2] = 0.0f; out[6] = 0.0f; out[10] = 0.0f; out[14] = 0.0f;
    out[3] = 0.0f; out[7] = 0.0f; out[11] = 0.0f; out[15] = 1.0f;
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
    (void)windowHandle; // canvas HWND'si YALNIZ boyut izleme icin — GL baglami
                        // gizli yardimci pencerede kurulur.

    // GIZLI YARDIMCI PENCERE: canvas'a SetPixelFormat uygulamak pencereyi GL
    // piksel formatina cevirip DWM/Qt kompozisyonunda GDI sunumunu kapatir —
    // canvas tamamen SIYAH kalir ("GL'ye gecince her sey yok oluyor"). FBO
    // offscreen renderi zaten gercek pencere gerektirmez; context gizli
    // pencerede yasar, canvas'a HIC dokunulmaz.
    wglWindow_ = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 1, 1,
                                 nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!wglWindow_) return false;

    openglModule = LoadLibraryA("opengl32.dll");
    if (!openglModule) return false;

    HDC hdc = GetDC(static_cast<HWND>(wglWindow_));
    if (!hdc) { FreeLibrary(openglModule); openglModule = nullptr; return false; }
    deviceContext_ = hdc;

    if (!loadWglExtensions(hdc)) {
        ReleaseDC(static_cast<HWND>(wglWindow_), hdc); deviceContext_ = nullptr;
        FreeLibrary(openglModule); openglModule = nullptr;
        return false;
    }

    glContext_ = createGL33Context(hdc);
    if (!glContext_) {
        ReleaseDC(static_cast<HWND>(wglWindow_), hdc); deviceContext_ = nullptr;
        FreeLibrary(openglModule); openglModule = nullptr;
        return false;
    }

    if (!wglMakeCurrent(hdc, static_cast<HGLRC>(glContext_))) {
        wglDeleteContext(static_cast<HGLRC>(glContext_)); glContext_ = nullptr;
        ReleaseDC(static_cast<HWND>(wglWindow_), hdc); deviceContext_ = nullptr;
        FreeLibrary(openglModule); openglModule = nullptr;
        return false;
    }

    if (!compileShaders()) {
        wglMakeCurrent(hdc, nullptr);
        wglDeleteContext(static_cast<HGLRC>(glContext_)); glContext_ = nullptr;
        ReleaseDC(static_cast<HWND>(wglWindow_), hdc); deviceContext_ = nullptr;
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
    if (deviceContext_ && wglWindow_) {
        wglMakeCurrent(static_cast<HDC>(deviceContext_), nullptr);
        ReleaseDC(static_cast<HWND>(wglWindow_), static_cast<HDC>(deviceContext_));
        deviceContext_ = nullptr;
    }
    if (wglWindow_) { DestroyWindow(static_cast<HWND>(wglWindow_)); wglWindow_ = nullptr; }
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
    if (wglWindow_) { DestroyWindow(static_cast<HWND>(wglWindow_)); wglWindow_ = nullptr; }
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
        // PBO cifti boyut degisince yeniden kurulur (asenkron readback).
        if (pboPair_[0] != 0) { glDeleteBuffers(2, pboPair_); pboPair_[0] = pboPair_[1] = 0; }
        glGenBuffers(2, pboPair_);
        for (const std::uint32_t pbo : pboPair_) {
            glBindBuffer(GLConst::PIXEL_PACK_BUFFER, pbo);
            glBufferData(GLConst::PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(needed),
                         nullptr, GLConst::STREAM_READ);
        }
        glBindBuffer(GLConst::PIXEL_PACK_BUFFER, 0);
        pboFrame_ = 0;
    }
}

void OpenGLRenderBackend::resetDiagnostics() {
    // Her GL oturumu basinda tanilar tazelenir (statik sayaçlar islem
    // omru boyunca kesilir ve guncel oturumun verisi kaybolurdu).
    glDiagCount_ = 0;
    mvpDiagCount_ = 0;
    errDiagCount_ = 0;
}

bool OpenGLRenderBackend::blitToDC(void* target, int width, int height) {
    if (blitBuffer_.empty() || width <= 0 || height <= 0) return false;
    HDC hdcTarget = static_cast<HDC>(target);

    std::size_t needed = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    if (blitBuffer_.size() < needed) return false;

    // glReadPixels bottom-to-top okur; POZITIF biHeight (bottom-up DIB) ile
    // readback satir duzeni birebir korunur — negatif (top-down) DIB'de
    // goruntu dikey aynalanmis cikiyordu ("GL ekrani bozuyor").
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = height; // bottom-up DIB — readback ile ayni duzen
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
    const Camera& camera, int width, int height, void* targetHdc,
    bool useProjection2D, std::uint64_t contentRevision,
    std::uint8_t faceAlpha) {
    if (!initialized_ || models.empty()) return false;
    const auto glDiag = [this](const char* step) {
        if (glDiagCount_ >= 8) return;
        FILE* diag = fopen("model-maker-render.log", "a");
        if (diag) { fprintf(diag, "GLDIAG %d %s\n", glDiagCount_, step); fclose(diag); }
    };
    HDC hdc = static_cast<HDC>(deviceContext_);
    if (!wglMakeCurrent(hdc, static_cast<HGLRC>(glContext_))) {
        ++glDiagCount_; glDiag("MAKECURRENT-FAIL"); return false;
    }
    glDiag("CURRENT-OK");

    drawCalls_ = 0; uploadBytes_ = 0; renderedLines_ = 0;

    ensureFbo(width, height);
    if (fbo_ == 0) { ++glDiagCount_; glDiag("FBO-FAIL"); wglMakeCurrent(hdc, nullptr); return false; }
    glDiag("FBO-OK");
    glBindFramebuffer(GLConst::FRAMEBUFFER, fbo_);
    glViewport(0, 0, width, height);
    // Seffaf zemin: GDI icerigi AlphaBlend ile alttan gorunur, yalniz
    // cizgiler kompozite edilir.
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GLConst::COLOR_BUFFER_BIT);

    ensureBatch(models, contentRevision);
    glDiag("BATCH-OK");
    faceAlpha_ = faceAlpha;
    renderedTriangles_ = 0;
    if (faceAlpha != 0) {
        // Yuzler once, derinlik testiyle: opak dolgu + dogru ortme;
        // tel kafes cizgileri ustte kalir (depth yazilmaz/yok sayilir).
        ensureFaceBatch(models, contentRevision);
        renderFaceBatch(faceBatch_, camera, width, height, useProjection2D, faceAlpha);
        renderedTriangles_ = faceBatch_.indexCount / 3;
        glClear(GLConst::DEPTH_BUFFER_BIT);
    }
    if (lineBatch_.indexCount != 0) {
        renderBatch(lineBatch_, camera, width, height, useProjection2D);
        drawCalls_ = 1;
        renderedLines_ = lineBatch_.indexCount / 2;
    }

    glFinish();
    ensureBlitBuffer(width, height);
    if (pboPair_[0] != 0 && glMapBuffer) {
        // Asenkron: bu kareyi diger PBO'ya yaz (bekleme yok), onceki karenin
        // PBO'sundan kopyala — zoom sirasindaki GPU beklemesi kaybolur.
        const std::size_t needed = blitBuffer_.size();
        glBindBuffer(GLConst::PIXEL_PACK_BUFFER, pboPair_[pboFrame_ % 2]);
        glReadPixels(0, 0, width, height, GLConst::BGRA_EXT, GLConst::UNSIGNED_BYTE_TYPE,
                     nullptr);
        if (pboFrame_ >= 2) {
            glBindBuffer(GLConst::PIXEL_PACK_BUFFER, pboPair_[(pboFrame_ + 1) % 2]);
            const void* mapped = glMapBuffer(GLConst::PIXEL_PACK_BUFFER, GLConst::READ_ONLY);
            if (mapped) {
                std::memcpy(blitBuffer_.data(), mapped, needed);
                glUnmapBuffer(GLConst::PIXEL_PACK_BUFFER);
            }
        }
        glBindBuffer(GLConst::PIXEL_PACK_BUFFER, 0);
        ++pboFrame_;
    } else {
        glReadPixels(0, 0, width, height, GLConst::BGRA_EXT, GLConst::UNSIGNED_BYTE_TYPE,
                     blitBuffer_.data());
    }
    // Readback taramasi: TUM tampon + ilk opak pikselin satiri + beklenen
    // cizgi bolgesinden 3x3 ornek (eski 20k taramasi ust satirlari kapsiyordu
    // ve asagi cizilen cizgiyi kaciriyordu).
    if (glDiagCount_ < 5 && !blitBuffer_.empty()) {
        const auto px = [&](std::size_t idx) {
            return static_cast<unsigned>(blitBuffer_[idx]);
        };
        const std::size_t center = (static_cast<std::size_t>(height / 2) * width + width / 2) * 4;
        std::size_t opaque = 0; std::size_t firstOpaque = blitBuffer_.size();
        for (std::size_t i = 3; i < blitBuffer_.size(); i += 4) {
            if (blitBuffer_[i] > 32) {
                ++opaque;
                if (i < firstOpaque) firstOpaque = i;
            }
        }
        const std::size_t probePx = 302 + 413 * static_cast<std::size_t>(width);
        const std::size_t probe = probePx * 4;
        FILE* diag = fopen("model-maker-render.log", "a");
        if (diag) {
            fprintf(diag,
                "GLDIAG %d READBACK center=%u,%u,%u,%u opaque=%zu/%zu firstOpaque=%zu,%zu "
                "probe(302,413)=%u,%u,%u,%u lines=%zu gdiObjs=%d\n",
                glDiagCount_, px(center), px(center + 1), px(center + 2), px(center + 3),
                opaque, blitBuffer_.size() / 4,
                firstOpaque == blitBuffer_.size() ? 0 : (firstOpaque / 4) % static_cast<std::size_t>(width),
                firstOpaque == blitBuffer_.size() ? 0 : (firstOpaque / 4) / static_cast<std::size_t>(width),
                probe < blitBuffer_.size() ? px(probe) : 0,
                probe < blitBuffer_.size() ? px(probe + 1) : 0,
                probe < blitBuffer_.size() ? px(probe + 2) : 0,
                probe < blitBuffer_.size() ? px(probe + 3) : 0,
                renderedLines_,
                static_cast<int>(GetGuiResources(GetCurrentProcess(), 0)));
            fclose(diag);
        }
    }
    glBindFramebuffer(GLConst::FRAMEBUFFER, 0);
    wglMakeCurrent(hdc, nullptr);

    const bool blitOk = blitToDC(targetHdc, width, height);
    glDiag(blitOk ? "BLIT-OK" : "BLIT-FAIL");
    ++glDiagCount_;
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
    uniformAlpha_ = glGetUniformLocation(shaderProgram_, "uAlpha");
    glUseProgram(shaderProgram_);
    if (uniformAlpha_ >= 0) glUniform1f(uniformAlpha_, 1.0f);
    glUseProgram(0);
    // F3: kamera UBO'su (64 bayt = mat4) — her karede glBufferSubData ile
    // guncellenir, glUniform cagrisi ve string lookup yok.
    glGenBuffers(1, &cameraUbo_);
    glBindBuffer(GLConst::UNIFORM_BUFFER, cameraUbo_);
    glBufferData(GLConst::UNIFORM_BUFFER, 64, nullptr, GLConst::DYNAMIC_DRAW);
    glBindBufferBase(GLConst::UNIFORM_BUFFER, 0, cameraUbo_);
    glBindBuffer(GLConst::UNIFORM_BUFFER, 0);
    return true;
}

// ── GPU batch rendering ──────────────────────────────────────────
void OpenGLRenderBackend::renderWireframeBatch(
    const std::vector<std::pair<std::size_t, WireframeModel>>& models,
    const Camera& camera) {
    if (!initialized_ || models.empty()) return;
    ensureBatch(models, 0);
    if (lineBatch_.indexCount == 0) return;
    renderBatch(lineBatch_, camera, width_, height_);
    perFrameMetricsReset_ = false;
    drawCalls_ = 1;
    renderedLines_ = lineBatch_.indexCount / 2;
}

void OpenGLRenderBackend::ensureBatch(
    const std::vector<std::pair<std::size_t, WireframeModel>>& models,
    std::uint64_t contentRevision) {
    std::size_t totalEdges = 0;
    for (const auto& [idx, model] : models) totalEdges += model.edges().size();
    // F5: icerik surumu birincil etiket — pozisyon degistiren komutlar
    // (move/trim) boyut ayni kalsa da batch'i yeniler (eski tag bayat
    // geometri birakiyordu). Surum yoksa boyut bazli eski davranis.
    std::size_t tag = contentRevision
        ? static_cast<std::size_t>(contentRevision)
        : (models.size() ^ (totalEdges << 16));
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

void OpenGLRenderBackend::ensureFaceBatch(
    const std::vector<std::pair<std::size_t, WireframeModel>>& models,
    std::uint64_t contentRevision) {
    std::size_t totalFaces = 0;
    for (const auto& [idx, model] : models) totalFaces += model.faces().size();
    std::size_t tag = contentRevision
        ? static_cast<std::size_t>(contentRevision)
        : (models.size() ^ (totalFaces << 16));
    if (tag == faceBatch_.versionTag && !faceBatch_.dirty) return;

    faceBatch_.versionTag = tag;
    faceBatch_.dirty = true;
    faceBatch_.modelCount = models.size();

    std::size_t totalVertices = 0, totalIndices = 0;
    for (const auto& [idx, model] : models) {
        if (model.faces().empty()) continue;
        totalVertices += model.vertices().size();
        for (const auto& face : model.faces())
            totalIndices += face.size() >= 3 ? (face.size() - 2) * 3 : 0;
    }

    faceBatch_.vertices.clear();
    faceBatch_.vertices.reserve(totalVertices);
    faceBatch_.indices.clear();
    faceBatch_.indices.reserve(totalIndices);

    std::size_t vertexBase = 0;
    for (const auto& [modelIdx, model] : models) {
        const auto& vertices = model.vertices();
        const auto& faces = model.faces();
        if (vertices.empty() || faces.empty()) continue;
        const auto props = model.properties();
        const std::uint32_t rgba = toRGBA8(props.effectiveColor);
        for (const auto& v : vertices) {
            GpuLineBatch::GpuVertex gv;
            gv.x = static_cast<float>(v.x);
            gv.y = static_cast<float>(v.y);
            gv.z = static_cast<float>(v.z);
            gv.color = rgba;
            faceBatch_.vertices.push_back(gv);
        }
        for (const auto& face : faces) {
            if (face.size() < 3) continue;
            // N-gen yelpaze ucgenlemesi (ucgen yuzler icin birebir ayni).
            for (std::size_t i = 1; i + 1 < face.size(); ++i) {
                faceBatch_.indices.push_back(static_cast<std::uint32_t>(vertexBase + face[0]));
                faceBatch_.indices.push_back(static_cast<std::uint32_t>(vertexBase + face[i]));
                faceBatch_.indices.push_back(static_cast<std::uint32_t>(vertexBase + face[i + 1]));
            }
        }
        vertexBase += vertices.size();
    }
    faceBatch_.indexCount = faceBatch_.indices.size();
    uploadBatch(faceBatch_);
}

void OpenGLRenderBackend::renderFaceBatch(const GpuLineBatch& batch, const Camera& camera,
                                          int width, int height, bool useProjection2D,
                                          std::uint8_t faceAlpha) {
    if (batch.indexCount == 0 || batch.vao == 0) return;
    glUseProgram(shaderProgram_);
    float mvp[16];
    computeMVPMatrix(camera, width, height, mvp, useProjection2D);
    glBindBuffer(GLConst::UNIFORM_BUFFER, cameraUbo_);
    glBufferSubData(GLConst::UNIFORM_BUFFER, 0, 64, mvp);
    glBindBuffer(GLConst::UNIFORM_BUFFER, 0);
    if (uniformAlpha_ >= 0)
        glUniform1f(uniformAlpha_, static_cast<float>(faceAlpha) / 255.0f);

    glEnable(GLConst::DEPTH_TEST);
    glDepthFunc(GLConst::LEQUAL);
    glClear(GLConst::DEPTH_BUFFER_BIT);
    glEnable(GLConst::BLEND_MODE);
    glBlendFunc(GLConst::SRC_ALPHA, GLConst::ONE_MINUS_SRC_ALPHA);
    glDepthMask(faceAlpha == 255 ? GLConst::GL_TRUE : GLConst::GL_FALSE);
    // Cizgilerin yuzeyle ayni duzlemde kaybolmasini onle: yuzleri hafifce
    // geri it (poligon ofseti) — tel kafes her zaman gorunur kalir.
    glEnable(GLConst::POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    glBindVertexArray(batch.vao);
    glDrawElements(GLConst::TRIANGLES, static_cast<GLsizei>(batch.indexCount),
                   GLConst::UNSIGNED_INT_TYPE, nullptr);
    glBindVertexArray(0);
    glDisable(GLConst::POLYGON_OFFSET_FILL);
    glDisable(GLConst::BLEND_MODE);
    glDepthMask(GLConst::GL_TRUE);
    glDisable(GLConst::DEPTH_TEST);
    if (uniformAlpha_ >= 0) glUniform1f(uniformAlpha_, 1.0f);
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
                                       int width, int height, bool useProjection2D) {
    if (batch.indexCount == 0 || batch.vao == 0) return;
    glUseProgram(shaderProgram_);
    float mvp[16];
    computeMVPMatrix(camera, width, height, mvp, useProjection2D);
    if (mvpDiagCount_ < 5) {
        ++mvpDiagCount_;
        const float vx = batch.vertices.empty() ? 0.0f : batch.vertices[0].x;
        const float vy = batch.vertices.empty() ? 0.0f : batch.vertices[0].y;
        const float vz = batch.vertices.empty() ? 0.0f : batch.vertices[0].z;
        const float cx = mvp[0] * vx + mvp[4] * vy + mvp[8] * vz + mvp[12];
        const float cy = mvp[1] * vx + mvp[5] * vy + mvp[9] * vz + mvp[13];
        const float cw = mvp[3] * vx + mvp[7] * vy + mvp[11] * vz + mvp[15];
        // GDI ile hizalama tanisi: ayni vertex'in GDI izdusumu (project2D/project)
        // ve GL matrisinden hesaplanan piksel konumu — delta 0 ise birebir.
        const Vec3 v0world{static_cast<double>(vx), static_cast<double>(vy),
                           static_cast<double>(vz)};
        const Vec2 gdiP = useProjection2D ? camera.project2D(v0world, width, height)
                                          : camera.project(v0world, width, height);
        const float ndcX = cw != 0.0f ? cx / cw : cx;
        const float ndcY = cw != 0.0f ? cy / cw : cy;
        const float glPx = (ndcX + 1.0f) * 0.5f * static_cast<float>(width);
        const float glPy = (1.0f - ndcY) * 0.5f * static_cast<float>(height);
        FILE* diag = fopen("model-maker-render.log", "a");
        if (diag) {
            fprintf(diag, "GLDIAG MVP proj=%s v0=(%.2f,%.2f,%.2f) clip=(%.2f,%.2f) idx=%zu "
                    "gdiPx=(%.1f,%.1f) glPx=(%.1f,%.1f) delta=(%.1f,%.1f)\n",
                    useProjection2D ? "2D" : "3D",
                    vx, vy, vz, ndcX, ndcY, batch.indexCount,
                    gdiP.x, gdiP.y, glPx, glPy, glPx - gdiP.x, glPy - gdiP.y);
            fclose(diag);
        }
    }
    glBindBuffer(GLConst::UNIFORM_BUFFER, cameraUbo_);
    glBufferSubData(GLConst::UNIFORM_BUFFER, 0, 64, mvp);
    glBindBuffer(GLConst::UNIFORM_BUFFER, 0);
    glEnable(GLConst::BLEND_MODE);
    glBlendFunc(GLConst::SRC_ALPHA, GLConst::ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(batch.vao);
    glDrawElements(GLConst::LINES, static_cast<GLsizei>(batch.indexCount),
                   GLConst::UNSIGNED_INT_TYPE, nullptr);
    glBindVertexArray(0);
    if (errDiagCount_ < 3) {
        ++errDiagCount_;
        FILE* diag = fopen("model-maker-render.log", "a");
        if (diag) {
            if (glGetError)
                fprintf(diag, "GLDIAG GLERROR=0x%X after draw (idx=%zu)\n",
                        static_cast<unsigned>(glGetError()), batch.indexCount);
            else
                fprintf(diag, "GLDIAG GLERROR-PTR-NULL (load basarisiz)\n");
            fclose(diag);
        }
    }
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
    if (cameraUbo_) { glDeleteBuffers(1, &cameraUbo_); cameraUbo_ = 0; }
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
