// Platform glue: message box, DPI awareness, fullscreen.
#include <algorithm>
#include <cmath>

#include "internal.h"

namespace retro_shell {

void show_error(const std::string &msg) {
  fprintf(stderr, "%s\n", msg.c_str());
  if (detail::g_opt.frames < 0)
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, detail::g_game ? detail::g_game->title : "", msg.c_str(), nullptr);
}

namespace detail {

void platform_init_hints() {
  SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  // Buttons keep their position (SDL's A = south) on Nintendo pads too; only the labels differ.
  SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
}

// The display the window is on; before the window exists, the one under the mouse pointer.
int pick_display() {
  if (g_window) {
    const int d = SDL_GetWindowDisplayIndex(g_window);
    if (d >= 0) return d;
  }
  int mx = 0, my = 0;
  SDL_GetGlobalMouseState(&mx, &my);
  for (int i = 0; i < SDL_GetNumVideoDisplays(); ++i) {
    SDL_Rect r;
    if (SDL_GetDisplayBounds(i, &r) == 0 && mx >= r.x && my >= r.y && mx < r.x + r.w && my < r.y + r.h) return i;
  }
  return 0;
}

// Largest whole multiple of the picture height that fits 90% of the usable desktop height,
// counted in drawable pixels (HiDPI), returned in window coordinates.
void windowed_size(int &w, int &h) {
  const int base_h = g_base_h > 0 ? g_base_h : 240;
  int scale = 3;
  double ratio = 1.0;  // drawable pixels per window coordinate
  SDL_Rect usable;
  if (g_window) {
    int ww = 0, wh = 0, dw = 0, dh = 0;
    SDL_GetWindowSize(g_window, &ww, &wh);
    if (g_gl) SDL_GL_GetDrawableSize(g_window, &dw, &dh);
    if (ww > 0 && dw > 0) ratio = static_cast<double>(dw) / ww;
    int disp = SDL_GetWindowDisplayIndex(g_window);
    if (disp < 0) disp = 0;
    if (!g_gl) {
      // headless: no real display
    } else if (SDL_GetDisplayUsableBounds(disp, &usable) == 0 && usable.h > 0) {
      scale = std::max(1, static_cast<int>(usable.h * ratio * 0.9 / base_h));
    }
  }
  const int px_h = base_h * scale;
  const int px_w = static_cast<int>(std::floor(px_h * g_aspect + 1e-6));
  h = std::max(1, static_cast<int>(px_h / ratio + 0.5));
  w = std::max(1, static_cast<int>(px_w / ratio + 0.5));
}

void apply_fullscreen(bool on) {
  if (!g_window) return;
  const bool is_full = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
  if (on == is_full) return;
  if (on) {
    SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    return;
  }
  SDL_SetWindowFullscreen(g_window, 0);
  int w, h;
  windowed_size(w, h);
  SDL_SetWindowSize(g_window, w, h);
  SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED_DISPLAY(pick_display()),
                        SDL_WINDOWPOS_CENTERED_DISPLAY(pick_display()));
}

void toggle_fullscreen() {
  const bool is_full = g_window && (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
  set_fullscreen(!is_full, true);
}

}  // namespace detail
}  // namespace retro_shell
