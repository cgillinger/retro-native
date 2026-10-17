#version 330 core
out vec2 uv;
void main() {
  // Fullscreen triangle (no vertex buffer; draw 3 vertices with an empty VAO).
  // uv (0,0) = top-left of the frame, (1,1) = bottom-right. The viewport must be
  // set to the destination rectangle of the picture: uv is relative to it.
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  uv = vec2(p.x, 1.0 - p.y);
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
