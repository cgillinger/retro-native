// retro-native shared CRT filter: mode selection and preset data (renderer independent).
// The GLSL lives next to this file (crt.vert / sharp.frag / crt.frag); see README.md.
#pragma once

enum class CrtMode { Sharp, Crt, CrtMask };
enum class CrtSetting { Auto, Sharp, Crt, CrtMask };

// vertical_scale = viewport height in DRAWABLE pixels / LOGICAL source lines.
// The caller normalises interlace/hi-res first (448 interlaced lines -> 224 logical).
// Auto rule: < 3.5 Sharp, [3.5, 6) Crt, >= 6 CrtMask.
CrtMode crt_auto_mode(double vertical_scale);
CrtMode crt_resolve(CrtSetting setting, double vertical_scale);

// Shader parameters; presets are pure data.
struct CrtParams {
  float hSoft;         // horizontal gaussian sigma, logical texels
  float beamMin;       // beam sigma at black, logical lines
  float beamMax;       // beam sigma at white, logical lines
  float scanStrength;  // 0..1 mix flat -> beam profile
  float maskStrength;  // 0..1 aperture grille depth (0 = off)
};
CrtParams crt_params(CrtMode mode);  // Sharp returns all zeros (unused)

// Geometry for the uniforms that depend on the frame, not the preset.
struct CrtGeometry {
  float src_w, src_h;  // texture size in texels
  float log_w, log_h;  // logical width x logical lines (SNES 256x224, C64 384x272); src is 1x or 2x of it
  float out_w, out_h;  // viewport (picture rectangle) size in output pixels
};

// Loader-agnostic uniform delivery: calls set(user, name, n, values) for every uniform
// of crt.frag (n = 1 or 2 floats). The caller maps that to glUniform1f/2f (or any API).
using CrtUniformFn = void (*)(void *user, const char *name, int n, const float *v);
void crt_emit_uniforms(CrtUniformFn set, void *user, const CrtParams &p, const CrtGeometry &g);
