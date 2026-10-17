// Settings: ini-backed, applied live. The ini holds what the player chose; per-run command
// line overrides only touch the in-memory values.
#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "internal.h"

namespace retro_shell {
namespace detail {

Settings g_set;
Ini g_ini;

namespace {

bool parse_bool(const std::string &s, bool def) {
  std::string l = s;
  for (auto &c : l) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  if (l == "true" || l == "1" || l == "yes" || l == "on") return true;
  if (l == "false" || l == "0" || l == "no" || l == "off") return false;
  return def;
}
const char *bstr(bool b) { return b ? "true" : "false"; }
const char *kFilterNames[] = {"auto", "sharp", "crt", "crtmask"};
const char *kVsyncNames[] = {"auto", "on", "off"};
const char *kBufNames[] = {"auto", "small", "medium", "large"};

std::string default_filter_name() { return g_game->signal == Signal::VGA ? "sharp" : "auto"; }

void apply_audio() {
  audio_set_volume(g_set.volume, g_set.mute);
  audio_set_buffer_mode(g_set.audio_buffer);
}

int clampi(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }


}  // namespace

void settings_load() {
  std::string path;
  if (g_test_run) {
    path = g_opt.ini;  // test runs never touch the player's ini
  } else {
    const char *name = g_game->ini_name && *g_game->ini_name ? g_game->ini_name : "settings";
    path = save_dir() + "/" + name + ".ini";
  }
  g_ini.open(path);
  g_set = Settings();
  g_set.filter = default_filter_name() == "sharp" ? FILTER_SHARP : FILTER_AUTO;
  std::string v;
  if (g_ini.get("video", "fullscreen", v)) g_set.fullscreen = parse_bool(v, g_set.fullscreen);
  if (g_ini.get("video", "scaling", v)) g_set.integer = v == "integer";
  if (g_ini.get("video", "filter", v))
    for (int i = 0; i < FILTER_COUNT; ++i)
      if (v == kFilterNames[i]) g_set.filter = i;
  if (g_ini.get("video", "vsync", v)) {  // auto | on | off (true/false from older files)
    std::string l = v;
    for (auto &c : l) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    for (int i = 0; i < VSYNC_COUNT; ++i)
      if (l == kVsyncNames[i]) g_set.vsync = i;
    if (l == "true" || l == "1") g_set.vsync = VSYNC_ON;
    if (l == "false" || l == "0") g_set.vsync = VSYNC_OFF;
  }
  if (g_ini.get("video", "crt_strength", v)) g_set.crt_strength = clampi(atoi(v.c_str()), 0, 100);
  if (g_ini.get("video", "border", v)) g_set.game_border = parse_bool(v, true);
  if (g_ini.get("video", "frame_delay", v)) g_set.frame_delay = parse_bool(v, false);
  if (g_ini.get("video", "aspect", v))
    for (size_t i = 0; i < g_game->aspects.size(); ++i)
      if (g_game->aspects[i].id == v) g_set.aspect = static_cast<int>(i);
  if (g_ini.get("audio", "buffer", v))
    for (int i = 0; i < ABUF_COUNT; ++i)
      if (v == kBufNames[i]) g_set.audio_buffer = i;
  if (g_ini.get("mouse", "enabled", v)) g_set.mouse_on = parse_bool(v, true);
  if (g_ini.get("mouse", "sensitivity", v)) g_set.mouse_sens = clampi(atoi(v.c_str()), 20, 300);
  if (g_ini.get("audio", "volume", v)) g_set.volume = std::max(0, std::min(100, atoi(v.c_str())));
  if (g_ini.get("audio", "mute", v)) g_set.mute = parse_bool(v, g_set.mute);
  if (g_ini.get("general", "pause_on_focus_loss", v)) g_set.pause_on_focus_loss = parse_bool(v, true);
  if (g_ini.get("general", "first_run_done", v)) g_set.first_run_done = parse_bool(v, false);
  if (g_ini.get("general", "show_start_help", v)) g_set.show_start_help = parse_bool(v, true);
  vlog("settings: volume=%d vsync=%d filter=%d border=%d aspect=%d crt=%d buffer=%d\n", g_set.volume, g_set.vsync, g_set.filter,
       g_set.game_border, g_set.aspect, g_set.crt_strength, g_set.audio_buffer);
  // Per-run overrides (not written back).
  if (g_opt.fullscreen >= 0) g_set.fullscreen = g_opt.fullscreen != 0;
  if (g_opt.integer_scale >= 0) g_set.integer = g_opt.integer_scale != 0;
  if (g_opt.filter >= 0) g_set.filter = g_opt.filter;
}

void set_fullscreen(bool on, bool toast_it) {
  g_set.fullscreen = on;
  vlog("fullscreen -> %d\n", on);
  g_ini.set("video", "fullscreen", bstr(on));
  if (g_window) apply_fullscreen(on);
  if (toast_it) toast(on ? "FULLSCREEN" : "WINDOW");
}
void set_integer(bool on) {
  g_set.integer = on;
  g_ini.set("video", "scaling", on ? "integer" : "fit");
}
void set_filter(int f) {
  g_set.filter = f;
  g_ini.set("video", "filter", kFilterNames[f]);
}
void set_vsync(int mode) {
  g_set.vsync = clampi(mode, 0, VSYNC_COUNT - 1);
  g_ini.set("video", "vsync", kVsyncNames[g_set.vsync]);
  video_apply_vsync(g_set.vsync, g_boot_hold);
}
void set_crt_strength(int pct) {
  g_set.crt_strength = clampi(pct, 0, 100);
  g_ini.set("video", "crt_strength", std::to_string(g_set.crt_strength));
}
void set_aspect(int idx) {
  g_set.aspect = idx;
  g_ini.set("video", "aspect", aspect_id(static_cast<size_t>(idx)));
  video_update_aspect();
}
void set_game_border(bool on) {
  g_set.game_border = on;
  g_ini.set("video", "border", bstr(on));
  video_rebuild();
}
void set_frame_delay(bool on) {
  g_set.frame_delay = on;
  g_ini.set("video", "frame_delay", bstr(on));
}
void set_audio_buffer(int mode) {
  g_set.audio_buffer = clampi(mode, 0, ABUF_COUNT - 1);
  g_ini.set("audio", "buffer", kBufNames[g_set.audio_buffer]);
  apply_audio();
}
void set_show_start_help(bool on) {
  g_set.show_start_help = on;
  g_ini.set("general", "show_start_help", bstr(on));
}
void set_mouse_on(bool on) {
  g_set.mouse_on = on;
  g_ini.set("mouse", "enabled", bstr(on));
}
void set_mouse_sens(int pct) {
  g_set.mouse_sens = clampi(pct, 20, 300);
  g_ini.set("mouse", "sensitivity", std::to_string(g_set.mouse_sens));
}
void set_volume(int v) {
  g_set.volume = std::max(0, std::min(100, v));
  g_ini.set("audio", "volume", std::to_string(g_set.volume));
  apply_audio();
}
void set_mute(bool on) {
  g_set.mute = on;
  g_ini.set("audio", "mute", bstr(on));
  apply_audio();
}
void set_pause_on_focus_loss(bool on) {
  g_set.pause_on_focus_loss = on;
  g_ini.set("general", "pause_on_focus_loss", bstr(on));
}

std::string option_value(size_t idx) {
  const GameOption &o = g_game->options[idx];
  std::string v;
  if (g_ini.get(o.section, o.key, v))
    for (const auto &c : o.choices)
      if (c.value == v) return v;
  return o.def;
}

void set_option(size_t idx, const std::string &value) {
  const GameOption &o = g_game->options[idx];
  g_ini.set(o.section, o.key, value);
  if (!o.core_key.empty() && g_backend) g_backend->set_option(o.core_key, value);
  if (g_game->on_option) g_game->on_option(o.key, value);
}

void reset_all_settings() {
  const bool keep_first_run = g_set.first_run_done;
  const bool was_fullscreen = g_set.fullscreen;
  g_set = Settings();
  g_set.filter = default_filter_name() == "sharp" ? FILTER_SHARP : FILTER_AUTO;
  g_set.first_run_done = keep_first_run;
  g_ini.set("general", "show_start_help", "true");
  g_ini.set("video", "fullscreen", bstr(g_set.fullscreen));
  g_ini.set("video", "scaling", "fit");
  g_ini.set("video", "filter", default_filter_name());
  g_ini.set("video", "vsync", kVsyncNames[g_set.vsync]);
  g_ini.set("video", "crt_strength", std::to_string(g_set.crt_strength));
  g_ini.set("video", "border", bstr(g_set.game_border));
  g_ini.set("video", "frame_delay", bstr(g_set.frame_delay));
  g_ini.set("video", "aspect", aspect_id(0));
  g_ini.set("audio", "buffer", kBufNames[g_set.audio_buffer]);
  if (g_game->uses_mouse) {
    g_ini.set("mouse", "enabled", bstr(g_set.mouse_on));
    g_ini.set("mouse", "sensitivity", std::to_string(g_set.mouse_sens));
  }
  g_ini.set("audio", "volume", std::to_string(g_set.volume));
  g_ini.set("audio", "mute", bstr(g_set.mute));
  g_ini.set("general", "pause_on_focus_loss", bstr(g_set.pause_on_focus_loss));
  if (was_fullscreen != g_set.fullscreen && g_window) apply_fullscreen(g_set.fullscreen);
  video_apply_vsync(g_set.vsync, g_boot_hold);
  video_update_aspect();
  video_rebuild();
  apply_audio();
  for (size_t i = 0; i < g_game->options.size(); ++i) set_option(i, g_game->options[i].def);
  ctl_reset();
}

std::vector<CoreOption> option_core_overrides() {
  std::vector<CoreOption> out;
  for (size_t i = 0; i < g_game->options.size(); ++i)
    if (!g_game->options[i].core_key.empty()) out.emplace_back(g_game->options[i].core_key, option_value(i));
  return out;
}

}  // namespace detail
}  // namespace retro_shell
