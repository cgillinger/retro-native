#include <cstdlib>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "internal.h"

namespace retro_shell {
namespace detail {

bool parse_args(int argc, char **argv, const Game &game) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> const char * { return i + 1 < argc ? argv[++i] : nullptr; };
    if (a == "--test-toast") {
      g_opt.test_toast = true;
    } else if (a == "--integer-scale") {
      g_opt.integer_scale = 1;
    } else if (a == "--frames") {
      const char *v = next();
      if (!v) return false;
      g_opt.frames = atoi(v);
    } else if (a == "--screenshot") {
      const char *v = next();
      if (!v) return false;
      g_opt.screenshot = v;
      std::string ext = g_opt.screenshot.size() >= 4 ? g_opt.screenshot.substr(g_opt.screenshot.size() - 4) : "";
      for (auto &c : ext) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
      if (ext != ".bmp") {
        fprintf(stderr, "--screenshot only writes BMP: use a .bmp path\n");
        return false;
      }
    } else if (a == "--shader") {
      const char *v = next();
      if (!v) return false;
      if (!strcmp(v, "auto")) g_opt.filter = FILTER_AUTO;
      else if (!strcmp(v, "sharp")) g_opt.filter = FILTER_SHARP;
      else if (!strcmp(v, "crt")) g_opt.filter = FILTER_CRT;
      else if (!strcmp(v, "crtmask")) g_opt.filter = FILTER_CRTMASK;
      else return false;
    } else if (a == "--out-size") {
      const char *v = next();
      int w = 0, h = 0;
      if (!v || sscanf(v, "%dx%d", &w, &h) != 2 || w < 16 || h < 16 || w > 16384 || h > 16384) return false;
      g_opt.out_w = w;
      g_opt.out_h = h;
    } else if (a == "--test-vpads") {  // N[:ps|:nin]: virtual gamepads for the pad tests
      const char *v = next();
      if (!v) return false;
      g_opt.test_vpads = atoi(v);
      if (strstr(v, ":ps")) g_opt.test_vpad_type = 1;
      else if (strstr(v, ":nin")) g_opt.test_vpad_type = 2;
    } else if (a == "--test-card") {
      g_opt.test_card = true;
    } else if (a == "--no-boot-hold") {
      g_opt.no_boot_hold = true;
    } else if (a == "--write-boot-state") {
      const char *v = next();
      if (!v) return false;
      g_opt.write_boot_state = v;
    } else if (a == "--boot-state") {
      const char *v = next();
      if (!v) return false;
      g_opt.boot_state_file = v;
    } else if (a == "--no-boot-state") {
      g_opt.no_boot_state = true;
    } else if (a == "--press") {
      const char *v = next();
      if (!v || !script_parse(v)) return false;
    } else if (a == "--ini") {
      const char *v = next();
      if (!v) return false;
      g_opt.ini = v;
    } else if (a == "--test-hint") {
      g_opt.test_hint = true;
    } else if (a == "--fullscreen") {
      g_opt.fullscreen = 1;
    } else if (a == "--window") {
      g_opt.fullscreen = 0;
    } else if (a == "--verbose") {
      g_opt.verbose = true;
    } else {
      bool handled = false;
      for (const auto &f : game.cli_flags) {
        if (a != f.name) continue;
        const char *v = nullptr;
        if (f.takes_value && !(v = next())) return false;
        if (f.handler && !f.handler(v)) return false;
        handled = true;
        break;
      }
      if (!handled) return false;
    }
  }
  if (const char *v = getenv(env_name("VERBOSE").c_str()))
    if (*v && strcmp(v, "0") != 0) g_opt.verbose = true;
  // The offscreen target is a screenshot facility: it needs a fixed frame count and a file.
  if (g_opt.out_w > 0 && (g_opt.frames < 0 || g_opt.screenshot.empty())) {
    fprintf(stderr, "--out-size needs --frames and --screenshot\n");
    return false;
  }
  return true;
}

}  // namespace detail
}  // namespace retro_shell
