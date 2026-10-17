#version 330 core
// Shared retro-native CRT shader. One program, two presets (CRT, CRT+mask) selected
// purely by uniform values. See README.md for the algorithm.
// INPUT CONTRACT: `tex` must deliver LINEAR light from texelFetch, i.e. bind the frame as an
// sRGB-format texture (GL_SRGB8; hardware decode is free). Output is sRGB-encoded here.
in vec2 uv;
out vec4 frag;

uniform sampler2D tex;    // source frame as an sRGB-format texture, only used with texelFetch
uniform vec2  srcSize;    // texture size in texels (may be 2x the logical size: hires/interlace)
uniform vec2  logSize;    // logical size: logical width x logical lines (e.g. 256x224 SNES, 384x272 C64);
                          // srcSize is 1x or 2x of it per axis
uniform vec2  outSize;    // viewport (picture rectangle) size in output pixels
uniform float hSoft;      // horizontal gaussian sigma, in logical texels
uniform float beamMin;    // vertical beam sigma for black, in logical lines
uniform float beamMax;    // vertical beam sigma for white, in logical lines
uniform float scanStrength; // 0 = no scanlines (flat), 1 = full beam profile
uniform float maskStrength; // 0 = no aperture grille

const float SQRT1_2 = 0.70710678;

// Exact sRGB encode (matches the hardware decode of an sRGB texture).
vec3 srgb_encode(vec3 c) {
  return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

// Abramowitz-Stegun 7.1.26, |error| < 1.5e-7.
float erf_as(float x) {
  float s = sign(x);
  x = abs(x);
  float t = 1.0 / (1.0 + 0.3275911 * x);
  float y = 1.0 - (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t - 0.284496736) * t + 0.254829592) * t * exp(-x * x);
  return s * y;
}
// Standard normal CDF.
float cdf(float x) { return 0.5 * (1.0 + erf_as(x * SQRT1_2)); }

// Colour of logical line `line` (linear light) at physical texel coordinate `xt`,
// after the horizontal gaussian (tap radius R texels, sigma sigmaP texels). Rows of a doubled (interlaced) frame are averaged.
vec3 line_colour(int line, float xt, vec2 per, int R, float sigmaP) {
  int rows = int(per.y + 0.5);
  int base = line * rows;
  int maxx = int(srcSize.x) - 1;
  int x0 = int(floor(xt));
  vec3 acc = vec3(0.0);
  float wsum = 0.0;
  for (int d = -R; d <= R; ++d) {
    int x = x0 + d;
    float dx = (float(x) + 0.5 - xt) / sigmaP;
    float w = exp(-0.5 * dx * dx);
    vec3 c = vec3(0.0);
    for (int r = 0; r < rows; ++r)
      c += texelFetch(tex, ivec2(clamp(x, 0, maxx), base + r), 0).rgb;
    acc += w * c / float(rows);
    wsum += w;
  }
  return acc / wsum;
}

void main() {
  vec2 per = srcSize / logSize;                 // texels per logical texel (1 or 2)
  float xt = uv.x * srcSize.x;
  float sigmaP = max(hSoft * per.x, 0.05);
  // Tap radius: +-2 logical texels at the 0.35 reference, growing with hSoft.
  int R = int(ceil(2.0 * per.x * max(hSoft / 0.35, 1.0)));

  float y = uv.y * logSize.y;                   // position in logical lines; line i spans [i, i+1)
  float h = logSize.y / outSize.y;              // output pixel footprint height in lines
  float ya = y - 0.5 * h, yb = y + 0.5 * h;
  int a = int(floor(y - 0.5));                  // line centres a+0.5 <= y < a+1.5
  int maxl = int(logSize.y) - 1;

  vec3 beam = vec3(0.0);
  vec3 flatc = vec3(0.0);
  for (int k = -1; k <= 2; ++k) {
    int i = a + k;
    // Edge lines: the index is clamped, so the first/last line keeps its own colour and
    // there is no falloff into the border (intentional; the border is the shell's job).
    vec3 c = line_colour(clamp(i, 0, maxl), xt, per, R, sigmaP);
    float L = dot(c, vec3(0.2126, 0.7152, 0.0722));
    // Linear-light luminance in [0,1]; brighter lines get a wider beam.
    float sigma = max(mix(beamMin, beamMax, clamp(L, 0.0, 1.0)), 1e-3);  // floor: avoids /0
    float centre = float(i) + 0.5;
    // Gaussian pdf (unit integral == the profile divided by sigma*sqrt(2*pi)) integrated
    // over the pixel footprint: anti-moire at fractional scale, mean brightness preserved.
    float wgt = (cdf((yb - centre) / sigma) - cdf((ya - centre) / sigma)) / h;
    beam += wgt * c;
    if (k == 0) flatc += (1.0 - fract(y - 0.5)) * c;   // flat = plain vertical lerp
    if (k == 1) flatc += fract(y - 0.5) * c;
  }
  vec3 col = mix(flatc, beam, scanStrength);

  if (maskStrength > 0.0) {
    // Aperture grille: R,G,B stripes, period 3 output pixels; normalised by the mean.
    int px = int(floor(uv.x * outSize.x)) % 3;
    vec3 stripe = vec3(px == 0, px == 1, px == 2);
    vec3 m = (1.0 - maskStrength) + maskStrength * stripe;
    col *= m / (1.0 - maskStrength + maskStrength / 3.0);
  }

  // Tone curve: identity on [0,1], hard clamp above (the beam peak of dark lines and the
  // mask gain can exceed 1 only briefly; flat white stays exactly 1 on average).
  col = clamp(col, 0.0, 1.0);
  frag = vec4(srgb_encode(col), 1.0);
}
