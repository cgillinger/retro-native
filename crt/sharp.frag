#version 330 core
// Sharp-bilinear: texels stay crisp except for a one-output-pixel blend at their
// borders. `prescale` = integer output pixels per source texel (ceil of the scale).
in vec2 uv;
out vec4 frag;
uniform sampler2D tex;
uniform vec2 srcSize;
uniform vec2 prescale;
void main() {
  vec2 texel = uv * srcSize;
  vec2 fl = floor(texel);
  vec2 d = fract(texel) - 0.5;
  vec2 range = 0.5 - 0.5 / prescale;
  vec2 f = (d - clamp(d, -range, range)) * prescale + 0.5;
  frag = vec4(texture(tex, (fl + f) / srcSize).rgb, 1.0);
}
