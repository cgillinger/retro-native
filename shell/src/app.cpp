// Application: SDL setup, window, event loop and frame pacing.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "internal.h"
#include "retro_shell/desktop.h"

namespace retro_shell {
namespace detail {

Options g_opt;
const Game *g_game = nullptr;
Backend *g_backend = nullptr;
bool g_boot_hold = false;
bool g_has_focus = true;
bool g_test_run = false;

namespace {

bool g_shutdown_requested = false;
bool g_sdl_inited = false;
bool g_minimized = false;
bool g_frame_produced = false;  // the backend delivered a new picture since the last present
int g_boot_runs = 0;            // run_frame() calls made while the boot hold was active
bool g_rel_mouse = false;       // relative mouse mode as the shell wants it
bool g_hold_hook_released = false;  // the boot hold ended because the game's hook said so (not a timeout)

class BootCtl final : public BootControl {
 public:
  void key(unsigned code, bool down) override {
    vlog("boot key %u %s (frame %d)\n", code, down ? "down" : "up", g_boot_runs);
    g_backend->key_event(code, down);
  }
  int frame() const override { return g_boot_runs; }
};
BootCtl g_boot_ctl;

// Called for every video frame while the boot hold is active.
void hold_boot_frame() {
  const BootHold &bh = g_game->boot;
  FrameView view;
  view.data = g_frame.data.data();
  view.width = g_frame.w;
  view.height = g_frame.h;
  view.bytes_per_pixel = g_frame.bpp;
  view.format = g_frame.fmt;
  if (bh.on_frame && bh.on_frame(view, g_boot_ctl)) {
    g_boot_hold = false;
    g_hold_hook_released = true;
    return;
  }
  std::fill(g_frame.data.begin(), g_frame.data.end(), 0);
}

// The safety net counts run_frame() calls, not delivered pictures: a core that repeats or
// skips frames must not keep the window hidden forever.
void hold_timeout_check() {
  if (!g_boot_hold || g_boot_runs <= g_game->boot.max_frames) return;
  g_boot_hold = false;
  if (g_game->boot.on_release) g_game->boot.on_release(BootRelease::Timeout, g_boot_ctl);
}

class ShellHost final : public Host {
 public:
  void video(const FrameDesc &f) override {
    video_submit(f);
    if (f.data && f.width && f.height) g_frame_produced = true;
    if (g_boot_hold && g_frame.valid && f.data && f.width && f.height) hold_boot_frame();
  }
  size_t audio(const int16_t *data, size_t frames) override {
    if (g_boot_hold) return frames;  // boot/load noise is never played
    return audio_push(data, frames);
  }
  const std::string &save_dir() const override { return retro_shell::save_dir(); }
  void request_shutdown() override { g_shutdown_requested = true; }
};
ShellHost g_host;

void shutdown_all() {
  if (g_backend) g_backend->shutdown();
  audio_close();
  input_close_pads();
  if (g_gl) SDL_GL_DeleteContext(g_gl);
  g_gl = nullptr;
  if (g_window) SDL_DestroyWindow(g_window);
  g_window = nullptr;
  if (g_sdl_inited) SDL_Quit();
  g_sdl_inited = false;
}

int fail(const std::string &msg) {
  show_error(msg);
  shutdown_all();
  return 1;
}

}  // namespace

void request_quit() { g_shutdown_requested = true; }

void open_menu(bool settings) {
  if (g_boot_hold || menu_open()) return;
  input_release_all();  // nothing the player was holding may stay pressed in the paused game
  menu_show(settings);
}

// Quick slot load (F9, script).
void load_state() {
  const bool ok = load_slot(0);
  if (ok) {
    menu_close();
    if (g_boot_hold) {  // a loaded state is a running game: stop hiding/warping at once
      g_boot_hold = false;
      if (g_game->boot.on_release) g_game->boot.on_release(BootRelease::StateLoaded, g_boot_ctl);
    }
    toast("LOADED");
  } else {
    toast("NO SAVE FOUND");
  }
}

namespace {

// Alt+Enter is a shell hotkey and must not reach the game at all, Alt included. A pressed Alt
// is therefore held back until the next key shows what it is for; a plain Alt tap is replayed.
SDL_Scancode g_alt_pending = SDL_SCANCODE_UNKNOWN, g_alt_sent = SDL_SCANCODE_UNKNOWN;
bool g_alt_consumed = false, g_enter_swallowed = false;

void to_game(SDL_Scancode sc, bool down) {
  if (!menu_open()) input_key_event(sc, down);
}


void flush_alt() {
  if (g_alt_pending == SDL_SCANCODE_UNKNOWN) return;
  to_game(g_alt_pending, true);
  g_alt_sent = g_alt_pending;
  g_alt_pending = SDL_SCANCODE_UNKNOWN;
}

}  // namespace

void shell_key(SDL_Scancode sc, bool down, bool repeat, SDL_Keycode keycode) {
  // AltGr: Windows reports it as a fake Left Ctrl immediately followed by Right Alt (same
  // timestamp). That Ctrl must never act as a key (a game function): it is retracted when the Alt arrives
  // and its release is swallowed. (Linux/X11/Wayland send Right Alt only.)
  static bool lctrl_down = false, fake_lctrl = false;
  static Uint32 lctrl_ts = 0;
  if (sc == SDL_SCANCODE_LCTRL) {
    if (down && fake_lctrl) return;
    if (!down) {
      lctrl_down = false;
      if (fake_lctrl) { fake_lctrl = false; return; }
    } else if (!repeat) {
      lctrl_down = true;
      lctrl_ts = now_ms();
    }
  } else if (sc == SDL_SCANCODE_RALT && down && !repeat && lctrl_down && now_ms() - lctrl_ts <= 3) {
    fake_lctrl = true;
    lctrl_down = false;
    input_key_released(SDL_SCANCODE_LCTRL);
    if (!menu_open()) input_key_event(SDL_SCANCODE_LCTRL, false);
  }
  const bool is_alt = sc == SDL_SCANCODE_LALT || sc == SDL_SCANCODE_RALT;
  const bool is_enter = sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER;
  if (is_alt) {
    if (down) {
      if (!repeat && g_alt_sent != sc) g_alt_pending = sc;
    } else if (g_alt_pending == sc) {
      g_alt_pending = SDL_SCANCODE_UNKNOWN;
      if (g_alt_consumed) {
        g_alt_consumed = false;
      } else {
        to_game(sc, true);
        to_game(sc, false);
      }
    } else if (g_alt_sent == sc) {
      g_alt_sent = SDL_SCANCODE_UNKNOWN;
      to_game(sc, false);
    } else {
      g_alt_consumed = false;
    }
    return;
  }
  if (is_enter) {
    if (down && (g_alt_pending != SDL_SCANCODE_UNKNOWN || g_alt_consumed)) {
      if (!repeat) {
        if (menu_capturing()) toast("ALT+ENTER IS RESERVED");  // refused while rebinding
        else toggle_fullscreen();
      }
      g_alt_pending = SDL_SCANCODE_UNKNOWN;
      g_alt_consumed = g_enter_swallowed = true;
      return;
    }
    if (!down && g_enter_swallowed) {
      g_enter_swallowed = false;
      return;
    }
  }
  // A key that went down while a menu was open belongs to the menu: its release (after the
  // menu closed) must not reach the game either.
  static bool menu_key[SDL_NUM_SCANCODES] = {};
  if (down && !repeat && menu_open()) menu_key[sc] = true;
  if (!down && menu_key[sc]) {
    menu_key[sc] = false;
    input_key_released(sc);
    return;
  }
  if (down) flush_alt();
  if (!down) input_key_released(sc);
  if (down) {
    if (menu_card_active()) {  // first-run controls card: any key dismisses it
      if (!repeat) menu_card_dismiss();
      return;
    }
    if (menu_capturing()) {  // rebinding prompt: the next key is the answer
      if (!repeat) menu_capture_key(sc);
      return;
    }
    if (!repeat) device_used(Device::Keyboard);
  }

  // Reserved shell keys: never forwarded, press or release.
  switch (sc) {
    case SDL_SCANCODE_ESCAPE:
      if (down && !repeat && !g_boot_hold) {
        if (menu_open()) menu_nav(Nav::Back);
        else open_menu(false);
      }
      return;
    case SDL_SCANCODE_F1:
      if (g_game->game_owns_f1) break;  // the game claims F1: forwarded like any other key
      if (down && !repeat && !g_boot_hold) {
        if (!menu_open()) open_menu(true);
        else menu_toggle_settings();
      }
      return;
    case SDL_SCANCODE_F5: if (down && !repeat) save_state(); return;
    case SDL_SCANCODE_F9: if (down && !repeat) load_state(); return;
    case SDL_SCANCODE_F11: if (down && !repeat) toggle_fullscreen(); return;
    default: break;
  }
  if (!menu_open()) {
    if (repeat) return;  // auto-repeat is never sent to the game as new key-downs
    input_key_event(sc, down, keycode);
    return;
  }
  if (!down) return;
  switch (sc) {
    case SDL_SCANCODE_UP: menu_nav(Nav::Up); break;
    case SDL_SCANCODE_DOWN: menu_nav(Nav::Down); break;
    case SDL_SCANCODE_LEFT: menu_nav(Nav::Left); break;
    case SDL_SCANCODE_RIGHT: menu_nav(Nav::Right); break;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
    case SDL_SCANCODE_SPACE: if (!repeat) menu_nav(Nav::Select); break;
    case SDL_SCANCODE_BACKSPACE: if (!repeat) menu_nav(menu_slot_page() ? Nav::Delete : Nav::Back); break;
    case SDL_SCANCODE_DELETE: if (!repeat) menu_nav(Nav::Delete); break;
    case SDL_SCANCODE_PAGEUP: menu_nav(Nav::TabPrev); break;
    case SDL_SCANCODE_PAGEDOWN: menu_nav(Nav::TabNext); break;
    default: break;
  }
}

}  // namespace detail

int run(int argc, char **argv, Game &game, Backend &backend) {
  using namespace detail;
  g_game = &game;
  g_backend = &backend;
  g_boot_hold = game.boot.enabled;
  if (!parse_args(argc, argv, game)) {
    show_error(std::string("Invalid command line.\n\n") + game.usage);
    return 2;
  }
  if (g_opt.no_boot_hold) g_boot_hold = false;
  // --write-boot-state: the build's snapshot run. Always the hidden boot, never touches the player's
  // files, dummy video/audio, bounded length.
  const bool write_state = !g_opt.write_boot_state.empty();
  if (write_state) {
    if (!game.boot.enabled) {
      show_error("--write-boot-state needs a game with a hidden boot (Game::boot).");
      return 2;
    }
    g_boot_hold = true;
    if (g_opt.frames < 0) g_opt.frames = game.boot.max_frames + game.boot.state_settle_frames + 200;
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
  }
  const Uint64 t_start = SDL_GetPerformanceCounter();
  const bool test_run = g_opt.frames >= 0;
  g_test_run = test_run;

  std::vector<uint8_t> data;
  if (game.load_data && !game.load_data(data)) return 1;

  SDL_SetMainReady();
  platform_init_hints();
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
    // Controllers may be unavailable on minimal systems; retry without them.
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0)
      return fail(std::string("Could not initialise video/audio:\n") + SDL_GetError());
  }
  g_sdl_inited = true;
  if (!setup_save_dir()) return fail("The user directory override is not writable:\n" + std::string(save_dir()));
  const bool from_memory = !game.memory_content.empty() && backend.wants_memory();
  std::string content;
  if (!from_memory) {
    if (game.stage_content) content = game.stage_content(data, test_run);
    if (content.empty()) return fail("Could not prepare the game data.");
  }
  migrate_quick_slot();
  settings_load();
  desktop_shortcut_first_start();
  controls_init();
  g_offscreen = g_opt.out_w > 0;
  const bool headless = !strcmp(SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "", "dummy");
  vlog("video driver: %s, audio driver: %s\nsaves: %s\nini: %s\n", SDL_GetCurrentVideoDriver(), SDL_GetCurrentAudioDriver(),
       save_dir().c_str(), g_ini.path().c_str());

  // ---- backend (the stored game options go in as core options)
  {
    auto base = game.core_options;
    game.core_options = [base]() {
      std::vector<CoreOption> o = base ? base() : std::vector<CoreOption>();
      for (auto &kv : option_core_overrides()) o.push_back(kv);
      return o;
    };
  }
  if (!backend.init(g_host)) return fail("The emulation backend could not be initialised.");

  // The core prints a couple of diagnostic lines to stdout while loading; hide them.
  if (!g_opt.verbose) {
#ifdef _WIN32
    freopen("NUL", "w", stdout);
#else
    freopen("/dev/null", "w", stdout);
#endif
  }
  if (from_memory ? !backend.load_memory(data.data(), data.size(), game.memory_content) : !backend.load(content))
    return fail("The game data could not be loaded.");
  input_ports_init();
  sram_load();
  // Instant start: an embedded (or --boot-state) snapshot of the game's first picture replaces
  // the hidden boot when it belongs to this engine build and this game data.
  if (g_boot_hold && !write_state && !g_opt.no_boot_state) {
    std::vector<uint8_t> file;
    const char *origin = "embedded";
    bool have = false;
    if (!g_opt.boot_state_file.empty()) {
      origin = "file";
      have = read_file(g_opt.boot_state_file, file);
      if (!have) vlog("boot state: cannot read %s\n", g_opt.boot_state_file.c_str());
    } else if (game.boot_snapshot.embedded) {
      have = game.boot_snapshot.embedded(file);
    }
    if (have) {
      const uint8_t *state = nullptr;
      size_t n = 0;
      std::string why;
      const std::string core = backend.identity();
      if (!core.empty() && bootstate_unpack(file, core, sha1_hex(data.data(), data.size()), backend.serialize_size(), state, n, why)) {
        if (backend.unserialize(state, n)) {
          g_boot_hold = false;
          vlog("boot state loaded (%s, %llu bytes) after %.0f ms\n", origin, static_cast<unsigned long long>(n),
               1000.0 * static_cast<double>(SDL_GetPerformanceCounter() - t_start) / static_cast<double>(SDL_GetPerformanceFrequency()));
          if (game.boot.on_release) game.boot.on_release(BootRelease::StateLoaded, g_boot_ctl);
        } else {
          vlog("boot state rejected by the engine; normal boot\n");
          backend.reset();  // it may have been half-loaded
        }
      } else {
        vlog("boot state not used (%s); normal boot\n", core.empty() ? "engine has no identity" : why.c_str());
      }
    } else {
      vlog("boot state: none; normal boot\n");
    }
  }
  const AvInfo av = backend.av_info();
  const double fps = av.fps > 1.0 ? av.fps : 50.0;
  audio_set_input_rate(av.sample_rate);
  video_init_aspects(av);
  video_update_aspect();
  g_base_h = static_cast<int>(av.height ? av.height : 240);
  vlog("region=%s fps=%.4f audio=%.0f Hz geometry=%ux%u\n", av.region.c_str(), fps, av.sample_rate, av.width, av.height);

  // ---- window / renderer
  // Initial guess for the windowed size (refined below once the HiDPI ratio is known). The
  // window opens on the display the mouse pointer is on.
  const int disp = headless ? 0 : pick_display();
  int scale = 3;
  SDL_Rect usable;
  if (!headless && SDL_GetDisplayUsableBounds(disp, &usable) == 0 && usable.h > 0)
    scale = std::max(1, static_cast<int>(usable.h * 0.9 / g_base_h));
  const int init_h = g_base_h * scale;
  const int init_w = static_cast<int>(std::floor(init_h * g_aspect + 1e-6));
  Uint32 wflags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (g_set.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
  if (!headless) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    wflags |= SDL_WINDOW_OPENGL;
  }
  if (g_offscreen) {  // screenshot gate: a hidden window only provides the GL context
    if (headless) return fail("--out-size needs a real OpenGL context (not the dummy video driver).");
    wflags = (wflags & ~static_cast<Uint32>(SDL_WINDOW_FULLSCREEN_DESKTOP)) | SDL_WINDOW_HIDDEN;
  }
  g_window = SDL_CreateWindow(game.title, SDL_WINDOWPOS_CENTERED_DISPLAY(disp), SDL_WINDOWPOS_CENTERED_DISPLAY(disp), init_w,
                              init_h, wflags);
  if (!g_window) return fail(std::string("Could not create the window:\n") + SDL_GetError());
  if (!headless) {
    g_gl = SDL_GL_CreateContext(g_window);
    if (!g_gl || !load_gl() || !init_gl_pipeline())
      return fail(std::string("OpenGL 3.3 is required but could not be initialised:\n") + SDL_GetError());
    video_apply_vsync(g_set.vsync, g_boot_hold);  // the hidden boot never waits for the display
    vlog("GL: %s / %s vsync=%d fullscreen=%d\n", gl_renderer(), gl_version(), vsync_enabled(), g_set.fullscreen);
    if (!g_set.fullscreen && !g_offscreen) {  // size by drawable pixels now that the HiDPI ratio is known
      int w, h;
      windowed_size(w, h);
      SDL_SetWindowSize(g_window, w, h);
      SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED_DISPLAY(disp), SDL_WINDOWPOS_CENTERED_DISPLAY(disp));
    }
  }
  audio_set_volume(g_set.volume, g_set.mute);
  audio_set_buffer_mode(g_set.audio_buffer);
  if (game.icon.rgba && game.icon.w > 0 && game.icon.h > 0) {
    SDL_Surface *ico = SDL_CreateRGBSurfaceWithFormatFrom(const_cast<uint8_t *>(game.icon.rgba), game.icon.w, game.icon.h, 32,
                                                          game.icon.w * 4, SDL_PIXELFORMAT_RGBA32);
    if (ico) {
      SDL_SetWindowIcon(g_window, ico);
      SDL_FreeSurface(ico);
    }
  }
  SDL_ShowCursor(game.uses_mouse ? SDL_ENABLE : SDL_DISABLE);
  // Test runs have no real focus: the script drives it (focus_lost).
  g_has_focus = test_run || (SDL_GetWindowFlags(g_window) & SDL_WINDOW_INPUT_FOCUS) != 0;

  if (!g_offscreen) audio_open();
  if (g_boot_hold) audio_hold(true);
  else audio_prefill();
  if (g_opt.test_vpads > 0) input_test_vpads(g_opt.test_vpads, g_opt.test_vpad_type);
  input_open_pads();  // (SDL announces the same pads again: the duplicates are skipped)
  if (input_has_pad()) device_used(Device::Pad);
  if (game.uses_mouse) device_used(Device::Mouse);

  // ---- main loop
  const bool unpaced = (headless || g_offscreen) && test_run;  // (--write-boot-state forces dummy drivers)
  const double period = static_cast<double>(SDL_GetPerformanceFrequency()) / fps;
  const double ticks_per_ms = static_cast<double>(SDL_GetPerformanceFrequency()) / 1000.0;
  double acc = 0, cost_ms = 2.0;  // cost_ms: moving average of one run_frame() (frame delay)
  Uint64 last = SDL_GetPerformanceCounter();
  int frames_run = 0;
  // Speed log (--verbose): emulated frames per monotonic second since the boot hold ended.
  Uint64 rate_t0 = 0;
  int rate_f0 = -1;
  int clamps = 0;
  double clamp_ms = 0;
  long paused_turns = 0;
  bool running = true, was_paused = false, help_shown = false, card_checked = false;
  bool prev_hold = g_boot_hold, present_needed = true;
  int settle_runs = 0;
  bool state_written = false;
  Uint32 hold_present = 0, sram_t = SDL_GetTicks();
  if (g_opt.test_toast) toast("SAVED");

  // The hidden boot ends (hook, timeout, loaded state): show the game, wait for the display
  // again, and start the audio with a full cushion.
  auto hold_edge = [&]() {
    if (prev_hold && !g_boot_hold) {
      vlog("boot hold over after %d run_frame calls\n", g_boot_runs);
      video_rebuild();
      video_apply_vsync(g_set.vsync, false);
      audio_prefill();
      present_needed = true;
      last = SDL_GetPerformanceCounter();
      acc = 0;
    }
    prev_hold = g_boot_hold;
  };

  while (running && !g_shutdown_requested) {
    const Uint64 iter_start = SDL_GetPerformanceCounter();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
        case SDL_QUIT: running = false; break;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
          shell_key(e.key.keysym.scancode, e.type == SDL_KEYDOWN, e.key.repeat != 0, e.key.keysym.sym);
          break;
        case SDL_MOUSEMOTION:
          if (g_game->uses_mouse) device_used(Device::Mouse);
          if (menu_open()) menu_mouse_move(e.motion.x, e.motion.y);
          else if (g_rel_mouse) input_mouse_motion(e.motion.xrel, e.motion.yrel);
          break;
        case SDL_MOUSEBUTTONDOWN:
          if (g_game->uses_mouse) device_used(Device::Mouse);
          if (menu_open()) menu_mouse_click(e.button.x, e.button.y, e.button.button);
          else if (g_rel_mouse && !input_mouse_swallowed(true, e.button.button)) input_mouse_button(e.button.button, true);
          break;
        case SDL_MOUSEBUTTONUP:
          if (mouse_click_swallowed(true)) break;
          if (!input_mouse_swallowed(false, e.button.button)) input_mouse_button(e.button.button, false);
          break;
        case SDL_MOUSEWHEEL: if (menu_open()) menu_wheel(e.wheel.y); break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
          input_pad_button(e.cbutton.which, static_cast<SDL_GameControllerButton>(e.cbutton.button),
                           e.type == SDL_CONTROLLERBUTTONDOWN);
          break;
        case SDL_CONTROLLERAXISMOTION:
          input_axis(static_cast<SDL_GameControllerAxis>(e.caxis.axis), e.caxis.value);
          break;
        case SDL_WINDOWEVENT:
          switch (e.window.event) {
            case SDL_WINDOWEVENT_FOCUS_GAINED:
              if (!test_run) { g_has_focus = true; input_focus_gained(); }
              present_needed = true;
              break;
            case SDL_WINDOWEVENT_FOCUS_LOST: if (!test_run) g_has_focus = false; break;
            case SDL_WINDOWEVENT_MINIMIZED: g_minimized = true; break;
            case SDL_WINDOWEVENT_RESTORED:
            case SDL_WINDOWEVENT_MAXIMIZED:
              g_minimized = false;
              present_needed = true;
              break;
            case SDL_WINDOWEVENT_EXPOSED:
            case SDL_WINDOWEVENT_SHOWN:
            case SDL_WINDOWEVENT_SIZE_CHANGED:
            case SDL_WINDOWEVENT_DISPLAY_CHANGED:
              present_needed = true;
              break;
            default: break;
          }
          break;
        case SDL_CONTROLLERDEVICEADDED: input_pad_added(e.cdevice.which); break;
        case SDL_CONTROLLERDEVICEREMOVED: input_pad_removed(e.cdevice.which); break;
        default: break;
      }
    }
    pad_guide_or_hold_check();
    hold_edge();

    // First launch: the controls card, once the game itself is on screen.
    if (!card_checked && !g_boot_hold && g_frame.valid) {
      card_checked = true;
      if (!g_set.first_run_done && (!test_run || g_opt.test_card)) {
        input_release_all();
        menu_show_first_run();
      }
    }
    // The start help box (where the menu and the controls are), at every start once the game
    // itself is on screen and the first-run card has been dismissed. The game waits behind it.
    if (!help_shown && !g_boot_hold && g_frame.valid && card_checked && !menu_open() && (!test_run || g_opt.test_hint)) {
      help_shown = true;
      if (g_set.show_start_help) {
        input_release_all();
        menu_show_start_help();
      }
    }

    // Menu open or focus lost: the game and the audio stand still.
    const bool paused = menu_open() || (g_set.pause_on_focus_loss && !g_has_focus && !g_boot_hold);
    if (paused != was_paused) {
      vlog("paused=%d (menu=%d focus=%d)\n", paused, menu_open(), g_has_focus);
      audio_set_paused(paused);
      if (paused) sram_poll(false);
      was_paused = paused;
    }
    // Relative mouse only while the game has focus, no menu is open and it is not hidden.
    {
      const bool want = mouse_wanted() && !paused;
      if (want != g_rel_mouse) {
        g_rel_mouse = want;
        SDL_SetRelativeMouseMode(want ? SDL_TRUE : SDL_FALSE);
        vlog("relative mouse %d\n", want);
        if (!want) input_mouse_reset();
      }
    }
    int to_run = 0;
    if (paused) {
      last = SDL_GetPerformanceCounter();
      acc = 0;
      script_step(script_tick());
      script_advance();
      if (test_run && ++paused_turns > 400000) running = false;  // a test left the menu open
    } else if (unpaced) {
      to_run = 1;
    } else if (g_boot_hold) {
      // Hidden boot/load phase: run flat out (budget ~10 ms of wall time per loop turn),
      // so the game appears as fast as the core can get there.
      last = SDL_GetPerformanceCounter();
      acc = 0;
      to_run = 1000000;
    } else {
      Uint64 now = SDL_GetPerformanceCounter();
      acc += static_cast<double>(now - last);
      last = now;
      if (acc > period * 5) {  // e.g. window drag stall: do not fast-forward
        const double dropped = (acc - period) / ticks_per_ms;
        ++clamps;
        clamp_ms += dropped;
        vlog("clamp: dropped %.1f ms (accumulator %.1f ms)\n", dropped, acc / ticks_per_ms);
        acc = period;
      }
      // Frame delay: with a display wait ahead, run the frame as late in its period as the
      // measured cost allows, so the input it reads is as fresh as possible. The cadence is
      // unchanged (acc keeps its phase), so game speed is not affected.
      double delay = 0;
      if (g_set.frame_delay && vsync_enabled())
        delay = std::max(0.0, std::min(8.0, period / ticks_per_ms - 2.0 * cost_ms - 1.5)) * ticks_per_ms;
      if (acc >= period + delay || (acc >= period && acc >= period * 1.5)) {
        while (acc >= period && to_run < 3) {
          acc -= period;
          ++to_run;
        }
      }
    }
    if (rate_f0 < 0 && !g_boot_hold) {
      rate_t0 = SDL_GetPerformanceCounter();
      rate_f0 = frames_run;
    }
    const Uint64 run_start = SDL_GetPerformanceCounter();
    // A flat-out batch must stop as soon as the hold ends inside it, or the rest of the
    // huge to_run budget would run unpaced and unrendered (black window for ages).
    const bool flat_out = g_boot_hold;
    g_frame_produced = false;
    for (int i = 0; i < to_run && running && !menu_open(); ++i) {
      if (flat_out && i > 0 && (!g_boot_hold || (SDL_GetPerformanceCounter() - run_start) > ticks_per_ms * 10)) break;
      script_step(script_tick());
      if (menu_open() || g_shutdown_requested) break;  // a scripted action paused the game
      input_actions_step();
      input_apply_pads();
      input_apply_mouse();
      if (g_boot_hold) ++g_boot_runs;
      const Uint64 t0 = SDL_GetPerformanceCounter();
      backend.run_frame();
      if (!flat_out) cost_ms = cost_ms * 0.9 + 0.1 * static_cast<double>(SDL_GetPerformanceCounter() - t0) / ticks_per_ms;
      hold_timeout_check();
      if (write_state && !g_boot_hold) {
        if (!g_hold_hook_released) {
          fprintf(stderr, "The hidden boot did not reach the game's first picture (timeout).\n");
          running = false;
        } else if (++settle_runs > game.boot.state_settle_frames) {
          const size_t n = backend.serialize_size();
          std::vector<uint8_t> buf(n);
          if (n && backend.serialize(buf.data(), n)) {
            const auto packed = bootstate_pack(backend.identity(), sha1_hex(data.data(), data.size()), buf.data(), n);
            state_written = write_file_atomic(g_opt.write_boot_state, packed.data(), packed.size());
            vlog("boot state written to %s: %llu bytes (state %llu) at run frame %d\n", g_opt.write_boot_state.c_str(),
                 static_cast<unsigned long long>(packed.size()), static_cast<unsigned long long>(n), g_boot_runs + settle_runs);
          }
          if (!state_written) fprintf(stderr, "Could not write the boot state to %s\n", g_opt.write_boot_state.c_str());
          running = false;
        }
      }
      script_advance();
      ++frames_run;
      if (g_opt.frames >= 0 && frames_run >= g_opt.frames) running = false;
      if (flat_out && !g_boot_hold) break;
    }
    hold_edge();
    if (!paused && Uint32(SDL_GetTicks() - sram_t) > 2000) {
      sram_t = SDL_GetTicks();
      sram_poll(false);
    }

    // ---- present. Only when something new exists: a produced picture, a menu, a changed
    // window. With VRR/vsync off this presents at the core's rate, never at the loop's.
    bool presented = false;
    if (unpaced) {
      if (to_run > 0 || paused || toast_on_screen() || toast_pending()) upload_frame();
    } else if (g_boot_hold) {
      // Hidden boot: a black frame now and then keeps the window alive; no display wait.
      if (SDL_GetTicks() - hold_present >= 200 && g_gl && !g_minimized) {
        hold_present = SDL_GetTicks();
        upload_frame();
        render();
      }
    } else if (g_frame_produced || present_needed || menu_open() ||
               (paused && (toast_pending() || toast_on_screen()))) {
      upload_frame();
      if (g_gl && !g_minimized) render();
      present_needed = g_minimized;  // while minimised the next restore must present
      presented = true;
    }
    if (!unpaced) {
      const double ms = static_cast<double>(SDL_GetPerformanceCounter() - iter_start) / ticks_per_ms;
      if (g_minimized) {
        SDL_Delay(8);
      } else if (g_boot_hold) {
        // flat out
      } else if (paused) {
        if (ms < 8.0) SDL_Delay(static_cast<Uint32>(8.0 - ms));  // menu: ~120 turns/s at most
      } else if (to_run == 0 && !presented) {
        // Nothing due: sleep towards the next frame instead of spinning.
        const double to_due = (period - acc) / ticks_per_ms;
        SDL_Delay(to_due > 3.0 ? 2 : 1);
      }
    }
  }

  if (rate_f0 >= 0) {
    const double secs = static_cast<double>(SDL_GetPerformanceCounter() - rate_t0) / static_cast<double>(SDL_GetPerformanceFrequency());
    vlog("rate: %d frames in %.3f s = %.3f fps (nominal %.3f)\n", frames_run - rate_f0, secs,
         secs > 0 ? (frames_run - rate_f0) / secs : 0.0, fps);
    vlog("clamps: %d dropped %.1f ms\n", clamps, clamp_ms);
  }
  sram_poll(true);
  if (!g_opt.screenshot.empty() && !save_screenshot(g_opt.screenshot)) return fail("Screenshot failed.");

  shutdown_all();
  if (write_state && !state_written) return 1;
  return 0;
}

}  // namespace retro_shell
