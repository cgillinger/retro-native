#include "crt.h"

CrtMode crt_auto_mode(double vertical_scale) {
  if (vertical_scale < 3.5) return CrtMode::Sharp;
  if (vertical_scale < 6.0) return CrtMode::Crt;
  return CrtMode::CrtMask;
}

CrtMode crt_resolve(CrtSetting setting, double vertical_scale) {
  switch (setting) {
    case CrtSetting::Sharp: return CrtMode::Sharp;
    case CrtSetting::Crt: return CrtMode::Crt;
    case CrtSetting::CrtMask: return CrtMode::CrtMask;
    case CrtSetting::Auto: break;
  }
  return crt_auto_mode(vertical_scale);
}

CrtParams crt_params(CrtMode mode) {
  switch (mode) {
    case CrtMode::Crt: return {0.35f, 0.30f, 0.45f, 0.65f, 0.0f};
    case CrtMode::CrtMask: return {0.35f, 0.30f, 0.45f, 0.70f, 0.20f};
    case CrtMode::Sharp: break;
  }
  return {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
}

void crt_emit_uniforms(CrtUniformFn set, void *user, const CrtParams &p, const CrtGeometry &g) {
  const float src[2] = {g.src_w, g.src_h}, lg[2] = {g.log_w, g.log_h}, out[2] = {g.out_w, g.out_h};
  set(user, "srcSize", 2, src);
  set(user, "logSize", 2, lg);
  set(user, "outSize", 2, out);
  set(user, "hSoft", 1, &p.hSoft);
  set(user, "beamMin", 1, &p.beamMin);
  set(user, "beamMax", 1, &p.beamMax);
  set(user, "scanStrength", 1, &p.scanStrength);
  set(user, "maskStrength", 1, &p.maskStrength);
}
