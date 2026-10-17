// Shell-internal state shared between the modules. Not installed.
#pragma once

#include <SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include "retro_shell/shell.h"

namespace retro_shell {
namespace detail {

struct Options {
  bool test_toast = false;     // hidden: show a SAVED toast at frame 0
  bool test_hint = false;      // hidden: show the start help box in test runs too
  int frames = -1;             // >=0: run N frames then exit (a "test run")
  std::string screenshot;      // BMP path written after the last frame
  std::string ini;             // --ini: settings file for test runs (its directory also holds the slots)
  bool test_card = false;      // hidden: show the first-run controls card in test runs too
  int test_vpads = 0;          // hidden: attach N virtual gamepads after SDL init (--test-vpads N[:ps|nin])
  int test_vpad_type = 0;      // 0 Xbox, 1 PlayStation, 2 Nintendo (look of the virtual pads)
  bool verbose = false;
  bool no_boot_hold = false;
  std::string write_boot_state;  // --write-boot-state FILE: boot hidden, store the snapshot, exit
  std::string boot_state_file;   // --boot-state FILE: use this snapshot instead of the embedded one
  bool no_boot_state = false;    // --no-boot-state: ignore any snapshot, boot normally
  int out_w = 0, out_h = 0;    // --out-size WxH: render into an offscreen target (screenshot gate)
  // Per-run overrides of the ini (-1 = not given).
  int fullscreen = -1;         // --fullscreen / --window
  int integer_scale = -1;      // --integer-scale
  int filter = -1;             // --shader auto|sharp|crt|crtmask
};
extern Options g_opt;

// ---- settings.cpp: effective settings (ini + per-run overrides), applied live
enum Filter { FILTER_AUTO = 0, FILTER_SHARP, FILTER_CRT, FILTER_CRTMASK, FILTER_COUNT };
enum VsyncMode { VSYNC_AUTO = 0, VSYNC_ON, VSYNC_OFF, VSYNC_COUNT };
enum AudioBuf { ABUF_AUTO = 0, ABUF_SMALL, ABUF_MEDIUM, ABUF_LARGE, ABUF_COUNT };
struct Settings {
  bool fullscreen = true;
  bool integer = false;  // scaling: false = fit, true = integer
  int filter = FILTER_AUTO;
  int crt_strength = 100;  // percent of the shared CRT preset (scanlines / mask depth)
  int aspect = 0;          // index into Game::aspects
  bool game_border = true; // false: crop to Game::border_crop
  int vsync = VSYNC_AUTO;
  bool frame_delay = false;
  int volume = 100;
  bool mute = false;
  int audio_buffer = ABUF_AUTO;
  bool pause_on_focus_loss = true;
  bool mouse_on = true;
  int mouse_sens = 100;  // percent
  bool first_run_done = false;
  bool show_start_help = true;  // the help box at every start (general.show_start_help)
};
extern Settings g_set;

class Ini {
 public:
  void open(const std::string &path);  // "" = in-memory only
  bool get(const std::string &section, const std::string &key, std::string &out) const;
  bool set(const std::string &section, const std::string &key, const std::string &value);  // writes atomically
  bool erase(const std::string &section, const std::string &key);  // removes the entry if present
  const std::string &path() const { return path_; }

 private:
  size_t find(const std::string &section, const std::string &key, size_t &section_end) const;
  bool write() const;
  std::string path_;
  std::vector<std::string> lines_;
};
extern Ini g_ini;

void settings_load();  // reads the ini (or defaults), then applies g_opt overrides
// Setters apply live and store to the ini at once.
void set_fullscreen(bool on, bool toast_it = false);
void set_integer(bool on);
void set_filter(int f);
void set_vsync(int mode);
void set_crt_strength(int pct);
void set_aspect(int idx);
void set_game_border(bool on);
void set_frame_delay(bool on);
void set_audio_buffer(int mode);
void set_mouse_on(bool on);
void set_show_start_help(bool on);
void set_mouse_sens(int pct);
void set_volume(int v);
void set_mute(bool on);
void set_pause_on_focus_loss(bool on);
std::string option_value(size_t idx);                  // current value of Game::options[idx]
void set_option(size_t idx, const std::string &value);
void reset_all_settings();
std::vector<CoreOption> option_core_overrides();       // core_key/value pairs from the stored options

extern const Game *g_game;
// Environment variable <INI_NAME upper>_<suffix> (e.g. BARBARIAN_USER_DIR): test/release
// smoke switches that redirect or disable the shell's touching of the player's files.
std::string env_name(const char *suffix);
// First start of an installed copy: desktop shortcut fallback (shortcut.cpp).
void desktop_shortcut_first_start();
// Boot snapshot container (bootstate.cpp).
std::vector<uint8_t> bootstate_pack(const std::string &core, const std::string &content_id, const void *state, size_t n);
// Checks `file` against the running engine; on success points `state`/`n` into it. Else `why` says what differs.
bool bootstate_unpack(const std::vector<uint8_t> &file, const std::string &core, const std::string &content_id,
                      size_t expected_size, const uint8_t *&state, size_t &n, std::string &why);
extern bool g_test_run;  // --frames given: no user directory, test clock
extern bool g_has_focus;
extern Backend *g_backend;

// ---- cli.cpp
bool parse_args(int argc, char **argv, const Game &game);

// ---- video.cpp
struct Frame {
  std::vector<uint8_t> data;  // last frame, tightly packed in the texture format
  int w = 0, h = 0, bpp = 2;
  PixelFormat fmt = PixelFormat::RGB565;
  bool valid = false;
};
extern Frame g_frame;
extern SDL_Window *g_window;
extern SDL_GLContext g_gl;  // null in headless (dummy driver) mode: no rendering
extern double g_aspect;     // display aspect used for the destination rectangle
void video_submit(const FrameDesc &f);
void video_rebuild();                 // re-derive the shown frame (border crop) from the last core frame
void video_init_aspects(const AvInfo &av);  // after load: builds the aspect list
void video_update_aspect();           // applies Settings::aspect / game_border to g_aspect
size_t aspect_count();
std::string aspect_label(size_t i);
std::string aspect_id(size_t i);
bool video_mouse_scale(double &sx, double &sy);  // logical pixels per window unit
bool video_has_crop();
bool load_gl();
bool init_gl_pipeline();
void upload_frame();
void render(bool swap = true);
bool save_screenshot(const std::string &path);
bool toast_on_screen();   // last uploaded frame carried a toast
bool vsync_enabled();
void video_apply_vsync(int mode, bool force_off = false);  // VsyncMode; force_off: interval 0 (hidden boot)
// Filter actually applied now (Auto resolved from the current picture scale): "SHARP",
// "CRT" or "CRT+MASK".
const char *filter_active_name();
extern bool g_offscreen;  // --out-size: the final image goes to an offscreen target of g_opt.out_w x out_h
bool window_to_frame(int wx, int wy, int &fx, int &fy);  // false when outside the picture
extern int g_base_h;  // picture height used for the windowed size
const char *gl_renderer();
const char *gl_version();

// ---- draw.cpp: primitives in the picture's pixel grid
void plot(Frame &f, int x, int y, uint32_t rgb, float a);
void fill_rect(Frame &f, int x, int y, int w, int h, uint32_t rgb, float a);
int text_width(const std::string &s, int scale);
void draw_text(Frame &f, int x, int y, const std::string &s, uint32_t rgb, float a, int scale, bool shadow);

// ---- toast.cpp
void toast_draw(Frame &f, float alpha);
float toast_alpha();         // 0 when inactive
bool toast_pending();        // text still set
bool toast_visible();        // text set and not yet expired (advances the expiry check)

// ---- menu.cpp
enum class Nav { Up, Down, Left, Right, Select, Back, TabPrev, TabNext, Delete };
bool menu_open();
void menu_show(bool settings);  // false: main page, true: straight to Settings
void menu_show_first_run();     // the controls card; any key/button/click dismisses it
bool menu_card_active();        // first-run card showing
void menu_show_start_help();    // the help box (OK / DON'T SHOW AGAIN); the game stays paused behind it
bool menu_help_active();
void menu_card_dismiss();       // closes it and sets general.first_run_done
bool menu_slot_page();          // Save or Load page showing (Backspace deletes there)
bool menu_capturing();          // "press a key / button" prompt of the rebinding screen is up
// Rebinding prompt input (return true when consumed). Reserved keys are refused with a toast.
bool menu_capture_key(SDL_Scancode sc);
bool menu_capture_pad(int button);
void menu_close();              // resume the game
void menu_toggle_settings();    // F1: open on Settings, jump to it, or close when already there
void menu_nav(Nav n);
void menu_mouse_move(int wx, int wy);
void menu_mouse_click(int wx, int wy, int button);  // SDL_BUTTON_LEFT / RIGHT
void menu_wheel(int dy);
void menu_draw(Frame &f);
bool mouse_click_swallowed(bool up);  // true for the button-up that ends the click that closed the menu

// ---- controls.cpp: rebindable functions (keyboard + gamepad), active input device
enum class Device { Keyboard, Pad, Mouse };
extern Device g_device;          // last used input device
void device_used(Device d);
void controls_init();            // defaults + [keys]/[pad] from the ini; call after settings_load
size_t ctl_count();
std::vector<SDL_Scancode> ctl_keys(size_t i);  // keys currently driving function i
std::vector<int> ctl_pads(size_t i);           // gamepad buttons currently driving function i
bool ctl_rebindable(size_t i, Device d);
std::string ctl_value(size_t i, Device d);     // card text for the function under device d
std::string key_text(SDL_Scancode sc);         // human-readable key name (arrows are font glyphs)
// Arrow glyphs of the 5x7 font (single chars inside UI strings); glyph_utf8 turns them into
// real arrows for the log.
constexpr char kArrowUp = '\x18', kArrowDown = '\x19', kArrowRight = '\x1A', kArrowLeft = '\x1B';
std::string glyph_utf8(const std::string &s);
std::string pad_button_text(int button);       // Xbox-positional text label ("A", "LB", "START", ...)
bool key_reserved(SDL_Scancode sc);
enum class BindResult { Ok, Swapped, Reserved, Fixed };  // Fixed: key of the game's fixed key map
enum class PadStyle { Xbox, PlayStation, Nintendo };
extern PadStyle g_pad_style;       // look of the active gamepad (labels only; mapping is positional)
std::string pad_hint_keys();       // "A SELECT  B BACK" for the active pad style
BindResult ctl_bind_key(size_t i, SDL_Scancode sc, std::string &other);  // other = label swapped with
BindResult ctl_bind_pad(size_t i, int button, std::string &other);
void ctl_reset();                              // all functions back to defaults (ini entries removed)
bool ctl_key_owned(SDL_Scancode sc);           // bound to any function (primary or active alternate)

// ---- audio.cpp
bool audio_open();
void audio_close();
bool audio_available();
size_t audio_push(const int16_t *data, size_t frames);
void audio_prefill();          // drops stale samples and fills the ring with silence up to the target
void audio_hold(bool held);    // hidden boot: no underrun accounting while true
void audio_set_buffer_mode(int mode);  // AudioBuf
void audio_set_input_rate(double hz);
void audio_set_volume(int percent, bool mute);
void audio_set_paused(bool paused);

// ---- input.cpp
void input_open_pads();
bool input_has_pad();
size_t input_pad_count();
void input_close_pads();
void input_pad_added(int idx);
void input_pad_removed(SDL_JoystickID id);
void input_pad_button(SDL_JoystickID which, SDL_GameControllerButton b, bool down);
void input_key_event(SDL_Scancode sc, bool down, SDL_Keycode keycode = SDLK_UNKNOWN);  // function / key map passthrough
void input_focus_gained();
void input_mouse_motion(int xrel, int yrel);       // window units
void input_mouse_button(int sdl_button, bool down);
bool input_mouse_swallowed(bool down, int sdl_button);  // focus click: true = do not use it
bool mouse_wanted();                               // relative mouse mode should be on
void input_ports_init();                           // set_port_device for every port (hot ports start as Joypad)
void input_actions_step();                         // delayed game actions due before this frame
void input_apply_mouse();
void input_mouse_reset();                          // relative mode ended: drop buttons and pending motion
void input_test_vpads(int n, int type);            // --test-vpads
void input_key_released(SDL_Scancode sc);          // a release is always recorded, menu open or not
void send_key(unsigned code, bool down);           // user key: presses dropped during the boot hold
bool script_parse(const std::string &spec);
void script_step(int tick);
int script_tick();                                 // test clock: emulated frames + paused loop turns
void script_advance();
Uint32 now_ms();                                   // SDL ticks; the test clock in test runs
void pad_guide_or_hold_check();                    // opens the menu on Start+Back held >= 1 s
void input_release_all();                          // releases every game key (menu opening)
void input_axis(SDL_GameControllerAxis a, int value);
void input_apply_pads();                           // computes and sets pad states on the backend

// ---- state shared between app.cpp and the hooks above
extern bool g_boot_hold;
void save_state();   // quick slot
void load_state();
bool save_slot(int slot);  // 0 = quick slot, 1..5; toasts on failure
bool load_slot(int slot);
bool slot_time(int slot, long long &t);  // modification time, false = empty
// Preview sidecar of a slot (<slot>.thm): a downscaled RGB copy of the picture at save time.
struct Thumb {
  int w = 0, h = 0;
  uint32_t seq = 0;            // save counter: the largest one is the most recent slot
  std::vector<uint8_t> rgb;    // w*h*3
};
bool thumb_load(int slot, Thumb &t);
int latest_slot();             // most recently saved slot (0 = quick), -1 when none exists
bool delete_slot(int slot);    // removes state and preview; false when the slot was empty
void request_quit();
void shell_key(SDL_Scancode sc, bool down, bool repeat, SDL_Keycode keycode = SDLK_UNKNOWN);  // hotkeys + menu + game routing
void sram_load();            // after the backend loaded: restores <ini_name>.sram
void sram_poll(bool force);  // writes the game's save RAM when it changed
void open_menu(bool settings);

// ---- platform.cpp
void platform_init_hints();
void toggle_fullscreen();
void apply_fullscreen(bool on);
int pick_display();                 // display of the window, else the one under the mouse
void windowed_size(int &w, int &h);  // largest integer multiple <= 90% of the usable height

// ---- userdir.cpp
bool setup_save_dir();  // false: the user-dir override is unwritable
std::string slot_dir();
std::string slot_path(int slot);
std::string thumb_path(int slot);
void migrate_quick_slot();

}  // namespace detail
}  // namespace retro_shell
