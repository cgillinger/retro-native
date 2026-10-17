// Toast: a short message drawn into the picture's own pixel grid (so it passes through the
// same filter), bottom-left.
#include <string>

#include "internal.h"
#include "retro_shell/log.h"

namespace retro_shell {
namespace {

std::string g_toast_text;
Uint32 g_toast_start = 0;
}  // namespace

void toast(const char *text) {
  vlog("toast: %s\n", detail::glyph_utf8(text).c_str());
  g_toast_text = text;
  g_toast_start = detail::now_ms();
}

bool is_test_run() { return detail::g_test_run; }

namespace detail {

bool toast_pending() { return !g_toast_text.empty(); }
bool toast_visible() { return toast_alpha() > 0.0f; }

// 1.0 for the first 1.1 s, fading to 0 over the last 0.4 s; 0 when inactive.
float toast_alpha() {
  if (g_toast_text.empty()) return 0.0f;
  const Uint32 age = now_ms() - g_toast_start;
  if (age < 1100) return 1.0f;
  if (age < 1500) return static_cast<float>(1500 - age) / 400.0f;
  g_toast_text.clear();
  return 0.0f;
}

// Draws the toast into `f` in the game's logical pixel grid.
void toast_draw(Frame &f, float alpha) {
  draw_text(f, 8, f.h - 8 - 7, g_toast_text, 0xF2E6C6, alpha, 1, true);
}

}  // namespace detail
}  // namespace retro_shell
