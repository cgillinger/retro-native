# retro-native CRT reference shader

The shared CRT/Sharp filter for every retro-native game (STANDARD.md sections 5 and 10, "Bild").
GLSL 330 core (OpenGL 3.3). One CRT program covers both CRT presets (CRT, CRT+mask) through
uniforms; `sharp.frag` is the sharp-bilinear base. Games using another renderer port it with
the same parameters (see "Porting"). Proven first in King Arthur's World (`src/shell/main.cpp`
there is the reference integration).

Files: `crt.vert` fullscreen triangle, `sharp.frag`, `crt.frag`, `crt.h/.cpp` (mode selection
and preset data, pure C++, no GL), `crt.cmake` (embeds the shaders, defines `retro_crt`),
`crt_test.cpp` (Auto-rule unit test), `flat_white_sim.py` (offline brightness check),
`gate.sh` (screenshot gate).

## Integrating (GL 3.3 shell)

```cmake
set(RETRO_NATIVE_DIR "${CMAKE_SOURCE_DIR}/../retro-native" CACHE PATH "")
include(${RETRO_NATIVE_DIR}/crt/crt.cmake)        # has an include guard
target_link_libraries(game PRIVATE retro_crt)
```
```cpp
#include "crt.h"
#include "retro_crt_shaders.h"   // kRetroCrtVert, kRetroSharpFrag, kRetroCrtFrag (raw strings)
```
1. Compile/link two programs once: `crt.vert + sharp.frag` and `crt.vert + crt.frag`; look up
   uniform locations once after linking.
2. Each frame, set `glViewport` to the picture rectangle (letterboxed; `uv` in the vertex
   shader is relative to it), pick the mode with
   `crt_resolve(setting, picture_h_px / logical_lines)` (every frame, so resize and fullscreen
   re-resolve), draw 3 vertices with an empty VAO.
3. Sharp: bind a normal (UNORM, GL_LINEAR, CLAMP_TO_EDGE) texture; uniforms `srcSize`
   (texels) and `prescale` = ceil(output px / texel) per axis.
4. Crt / CrtMask: bind the SAME frame as an **sRGB-format texture** (internal format
   `GL_SRGB8`; allowed with any client pixel type, e.g. 565/1555/BGRA8). The shader only uses
   `texelFetch` and expects it to return linear light, so filtering is irrelevant. Keeping one
   UNORM and one sRGB texture of the same frame costs a second tiny upload per frame.
   Set the uniforms with `crt_emit_uniforms(cb, user, crt_params(mode), geometry)`; `cb`
   receives (name, 1 or 2 floats) and calls `glUniform1f/2f`, so no GL loader is assumed.
5. Toasts/menus must already be drawn into the source frame so they pass through the filter.

Why sRGB texture: decoding gamma per tap (`pow`) on every `texelFetch` (4 lines x taps x rows)
made the pass 46 % slower in the KAW measurement (0.495 ms vs 0.340 ms per frame at 2560x1440,
RTX 5060 Ti, glFinish timing over 300 frames). The decode is the exact sRGB curve and the
output encode is the exact sRGB curve too (not pure 2.2).

## Auto rule

`vertical_scale = picture height in drawable pixels / LOGICAL source lines`. The caller
normalises interlace/hi-res (448 interlaced lines count as 224; a 512-wide hi-res frame still
has 256 logical columns). `< 3.5` Sharp, `[3.5, 6)` Crt, `>= 6` CrtMask. Applies to TV/15 kHz
platforms; VGA platforms default to Sharp. `crt_resolve` also takes explicit overrides.

## Algorithm (crt.frag)

All maths in linear light (hardware sRGB decode on fetch, sRGB encode on output).
1. Horizontal: per source line, gaussian in source-texel space, sigma `hSoft` logical texels.
   Taps via `texelFetch` over radius R = ceil(2 * per.x * max(hSoft/0.35, 1)) texels
   (per.x = srcSize.x / logSize.x, 1 or 2), i.e. +-2 logical texels at the 0.35 reference and
   wider for larger hSoft; weights normalised.
2. Vertical: the 4 lines around the pixel (the two nearest line centres plus one more each
   side). Per line the beam sigma = mix(beamMin, beamMax, luminance of that line's colour),
   floored at 1e-3 (so beamMin/Max may not be 0), in line units: brighter = wider. Weight =
   gaussian pdf INTEGRATED over the output pixel's vertical footprint (normal-CDF difference at
   the footprint's top/bottom edge, erf by Abramowitz-Stegun 7.1.26, divided by footprint
   height). Integrating over the footprint, not point-sampling, is the anti-moire requirement at
   fractional scale.
3. Brightness compensation: the unit-integral pdf equals the profile divided by
   sigma*sqrt(2*pi), so the mean over a line period equals the source colour.
   `out = mix(flat, beam, scanStrength)`; flat = plain vertical lerp between the two nearest
   lines. Tone curve: identity on [0,1], hard clamp above (no soft knee, so flat white is not
   dimmed).
4. Mask (maskStrength > 0, CrtMask only): aperture grille, R/G/B stripes, period 3 OUTPUT
   pixels (`floor(uv.x * outSize.x) % 3`), `m = (1-s) + s*onehot`, divided by its mean
   `1 - s + s/3`.
5. Edge lines: line indices are clamped, so the first/last line keeps its own colour and the
   picture does not fall off into the border (intended; black bars are the shell's job).
6. No curvature, bloom or noise.

Hi-res/interlace: `srcSize` may be 2x `logSize` per axis; extra columns widen the gaussian
taps, extra rows are averaged per logical line. `logSize` is generic: logical width x logical
lines (SNES 256x224, C64 384x272, ...).

## Parameters (uniforms)

| uniform | meaning | range | Crt | CrtMask |
|---|---|---|---|---|
| `srcSize` | texture size, texels | - | frame | frame |
| `logSize` | logical width x lines | - | frame | frame |
| `outSize` | picture rectangle, output px | - | frame | frame |
| `hSoft` | horizontal sigma, logical texels | 0.2-0.6 | 0.35 | 0.35 |
| `beamMin` | beam sigma at black, lines (min 1e-3) | 0.2-0.4 | 0.30 | 0.30 |
| `beamMax` | beam sigma at white, lines | 0.35-0.55 | 0.45 | 0.45 |
| `scanStrength` | flat -> beam mix | 0-1 | 0.65 | 0.70 |
| `maskStrength` | grille depth | 0-0.4 | 0 | 0.20 |

## Brightness

Verified offline with `flat_white_sim.py` (mirrors the shader maths, flat white frame, mean
linear output, 1.0 = unchanged): Crt 0.993 at scales 3.5-6; CrtMask 0.949. The Crt loss
(0.7 %) is the truncated beam tails (4 lines). The CrtMask loss is inherent: a mask that
darkens two of three subpixels cannot keep a pure-white frame at mean 1.0 once outputs are
clamped at 1 (the mean-normalising gain 1/(1-s+s/3) = 1.15 is clipped on white). Content
below white loses less; the gate measures real frames (about +2..7 % vs Sharp in linear light,
spread blur raises linear energy slightly at the picture edges).
To trade mask visibility for white level lower `maskStrength` (0.05 gives 0.988).

## Porting to a non-GL renderer

Same maths per output pixel. Inputs: the source frame with exact point fetch of LINEAR values
(pre-linearise to an RGB16F texture if the API has no sRGB texture format; do not decode with
`pow` inside the tap loops), the pixel's position within the picture rectangle, and the
uniform table. Reuse `crt.h/crt.cpp` unchanged for mode selection and presets;
`crt_emit_uniforms` writes values into your constant buffer. HLSL/Metal/Vulkan: translate
`crt.frag` literally (only `texelFetch`, `mix`, `clamp`, `step`, `pow`, `exp`).

## Contract and screenshot gate

Every game's shell must support these flags (used by `gate.sh`):
`--frames N` (run N emulated frames, then exit), `--out-size WxH` (render the final composed
image, letterbox + shader, into an offscreen target of exactly WxH; hidden window is fine;
requires `--frames` and `--screenshot`, no audio), `--shader sharp|crt|crtmask`,
`--screenshot FILE` (BMP accepted; gate.sh derives PNG and crops).

```
crt/gate.sh <exe> <outdir> [frames...] [-- extra exe args]
```
Defaults: frames 300 and 1500, sizes 1920x1080 and 2560x1440, shaders sharp/crt/crtmask.
Needs a real GL context (not the SDL dummy video driver). Each image is deleted before its run
and its size is checked against WxH (failure = exit 1). Outputs per run:
`<frames>_<H>p_<shader>.bmp/.png`, a 480x270 1:1 centre crop `*_crop.png`, and `index.txt`
with the list of this run's files and, per frame/size, the ratio of mean LINEAR luminance
(mean of (v/255)^2.2 over the non-black picture rectangle) of CRT/CRT+mask vs Sharp with
PASS/FAIL against +-10 %. Then look at the crops: no moire or banding at fractional scale.
This gate must be run and reviewed before Auto is locked for a game.

Unit test of the Auto rule: `c++ -std=c++17 crt_test.cpp crt.cpp -o crt_test && ./crt_test`.
