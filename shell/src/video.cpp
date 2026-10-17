// Video: GL loader, shader pipeline (sharp + shared CRT shader), frame upload, rendering, screenshot.
#include <algorithm>
#include <cmath>
#include <cstring>

#include <SDL_opengl.h>

#include "crt.h"
#include "internal.h"
#include "retro_crt_shaders.h"

#ifndef GL_SRGB8
#define GL_SRGB8 0x8C41
#endif

namespace retro_shell {

uint32_t pixel_rgb(const FrameView &f, int x, int y) {
  const uint8_t *p = &f.data[(static_cast<size_t>(y) * f.width + x) * f.bytes_per_pixel];
  if (f.format == PixelFormat::XRGB8888) return (p[2] << 16) | (p[1] << 8) | p[0];
  uint16_t v;
  memcpy(&v, p, 2);
  if (f.format == PixelFormat::RGB565) return (((v >> 11) & 31) << 19) | (((v >> 5) & 63) << 10) | ((v & 31) << 3);
  return (((v >> 10) & 31) << 19) | (((v >> 5) & 31) << 11) | ((v & 31) << 3);
}

namespace detail {

Frame g_frame;
SDL_Window *g_window = nullptr;
SDL_GLContext g_gl = nullptr;
double g_aspect = 4.0 / 3.0;

namespace {

int g_interval = 0;  // swap interval actually set
std::vector<uint8_t> g_present;
Frame g_raw;  // the core's frame as delivered; kept only when the game has a border crop
std::vector<AspectChoice> g_aspects;
double g_native_wh = 4.0 / 3.0;  // width / height of the core's picture, for ratio 0
bool g_toast_drawn = false;  // last uploaded frame carried an overlay (needs one clean re-upload)

Uint32 sdl_format() {
  switch (g_frame.fmt) {
    case PixelFormat::XRGB8888: return SDL_PIXELFORMAT_ARGB8888;
    case PixelFormat::RGB1555: return SDL_PIXELFORMAT_RGB555;
    default: return SDL_PIXELFORMAT_RGB565;
  }
}

// ---- minimal OpenGL 3.3 core loader (only what the shell uses)
#define GL_FUNCS(X)                                                                       \
  X(PFNGLCREATESHADERPROC, glCreateShader) X(PFNGLSHADERSOURCEPROC, glShaderSource)       \
  X(PFNGLCOMPILESHADERPROC, glCompileShader) X(PFNGLGETSHADERIVPROC, glGetShaderiv)       \
  X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) X(PFNGLCREATEPROGRAMPROC, glCreateProgram) \
  X(PFNGLATTACHSHADERPROC, glAttachShader) X(PFNGLLINKPROGRAMPROC, glLinkProgram)         \
  X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) \
  X(PFNGLDELETESHADERPROC, glDeleteShader) X(PFNGLUSEPROGRAMPROC, glUseProgram)           \
  X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) X(PFNGLUNIFORM1IPROC, glUniform1i) \
  X(PFNGLUNIFORM2FPROC, glUniform2f) X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)       \
  X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray) X(PFNGLUNIFORM1FPROC, glUniform1f)       \
  X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers) X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer) \
  X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D) X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)
#define X(T, N) T N = nullptr;
GL_FUNCS(X)
#undef X

// GL 1.1 entry points are loaded at runtime too, so the binary does not link libGL/opengl32.
struct GlBase {
  decltype(&::glViewport) Viewport;
  decltype(&::glClearColor) ClearColor;
  decltype(&::glClear) Clear;
  decltype(&::glTexParameteri) TexParameteri;
  decltype(&::glPixelStorei) PixelStorei;
  decltype(&::glGenTextures) GenTextures;
  decltype(&::glBindTexture) BindTexture;
  decltype(&::glTexImage2D) TexImage2D;
  decltype(&::glTexSubImage2D) TexSubImage2D;
  decltype(&::glDrawArrays) DrawArrays;
  decltype(&::glReadPixels) ReadPixels;
  decltype(&::glReadBuffer) ReadBuffer;
  decltype(&::glGetString) GetString;
} gl_fn;
#define glViewport gl_fn.Viewport
#define glClearColor gl_fn.ClearColor
#define glClear gl_fn.Clear
#define glTexParameteri gl_fn.TexParameteri
#define glPixelStorei gl_fn.PixelStorei
#define glGenTextures gl_fn.GenTextures
#define glBindTexture gl_fn.BindTexture
#define glTexImage2D gl_fn.TexImage2D
#define glTexSubImage2D gl_fn.TexSubImage2D
#define glDrawArrays gl_fn.DrawArrays
#define glReadPixels gl_fn.ReadPixels
#define glReadBuffer gl_fn.ReadBuffer
#define glGetString gl_fn.GetString

GLuint g_prog_sharp = 0, g_prog_crt = 0, g_vao = 0, g_tex = 0, g_tex_srgb = 0, g_fbo = 0, g_fbo_tex = 0;
int g_tex_w = 0, g_tex_h = 0;

GLuint compile(GLenum type, const char *src) {
  GLuint sh = glCreateShader(type);
  glShaderSource(sh, 1, &src, nullptr);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetShaderInfoLog(sh, sizeof log, nullptr, log);
    fprintf(stderr, "shader compile error: %s\n", log);
    return 0;
  }
  return sh;
}

// The frame as it should be shown: the core's picture plus the menu and toast overlays.
const std::vector<uint8_t> &present_frame() {
  const float ta = toast_alpha();
  g_toast_drawn = ta > 0.0f || menu_open();
  if (!g_toast_drawn || !g_frame.valid) return g_frame.data;
  Frame tmp;
  g_present = g_frame.data;
  tmp.data.swap(g_present);
  tmp.w = g_frame.w; tmp.h = g_frame.h; tmp.bpp = g_frame.bpp; tmp.fmt = g_frame.fmt;
  if (menu_open()) menu_draw(tmp);
  if (ta > 0.0f) toast_draw(tmp, ta);
  g_present.swap(tmp.data);
  return g_present;
}

// Destination rectangle in drawable (pixel) coordinates; letterboxed in black.
SDL_Rect compute_dest(int draw_w, int draw_h) {
  const double lh = g_tex_h > 0 ? g_tex_h : g_frame.h;
  const double base_w = lh * g_aspect, base_h = lh;
  double s = std::min(draw_w / base_w, draw_h / base_h);
  if (g_set.integer && s >= 1.0) s = std::floor(s);
  SDL_Rect r;
  r.w = std::max(1, static_cast<int>(base_w * s + 0.5));
  r.h = std::max(1, static_cast<int>(base_h * s + 0.5));
  r.x = (draw_w - r.w) / 2;
  r.y = (draw_h - r.h) / 2;
  return r;
}

}  // namespace

int g_base_h = 240;

bool toast_on_screen() { return g_toast_drawn; }
bool vsync_enabled() { return g_interval != 0; }
// Auto = on, with adaptive vsync (late frames tear instead of stalling) where the driver has it.
void video_apply_vsync(int mode, bool force_off) {
  if (!g_gl) return;
  int want = (force_off || mode == VSYNC_OFF) ? 0 : 1;
  if (want && mode == VSYNC_AUTO && SDL_GL_SetSwapInterval(-1) == 0) {
    g_interval = -1;
    return;
  }
  g_interval = SDL_GL_SetSwapInterval(want) == 0 ? want : 0;
}

// ---- aspect ratio list (Video page)
void video_init_aspects(const AvInfo &av) {
  g_native_wh = av.height ? static_cast<double>(av.width) / av.height : 4.0 / 3.0;
  g_aspects = g_game->aspects;
  if (g_aspects.empty()) {
    const double r = g_game->display_aspect > 0 ? g_game->display_aspect : (av.aspect > 0 ? av.aspect : 4.0 / 3.0);
    g_aspects.push_back({"default", "DEFAULT", r});
  }
  if (g_set.aspect < 0 || g_set.aspect >= static_cast<int>(g_aspects.size())) g_set.aspect = 0;
}
size_t aspect_count() { return g_aspects.size(); }
std::string aspect_label(size_t i) { return i < g_aspects.size() ? g_aspects[i].label : ""; }
std::string aspect_id(size_t i) { return i < g_aspects.size() ? g_aspects[i].id : ""; }

static bool crop_active() {
  const BorderCrop &c = g_game->border_crop;
  return c.w > 0 && !g_set.game_border && !g_boot_hold && g_raw.valid && c.x >= 0 && c.y >= 0 &&
         c.x + c.w <= g_raw.w && c.y + c.h <= g_raw.h;
}
bool video_has_crop() { return g_game->border_crop.w > 0; }

void video_update_aspect() {
  if (g_aspects.empty()) return;
  const size_t i = static_cast<size_t>(std::max(0, std::min(g_set.aspect, static_cast<int>(g_aspects.size()) - 1)));
  double r = g_aspects[i].ratio;
  if (r <= 0) r = (g_frame.valid && g_frame.h) ? static_cast<double>(g_frame.w) / g_frame.h : g_native_wh;
  // The correct (first) ratio is that of the whole picture; a cropped picture has its own.
  if (i == 0 && g_game->border_crop.aspect > 0 && g_game->border_crop.w > 0 && !g_set.game_border) r = g_game->border_crop.aspect;
  g_aspect = r;
}

void video_rebuild() {
  if (g_game->border_crop.w <= 0 || !g_raw.valid) return;
  if (!crop_active()) {
    g_frame = g_raw;
  } else {
    const BorderCrop &c = g_game->border_crop;
    g_frame.w = c.w; g_frame.h = c.h; g_frame.bpp = g_raw.bpp; g_frame.fmt = g_raw.fmt;
    g_frame.data.resize(static_cast<size_t>(c.w) * c.h * g_raw.bpp);
    for (int y = 0; y < c.h; ++y)
      memcpy(&g_frame.data[static_cast<size_t>(y) * c.w * g_raw.bpp],
             &g_raw.data[(static_cast<size_t>(y + c.y) * g_raw.w + c.x) * g_raw.bpp], static_cast<size_t>(c.w) * g_raw.bpp);
    g_frame.valid = true;
  }
  video_update_aspect();
}

// Logical pixels of the game per window unit (mouse motion arrives in window units).
bool video_mouse_scale(double &sx, double &sy) {
  if (!g_window || !g_frame.valid) return false;
  int ww, wh, dw, dh;
  SDL_GetWindowSize(g_window, &ww, &wh);
  SDL_GL_GetDrawableSize(g_window, &dw, &dh);
  if (!g_gl) { dw = ww; dh = wh; }
  if (ww <= 0 || wh <= 0) return false;
  const SDL_Rect r = compute_dest(dw, dh);
  const double lw = g_game->logical_width > 0 ? g_game->logical_width * (static_cast<double>(g_frame.w) / (g_raw.valid ? g_raw.w : g_frame.w)) : g_frame.w;
  const double lh = g_game->logical_height > 0 ? g_game->logical_height * (static_cast<double>(g_frame.h) / (g_raw.valid ? g_raw.h : g_frame.h)) : g_frame.h;
  sx = (static_cast<double>(dw) / ww) * lw / r.w;
  sy = (static_cast<double>(dh) / wh) * lh / r.h;
  vlog("mouse scale %.4f %.4f\n", sx, sy);
  return true;
}

namespace {

CrtSetting crt_setting() {
  switch (g_set.filter) {
    case FILTER_SHARP: return CrtSetting::Sharp;
    case FILTER_CRT: return CrtSetting::Crt;
    case FILTER_CRTMASK: return CrtSetting::CrtMask;
    default: return CrtSetting::Auto;
  }
}

// Size of the render target in pixels: the offscreen image, the drawable, or (headless) the window.
void target_size(int &w, int &h) {
  if (g_offscreen) { w = g_opt.out_w; h = g_opt.out_h; return; }
  if (g_gl) SDL_GL_GetDrawableSize(g_window, &w, &h);
  else if (g_window) SDL_GetWindowSize(g_window, &w, &h);
  else { w = 0; h = 0; }
}

// Logical size of the picture for the CRT filter (interlace / hi-res normalised by the game).
void logical_size(float &lw, float &lh) {
  lw = static_cast<float>(g_tex_w > 0 ? g_tex_w : g_frame.w);
  lh = static_cast<float>(g_tex_h > 0 ? g_tex_h : g_frame.h);
  // hi-res / interlace: the game states its logical size for the uncropped picture
  if (g_game->logical_width > 0) lw = static_cast<float>(g_game->logical_width) * lw / static_cast<float>(g_raw.valid ? g_raw.w : g_frame.w);
  if (g_game->logical_height > 0) lh = static_cast<float>(g_game->logical_height) * lh / static_cast<float>(g_raw.valid ? g_raw.h : g_frame.h);
}

// Vertical scale (picture height in output pixels / logical lines) decides Auto.
CrtMode resolve_mode(const SDL_Rect &dst) {
  float lw, lh;
  logical_size(lw, lh);
  return crt_resolve(crt_setting(), lh > 0 ? dst.h / static_cast<double>(lh) : 0.0);
}

void set_uniform(void *user, const char *name, int n, const float *v) {
  const GLint loc = glGetUniformLocation(*static_cast<GLuint *>(user), name);
  if (n == 1) glUniform1f(loc, v[0]);
  else glUniform2f(loc, v[0], v[1]);
}

}  // namespace

bool g_offscreen = false;

const char *filter_active_name() {
  int w, h;
  target_size(w, h);
  if (w <= 0 || h <= 0) return "SHARP";
  switch (resolve_mode(compute_dest(w, h))) {
    case CrtMode::Crt: return "CRT";
    case CrtMode::CrtMask: return "CRT+MASK";
    default: return "SHARP";
  }
}

bool window_to_frame(int wx, int wy, int &fx, int &fy) {
  if (!g_window || !g_frame.valid) return false;
  int ww, wh, dw, dh;
  SDL_GetWindowSize(g_window, &ww, &wh);
  SDL_GL_GetDrawableSize(g_window, &dw, &dh);
  if (!g_gl) { dw = ww; dh = wh; }
  if (ww <= 0 || wh <= 0) return false;
  const double px = wx * (static_cast<double>(dw) / ww), py = wy * (static_cast<double>(dh) / wh);
  const SDL_Rect r = compute_dest(dw, dh);
  fx = static_cast<int>((px - r.x) * g_frame.w / r.w);
  fy = static_cast<int>((py - r.y) * g_frame.h / r.h);
  return px >= r.x && py >= r.y && px < r.x + r.w && py < r.y + r.h;
}
const char *gl_renderer() { return reinterpret_cast<const char *>(glGetString(GL_RENDERER)); }
const char *gl_version() { return reinterpret_cast<const char *>(glGetString(GL_VERSION)); }

void video_submit(const FrameDesc &f) {
  if (!f.data || f.width == 0 || f.height == 0) return;  // dupe frame: keep the previous one
  const int bpp = f.format == PixelFormat::XRGB8888 ? 4 : 2;
  Frame &dst = g_game->border_crop.w > 0 ? g_raw : g_frame;
  dst.w = static_cast<int>(f.width);
  dst.h = static_cast<int>(f.height);
  dst.bpp = bpp;
  dst.fmt = f.format;
  dst.data.resize(static_cast<size_t>(f.width) * f.height * bpp);
  const uint8_t *src = static_cast<const uint8_t *>(f.data);
  for (unsigned y = 0; y < f.height; ++y)
    memcpy(&dst.data[static_cast<size_t>(y) * f.width * bpp], src + y * f.pitch, static_cast<size_t>(f.width) * bpp);
  dst.valid = true;
  if (&dst == &g_raw) video_rebuild();
}

bool load_gl() {
#define X(T, N)                                         \
  N = reinterpret_cast<T>(SDL_GL_GetProcAddress(#N));   \
  if (!N) { fprintf(stderr, "missing GL function " #N "\n"); return false; }
  GL_FUNCS(X)
#undef X
#define B(M, N)                                                                   \
  gl_fn.M = reinterpret_cast<decltype(gl_fn.M)>(SDL_GL_GetProcAddress(N));        \
  if (!gl_fn.M) { fprintf(stderr, "missing GL function " N "\n"); return false; }
  B(Viewport, "glViewport") B(ClearColor, "glClearColor") B(Clear, "glClear")
  B(TexParameteri, "glTexParameteri") B(PixelStorei, "glPixelStorei")
  B(GenTextures, "glGenTextures") B(BindTexture, "glBindTexture") B(TexImage2D, "glTexImage2D")
  B(TexSubImage2D, "glTexSubImage2D") B(DrawArrays, "glDrawArrays") B(ReadPixels, "glReadPixels")
  B(ReadBuffer, "glReadBuffer") B(GetString, "glGetString")
#undef B
  return true;
}

static GLuint link_program(GLuint vs, const char *frag_src) {
  GLuint fs = compile(GL_FRAGMENT_SHADER, frag_src);
  if (!vs || !fs) return 0;
  GLuint prog = glCreateProgram();
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);
  glLinkProgram(prog);
  GLint ok = 0;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetProgramInfoLog(prog, sizeof log, nullptr, log);
    fprintf(stderr, "shader link error: %s\n", log);
    return 0;
  }
  glDeleteShader(fs);
  return prog;
}

bool init_gl_pipeline() {
  GLuint vs = compile(GL_VERTEX_SHADER, kRetroCrtVert);
  g_prog_sharp = link_program(vs, kRetroSharpFrag);
  g_prog_crt = link_program(vs, kRetroCrtFrag);
  if (!g_prog_sharp || !g_prog_crt) return false;
  glDeleteShader(vs);
  glGenVertexArrays(1, &g_vao);
  // Two textures hold the same frame: UNORM for the sharp program, sRGB (decoded to linear
  // light on fetch) for the CRT program.
  for (GLuint *t : {&g_tex, &g_tex_srgb}) {
    glGenTextures(1, t);
    glBindTexture(GL_TEXTURE_2D, *t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  if (g_offscreen) {  // exact-size RGBA8 target for the screenshot gate
    glGenTextures(1, &g_fbo_tex);
    glBindTexture(GL_TEXTURE_2D, g_fbo_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_opt.out_w, g_opt.out_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &g_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_fbo_tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      fprintf(stderr, "offscreen target %dx%d is not available\n", g_opt.out_w, g_opt.out_h);
      return false;
    }
  }
  return true;
}

void upload_frame() {
  if (!g_frame.valid || !g_gl) return;
  GLenum fmt = GL_RGB, type = GL_UNSIGNED_SHORT_5_6_5;
  GLint internal = GL_RGB8, internal_srgb = GL_SRGB8;
  if (g_frame.fmt == PixelFormat::XRGB8888) { fmt = GL_BGRA; type = GL_UNSIGNED_INT_8_8_8_8_REV; }
  else if (g_frame.fmt == PixelFormat::RGB1555) { fmt = GL_BGRA; type = GL_UNSIGNED_SHORT_1_5_5_5_REV; }
  const void *pixels = present_frame().data();
  const bool resize = g_tex_w != g_frame.w || g_tex_h != g_frame.h;
  const struct { GLuint tex; GLint internal; } targets[2] = {{g_tex, internal}, {g_tex_srgb, internal_srgb}};
  for (const auto &t : targets) {
    glBindTexture(GL_TEXTURE_2D, t.tex);
    if (resize) glTexImage2D(GL_TEXTURE_2D, 0, t.internal, g_frame.w, g_frame.h, 0, fmt, type, pixels);
    else glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_frame.w, g_frame.h, fmt, type, pixels);
  }
  g_tex_w = g_frame.w;
  g_tex_h = g_frame.h;
}

void render(bool swap) {
  if (!g_gl) return;
  int dw, dh;
  target_size(dw, dh);
  if (dw <= 0 || dh <= 0) return;
  glBindFramebuffer(GL_FRAMEBUFFER, g_offscreen ? g_fbo : 0);
  glViewport(0, 0, dw, dh);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  if (g_tex_w > 0) {
    SDL_Rect dst = compute_dest(dw, dh);
    glViewport(dst.x, dst.y, dst.w, dst.h);
    const CrtMode mode = resolve_mode(dst);  // every frame: resize / fullscreen toggle re-resolve
    const GLuint prog = mode == CrtMode::Sharp ? g_prog_sharp : g_prog_crt;
    glUseProgram(prog);
    if (mode == CrtMode::Sharp) {
      glUniform2f(glGetUniformLocation(prog, "srcSize"), static_cast<float>(g_tex_w), static_cast<float>(g_tex_h));
      glUniform2f(glGetUniformLocation(prog, "prescale"), std::max(1.0f, std::ceil(static_cast<float>(dst.w) / g_tex_w)),
                  std::max(1.0f, std::ceil(static_cast<float>(dst.h) / g_tex_h)));
      glBindTexture(GL_TEXTURE_2D, g_tex);
    } else {
      CrtGeometry geo;
      geo.src_w = static_cast<float>(g_tex_w);
      geo.src_h = static_cast<float>(g_tex_h);
      logical_size(geo.log_w, geo.log_h);
      geo.out_w = static_cast<float>(dst.w);
      geo.out_h = static_cast<float>(dst.h);
      GLuint prog_id = prog;
      CrtParams cp = crt_params(mode);
      cp.scanStrength *= g_set.crt_strength / 100.0f;  // player's strength; 100 = the shared preset
      cp.maskStrength *= g_set.crt_strength / 100.0f;
      crt_emit_uniforms(set_uniform, &prog_id, cp, geo);
      glBindTexture(GL_TEXTURE_2D, g_tex_srgb);
    }
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  if (swap && !g_offscreen) SDL_GL_SwapWindow(g_window);
}

static bool save_surface(SDL_Surface *s, const std::string &path) {
  bool ok = SDL_SaveBMP(s, path.c_str()) == 0;
  SDL_FreeSurface(s);
  return ok;
}

// With a GL context: reads back the real composed output (letterbox, shader and
// all). Headless (no GL on the dummy driver): writes the raw core framebuffer.
bool save_screenshot(const std::string &path) {
  if (g_gl) {
    render(false);
    int w, h;
    target_size(w, h);
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4), flipped(px.size());
    glReadBuffer(g_offscreen ? GL_COLOR_ATTACHMENT0 : GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    for (int y = 0; y < h; ++y)
      memcpy(&flipped[static_cast<size_t>(y) * w * 4], &px[static_cast<size_t>(h - 1 - y) * w * 4],
             static_cast<size_t>(w) * 4);
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(flipped.data(), w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
    return s && save_surface(s, path);
  }
  if (!g_frame.valid) return false;
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(const_cast<uint8_t *>(present_frame().data()), g_frame.w, g_frame.h,
                                                      g_frame.bpp * 8, g_frame.w * g_frame.bpp, sdl_format());
  return s && save_surface(s, path);
}

}  // namespace detail
}  // namespace retro_shell
