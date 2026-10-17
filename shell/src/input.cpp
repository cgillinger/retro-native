// Input: keyboard / gamepad / mouse -> abstract pad state and backend key passthrough, plus the
// scripted input used by tests (--press).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "internal.h"

namespace retro_shell {
namespace detail {
namespace {

constexpr int kMaxPads = 4;
struct Pad {
  SDL_GameController *c = nullptr;
  SDL_JoystickID id = -1;
};
Pad g_pads[kMaxPads];  // compact: slot 0 is player 1, removal shifts the others down
int g_npads = 0;

// Keyboard state as seen by the shell: updated from the key events it routes (so scripted and
// real keys behave alike, and a key released while the menu was open is not left pressed).
bool g_kdown[SDL_NUM_SCANCODES] = {};
unsigned g_text_sent[SDL_NUM_SCANCODES] = {};  // text key code sent on press (for the release)
uint16_t g_last_mask[kMaxPorts] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
// Buttons that were already held when the menu/card/boot hold ended stay ignored until released.
uint16_t g_latch[kMaxPorts] = {};

// Scripted input for tests (--press "600:space,900:<function id>:30,1200:down:5"). The script clock
// ("tick") counts emulated frames plus loop turns spent paused (menu open), so menus can be
// driven. Joystick actions (the ids of Game::functions with a `joy`) are held for `len` ticks
// (default 5); key actions are pressed then released. Shell actions: save, load (quick slot),
// esc, f1, f5, f9, f11, guide, focus_lost, quit, shot (screenshot "<stem>-<tick>.bmp"),
// sc_<scancode>, pad_<button>, click_X_Y, rclick_X_Y, hover_X_Y, mouse_DX_DY, mbtn_left|right|middle,
// vpad_add[_ps|_nin], vpad_remove, vbtn_<n>_<button>, vaxis_<n>_<axis>_<value>.
struct Scripted {
  int start, len;
  std::string what;
  bool active = false;
};
std::vector<Scripted> g_script;

bool scripted_active(const std::string &what) {
  for (const auto &sc : g_script)
    if (sc.active && sc.what == what) return true;
  return false;
}

int g_tick = 0;
int g_axis_state[2] = {0, 0};
bool g_pmenu[SDL_CONTROLLER_BUTTON_MAX] = {};  // buttons pressed for a menu: the release is ignored

// ---- Start / Back: shell-owned combination. A press is held back for a moment so that
// Start+Back (menu hold) never reaches the game; a short press is delivered on release.
struct StartBack {
  bool down = false, delivered = false, swallow = false, tap = false;
  Uint32 ts = 0, release_at = 0;
  int slot = 0;
};
StartBack g_sb[2];  // 0 = Start, 1 = Back
bool g_hold_fired = false;
constexpr Uint32 kDeferMs = 60, kTapMs = 60;

SDL_GameControllerButton sb_button(int i) { return i == 0 ? SDL_CONTROLLER_BUTTON_START : SDL_CONTROLLER_BUTTON_BACK; }

PadStyle style_of(SDL_GameController *c) {
  switch (SDL_GameControllerGetType(c)) {
    case SDL_CONTROLLER_TYPE_PS3:
    case SDL_CONTROLLER_TYPE_PS4:
    case SDL_CONTROLLER_TYPE_PS5: return PadStyle::PlayStation;
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO: return PadStyle::Nintendo;
    default: return PadStyle::Xbox;
  }
}

int slot_of(SDL_JoystickID id) {
  for (int i = 0; i < g_npads; ++i)
    if (g_pads[i].id == id) return i;
  return -1;
}

bool btn(int slot, SDL_GameControllerButton b) {
  if (slot < 0 || slot >= g_npads) return false;
  if (b == SDL_CONTROLLER_BUTTON_START || b == SDL_CONTROLLER_BUTTON_BACK) {
    const StartBack &s = g_sb[b == SDL_CONTROLLER_BUTTON_START ? 0 : 1];
    return s.delivered && s.slot == slot;
  }
  return SDL_GameControllerGetButton(g_pads[slot].c, b) != 0;
}

PortDevice port_device(unsigned port) {
  return port < g_game->port_devices.size() ? g_game->port_devices[port] : PortDevice::None;
}

// Hot-switched shared ports (PortDevice::JoypadOrMouse): the device the backend currently has.
PortDevice g_hot_cur[kMaxPorts];
Uint32 g_hot_ms[kMaxPorts];
bool g_hot_init[kMaxPorts];
constexpr Uint32 kHotHoldoffMs = 250;

bool is_hot(unsigned port) { return port_device(port) == PortDevice::JoypadOrMouse; }
// Port currently acts as a joypad (a plain Joypad port, or a hot port set to Joypad).
bool port_is_joypad(unsigned port) {
  const PortDevice d = port_device(port);
  return d == PortDevice::Joypad || (d == PortDevice::JoypadOrMouse && g_hot_cur[port] == PortDevice::Joypad);
}
bool port_is_mouse(unsigned port) {
  const PortDevice d = port_device(port);
  return d == PortDevice::Mouse || (d == PortDevice::JoypadOrMouse && g_hot_cur[port] == PortDevice::Mouse);
}
void hot_switch(unsigned port, PortDevice to) {
  if (g_hot_cur[port] == to) return;
  const Uint32 now = now_ms();
  if (g_hot_init[port] && now - g_hot_ms[port] < kHotHoldoffMs) return;
  g_hot_init[port] = true;
  g_hot_ms[port] = now;
  g_hot_cur[port] = to;
  vlog("port %u -> %s\n", port, to == PortDevice::Mouse ? "mouse" : "joypad");
  g_backend->set_port_device(port, to);
  g_backend->set_pad(port, PadState());
  g_backend->set_mouse(port, MouseState());
}

// ---- game hooks: injected keys and delayed actions (ControlFunc::on_press)
struct Delayed { int frames; std::function<void()> fn; };
std::vector<Delayed> g_delayed;

class Actions final : public GameActions {
 public:
  void key(unsigned code, bool down) override {
    vlog("inject key %u %s\n", code, down ? "down" : "up");
    g_backend->key_event(code, down);
  }
  void after(int frames, std::function<void()> fn) override { g_delayed.push_back({frames, std::move(fn)}); }
  bool read_memory(size_t addr, void *dst, size_t n) override { return g_backend->read_memory(addr, dst, n); }
  FrameView frame() const override {
    FrameView v;
    v.data = g_frame.data.data();
    v.width = g_frame.w;
    v.height = g_frame.h;
    v.bytes_per_pixel = g_frame.bpp;
    v.format = g_frame.fmt;
    return v;
  }
};
Actions g_actions;

// A function's press/release: the game's hook first, else its plain key code.
void send_func(size_t i, bool down) {
  const ControlFunc &f = g_game->functions[i];
  if (down && g_boot_hold) return;
  if (f.on_press && f.on_press(down, g_actions)) return;
  send_key(f.code, down);
}

int mouse_port() {
  for (unsigned p = 0; p < g_game->port_devices.size() && p < kMaxPorts; ++p)
    if (g_game->port_devices[p] == PortDevice::Mouse || g_game->port_devices[p] == PortDevice::JoypadOrMouse)
      return static_cast<int>(p);
  return -1;
}

// Port 0: keyboard + pad #1 (+ scripted input) through the game's functions. Ports 1..: pad
// #2.. with the full positional mapping.
PadState raw_pad(unsigned port) {
  PadState st;
  if (port_device(port) != PortDevice::Joypad && !is_hot(port)) return st;
  uint16_t m = 0;
  const int slot = static_cast<int>(port);
  SDL_GameController *c = slot < g_npads ? g_pads[slot].c : nullptr;
  if (port == 0) {
    for (size_t i = 0; i < ctl_count(); ++i) {
      const ControlFunc &f = g_game->functions[i];
      if (f.joy < 0) continue;
      bool on = scripted_active(f.id);
      for (SDL_Scancode k : ctl_keys(i)) on |= g_kdown[k];
      for (int b : ctl_pads(i)) on |= btn(slot, static_cast<SDL_GameControllerButton>(b));
      if (on) m |= static_cast<uint16_t>(1u << f.joy);
    }
  } else if (c) {
    static const struct { SDL_GameControllerButton b; PadButton p; } map[] = {
        {SDL_CONTROLLER_BUTTON_A, PAD_B},           {SDL_CONTROLLER_BUTTON_B, PAD_A},
        {SDL_CONTROLLER_BUTTON_X, PAD_Y},           {SDL_CONTROLLER_BUTTON_Y, PAD_X},
        {SDL_CONTROLLER_BUTTON_BACK, PAD_SELECT},   {SDL_CONTROLLER_BUTTON_START, PAD_START},
        {SDL_CONTROLLER_BUTTON_LEFTSTICK, PAD_L3},  {SDL_CONTROLLER_BUTTON_RIGHTSTICK, PAD_R3},
        {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, PAD_L}, {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, PAD_R}};
    for (const auto &e : map)
      if (btn(slot, e.b)) m |= static_cast<uint16_t>(1u << e.p);
    for (const auto &e : g_game->extra_pad_map)
      if (btn(slot, e.first)) m |= static_cast<uint16_t>(1u << e.second);
    if (SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000) m |= 1u << PAD_L2;
    if (SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) m |= 1u << PAD_R2;
  }
  if (c) {
    const int dead = 12000;
    const int lx = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
    const int ly = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
    if (btn(slot, SDL_CONTROLLER_BUTTON_DPAD_UP) || ly < -dead) m |= 1u << PAD_UP;
    if (btn(slot, SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ly > dead) m |= 1u << PAD_DOWN;
    if (btn(slot, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || lx < -dead) m |= 1u << PAD_LEFT;
    if (btn(slot, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || lx > dead) m |= 1u << PAD_RIGHT;
    st.lx = static_cast<int16_t>(lx);
    st.ly = static_cast<int16_t>(ly);
    st.rx = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTX);
    st.ry = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTY);
  }
  st.buttons = m;
  return st;
}

// ---- mouse
double g_macc_x = 0, g_macc_y = 0;
int g_mbtn = 0;
bool g_focus_armed = false;
Uint32 g_focus_ms = 0;
int g_swallowed_btn = 0;

// ---- test hooks: virtual gamepads through SDL's own virtual joystick (the real pad path)
struct VPad { int dev_index; SDL_JoystickID id; };
std::vector<VPad> g_vpads;

void vpad_add(int type) {
  SDL_VirtualJoystickDesc d;
  SDL_zero(d);
  d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
  d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
  d.naxes = 6;
  d.nbuttons = 15;
  static const struct { Uint16 vid, pid; const char *name; } ids[] = {
      {0x045e, 0x028e, "Virtual Xbox pad"}, {0x054c, 0x09cc, "Virtual PlayStation pad"}, {0x057e, 0x2009, "Virtual Switch pad"}};
  d.vendor_id = ids[type].vid;
  d.product_id = ids[type].pid;
  d.name = ids[type].name;
  const int idx = SDL_JoystickAttachVirtualEx(&d);
  if (idx < 0) { vlog("virtual pad: %s\n", SDL_GetError()); return; }
  g_vpads.push_back({idx, SDL_JoystickGetDeviceInstanceID(idx)});
  vlog("virtual pad %d attached (type %d)\n", static_cast<int>(g_vpads.size()) - 1, type);
}

SDL_Joystick *vpad_joystick(size_t n) {
  if (n >= g_vpads.size()) return nullptr;
  const int slot = slot_of(g_vpads[n].id);
  return slot < 0 ? nullptr : SDL_GameControllerGetJoystick(g_pads[slot].c);
}

bool vpad_action(const std::string &w, bool down) {
  if (w.compare(0, 8, "vpad_add") == 0) {
    if (down) vpad_add(w == "vpad_add_ps" ? 1 : w == "vpad_add_nin" ? 2 : 0);
    return true;
  }
  if (w.compare(0, 11, "vpad_remove") == 0) {  // vpad_remove (the last one) or vpad_remove_<n>
    if (down && !g_vpads.empty()) {
      const size_t n = w.size() > 12 ? static_cast<size_t>(atoi(w.c_str() + 12)) : g_vpads.size() - 1;
      if (n >= g_vpads.size()) return false;
      for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_JoystickGetDeviceInstanceID(i) == g_vpads[n].id) SDL_JoystickDetachVirtual(i);
      g_vpads.erase(g_vpads.begin() + static_cast<long>(n));
    }
    return true;
  }
  int n;
  char name[40];
  if (sscanf(w.c_str(), "vbtn_%d_%39s", &n, name) == 2) {
    const SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(name);
    SDL_Joystick *j = vpad_joystick(static_cast<size_t>(n));
    if (b == SDL_CONTROLLER_BUTTON_INVALID || !j) return false;
    SDL_JoystickSetVirtualButton(j, b, down ? 1 : 0);
    return true;
  }
  int axis = 0, val = 0;
  if (sscanf(w.c_str(), "vaxis_%d_%d_%d", &n, &axis, &val) == 3) {
    SDL_Joystick *j = vpad_joystick(static_cast<size_t>(n));
    if (!j) return false;
    SDL_JoystickSetVirtualAxis(j, axis, down ? val : 0);
    return true;
  }
  return false;
}

bool shell_action(const std::string &w, bool down) {
  struct { const char *name; SDL_Scancode sc; } keys[] = {
      {"esc", SDL_SCANCODE_ESCAPE}, {"f1", SDL_SCANCODE_F1}, {"f5", SDL_SCANCODE_F5},
      {"f9", SDL_SCANCODE_F9},      {"f11", SDL_SCANCODE_F11}};
  for (const auto &k : keys)
    if (w == k.name) { shell_key(k.sc, down, false); return true; }
  if (w == "altenter") {  // Alt+Enter as typed on a keyboard
    if (down) {
      shell_key(SDL_SCANCODE_LALT, true, false);
      shell_key(SDL_SCANCODE_RETURN, true, false);
      shell_key(SDL_SCANCODE_RETURN, false, false);
      shell_key(SDL_SCANCODE_LALT, false, false);
    }
    return true;
  }
  if (w == "altgr") {  // AltGr as Windows reports it: a fake Left Ctrl, then Right Alt
    if (down) {
      shell_key(SDL_SCANCODE_LCTRL, true, false);
      shell_key(SDL_SCANCODE_RALT, true, false);
    } else {
      shell_key(SDL_SCANCODE_LCTRL, false, false);
      shell_key(SDL_SCANCODE_RALT, false, false);
    }
    return true;
  }
  int mx, my;
  if (sscanf(w.c_str(), "click_%d_%d", &mx, &my) == 2 || sscanf(w.c_str(), "hover_%d_%d", &mx, &my) == 2) {
    // Window coordinates; queued like real mouse input.
    SDL_Event ev{};
    if (w[0] == 'h') {
      ev.type = SDL_MOUSEMOTION;
      ev.motion.x = mx;
      ev.motion.y = my;
    } else {
      ev.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
      ev.button.button = SDL_BUTTON_LEFT;
      ev.button.x = mx;
      ev.button.y = my;
    }
    SDL_PushEvent(&ev);
    return true;
  }
  if (sscanf(w.c_str(), "rclick_%d_%d", &mx, &my) == 2) {  // right click at window coordinates
    SDL_Event ev{};
    ev.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    ev.button.button = SDL_BUTTON_RIGHT;
    ev.button.x = mx;
    ev.button.y = my;
    SDL_PushEvent(&ev);
    return true;
  }
  if (sscanf(w.c_str(), "mouse_%d_%d", &mx, &my) == 2) {  // relative motion
    if (down) {
      SDL_Event ev{};
      ev.type = SDL_MOUSEMOTION;
      ev.motion.xrel = mx;
      ev.motion.yrel = my;
      SDL_PushEvent(&ev);
    }
    return true;
  }
  if (w.compare(0, 5, "mbtn_") == 0) {
    SDL_Event ev{};
    ev.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    ev.button.button = w == "mbtn_right" ? SDL_BUTTON_RIGHT : w == "mbtn_middle" ? SDL_BUTTON_MIDDLE : SDL_BUTTON_LEFT;
    SDL_PushEvent(&ev);
    return true;
  }
  if (w == "guide") {
    if (down) input_pad_button(g_npads ? g_pads[0].id : -1, SDL_CONTROLLER_BUTTON_GUIDE, true);
    return true;
  }
  if (w.compare(0, 5, "vpad_") == 0 || w.compare(0, 5, "vbtn_") == 0 || w.compare(0, 6, "vaxis_") == 0) return vpad_action(w, down);
  // sdl_<name> / sdlrep_<name>: a real SDL key event (down, resp. auto-repeat down; the up is
  // the end of the action) so the event loop's own handling is exercised.
  if (w.compare(0, 4, "sdl_") == 0 || w.compare(0, 7, "sdlrep_") == 0) {
    const bool rep = w[3] == 'r';
    const SDL_Scancode sc = SDL_GetScancodeFromName(w.c_str() + (rep ? 7 : 4));
    if (sc == SDL_SCANCODE_UNKNOWN) return false;
    SDL_Event ev{};
    ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    ev.key.keysym.scancode = sc;
    ev.key.keysym.sym = SDL_GetKeyFromScancode(sc);
    ev.key.repeat = rep ? 1 : 0;
    if (rep && !down) return true;
    SDL_PushEvent(&ev);
    return true;
  }
  // sc_<SDL scancode name>: a key event as the keyboard would send it (e.g. sc_K, sc_Left).
  if (w.compare(0, 3, "sc_") == 0) {
    const SDL_Scancode sc = SDL_GetScancodeFromName(w.c_str() + 3);
    if (sc == SDL_SCANCODE_UNKNOWN) return false;
    shell_key(sc, down, false);
    return true;
  }
  // pad_<SDL button name>: a button of the first gamepad (a, b, x, y, start, back, leftshoulder, dpdown, ...).
  if (w.compare(0, 4, "pad_") == 0) {
    const SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(w.c_str() + 4);
    if (b == SDL_CONTROLLER_BUTTON_INVALID) return false;
    input_pad_button(g_npads ? g_pads[0].id : -1, b, down);
    return true;
  }
  if (w == "minimized") {  // as if the window were minimised (down) / restored (up)
    SDL_Event ev{};
    ev.type = SDL_WINDOWEVENT;
    ev.window.event = down ? SDL_WINDOWEVENT_MINIMIZED : SDL_WINDOWEVENT_RESTORED;
    SDL_PushEvent(&ev);
    return true;
  }
  if (w == "focus_lost") {
    g_has_focus = !down;
    if (!down) input_focus_gained();
    return true;
  }
  if (w == "quit") { if (down) request_quit(); return true; }
  if (w == "ramdump") {  // test aid: the machine's RAM to <user dir>/ram-<tick>.bin
    if (down) {
      std::vector<uint8_t> ram(65536);
      if (g_backend->read_memory(0, ram.data(), ram.size()))
        write_file(save_dir() + "/ram-" + std::to_string(g_tick) + ".bin", ram.data(), ram.size());
    }
    return true;
  }
  if (w == "shot") {
    if (down && !g_opt.screenshot.empty()) {
      std::string stem = g_opt.screenshot.substr(0, g_opt.screenshot.size() - 4);
      save_screenshot(stem + "-" + std::to_string(g_tick) + ".bmp");
    }
    return true;
  }
  if (menu_open() && down) {
    if (w == "up") { menu_nav(Nav::Up); return true; }
    if (w == "down") { menu_nav(Nav::Down); return true; }
    if (w == "left") { menu_nav(Nav::Left); return true; }
    if (w == "right") { menu_nav(Nav::Right); return true; }
    if (w == "enter") { menu_nav(Nav::Select); return true; }
    if (w == "back") { menu_nav(Nav::Back); return true; }
  }
  return false;
}

void sb_deliver(int i, bool down) {
  for (size_t f = 0; f < ctl_count(); ++f)
    if (g_game->functions[f].code)
      for (int pb : ctl_pads(f))
        if (pb == sb_button(i)) send_func(f, down);
}

void sb_step() {
  const Uint32 now = now_ms();
  for (int i = 0; i < 2; ++i) {
    StartBack &s = g_sb[i];
    if (s.down && !s.delivered && !s.swallow && now - s.ts >= kDeferMs && !menu_open() && !g_boot_hold) {
      s.delivered = true;
      sb_deliver(i, true);
    }
    if (s.tap && static_cast<int>(now - s.release_at) >= 0) {
      sb_deliver(i, false);
      s.delivered = s.tap = false;
    }
  }
}

}  // namespace

int script_tick() { return g_tick; }
void script_advance() { ++g_tick; }
Uint32 now_ms() { return g_opt.frames >= 0 ? static_cast<Uint32>(g_tick) * 20u : SDL_GetTicks(); }

void input_release_all() {
  // (delayed game actions are kept: they only count emulated frames, so a sequence started
  // before the menu opened simply continues after it)
  for (unsigned p = 0; p < kMaxPorts; ++p) g_latch[p] = raw_pad(p).buttons;  // held now: ignored until released
  for (const auto &e : g_game->key_map) send_key(e.code, false);
  for (const auto &e : g_game->text_keys) send_key(e.code, false);
  for (const auto &f : g_game->functions) send_key(f.code, false);
  memset(g_kdown, 0, sizeof g_kdown);
  memset(g_text_sent, 0, sizeof g_text_sent);
  for (unsigned p = 0; p < kMaxPorts; ++p) {
    g_backend->set_pad(p, PadState());
    g_last_mask[p] = 0xFFFF;
  }
  g_mbtn = 0;
  g_macc_x = g_macc_y = 0;
  if (mouse_port() >= 0) g_backend->set_mouse(static_cast<unsigned>(mouse_port()), MouseState());
}

void input_apply_pads() {
  for (unsigned port = 0; port < kMaxPorts && port < g_game->port_devices.size(); ++port) {
    if (g_game->port_devices[port] != PortDevice::Joypad && !is_hot(port)) continue;
    PadState st = raw_pad(port);
    if (is_hot(port)) {
      const bool active = st.buttons || abs(st.lx) > 12000 || abs(st.ly) > 12000;
      if (active && !g_boot_hold) hot_switch(port, PortDevice::Joypad);
      if (!port_is_joypad(port)) continue;  // the mouse owns the port now
    }
    if (g_boot_hold) {  // nothing typed or pressed during the hidden boot reaches the game
      g_latch[port] = st.buttons;
      st = PadState();
    } else {
      g_latch[port] &= st.buttons;
      st.buttons &= static_cast<uint16_t>(~g_latch[port]);
    }
    g_backend->set_pad(port, st);
    if (st.buttons != g_last_mask[port]) vlog("pad%u mask 0x%04x\n", port, st.buttons);
    g_last_mask[port] = st.buttons;
  }
}

bool mouse_wanted() {
  return g_game->uses_mouse && g_set.mouse_on && g_has_focus && !menu_open() && !g_boot_hold && mouse_port() >= 0;
}

void input_mouse_motion(int xrel, int yrel) {
  if (!mouse_wanted()) return;
  double sx, sy;
  if (!video_mouse_scale(sx, sy)) return;
  g_macc_x += xrel * sx * g_set.mouse_sens / 100.0;
  g_macc_y += yrel * sy * g_set.mouse_sens / 100.0;
}

void input_mouse_button(int b, bool down) {
  if (!mouse_wanted()) {
    if (!down) g_mbtn &= ~(1 << (b == SDL_BUTTON_LEFT ? 0 : b == SDL_BUTTON_RIGHT ? 1 : 2));
    return;
  }
  const int bit = 1 << (b == SDL_BUTTON_LEFT ? 0 : b == SDL_BUTTON_RIGHT ? 1 : 2);
  if (down) g_mbtn |= bit;
  else g_mbtn &= ~bit;
}

void input_focus_gained() {
  g_focus_armed = true;
  g_focus_ms = now_ms();
}

// The click that gives the window focus must not reach the game (nor its release).
bool input_mouse_swallowed(bool down, int b) {
  const int bit = 1 << (b & 7);
  if (down) {
    const bool swallow = g_focus_armed && now_ms() - g_focus_ms < 400;
    g_focus_armed = false;
    if (swallow) g_swallowed_btn |= bit;
    return swallow;
  }
  if (g_swallowed_btn & bit) {
    g_swallowed_btn &= ~bit;
    return true;
  }
  return false;
}

void input_mouse_reset() {
  g_mbtn = 0;
  g_macc_x = g_macc_y = 0;
  if (mouse_port() >= 0) g_backend->set_mouse(static_cast<unsigned>(mouse_port()), MouseState());
}

void input_apply_mouse() {
  const int port = mouse_port();
  if (port < 0) return;
  MouseState m;
  if (is_hot(static_cast<unsigned>(port)) && !port_is_mouse(static_cast<unsigned>(port))) {
    // Joypad is active on the shared port: real mouse input takes it over, otherwise stay idle.
    if (mouse_wanted() && (g_macc_x >= 1 || g_macc_x <= -1 || g_macc_y >= 1 || g_macc_y <= -1 || g_mbtn))
      hot_switch(static_cast<unsigned>(port), PortDevice::Mouse);
    if (!port_is_mouse(static_cast<unsigned>(port))) return;
  }
  if (mouse_wanted()) {
    m.dx = static_cast<int>(g_macc_x);  // truncation keeps the fraction for the next frame
    m.dy = static_cast<int>(g_macc_y);
    g_macc_x -= m.dx;
    g_macc_y -= m.dy;
    m.buttons = static_cast<uint8_t>(g_mbtn);
    if (m.dx || m.dy) vlog("mouse dx=%d dy=%d\n", m.dx, m.dy);
  }
  static uint8_t last = 0;
  if (m.buttons != last) vlog("mouse buttons 0x%02x\n", m.buttons);
  last = m.buttons;
  g_backend->set_mouse(static_cast<unsigned>(port), m);
}

// Nothing reaches the game while the boot hold is active (a key typed into the machine
// during start-up could end up in its keyboard buffer and break it); no key can be down
// from before the hold, so releases are dropped as well.
void send_key(unsigned code, bool down) {
  if (g_boot_hold) return;
  if (code) {
    vlog("game key %u %s\n", code, down ? "down" : "up");
    g_backend->key_event(code, down);
  }
}

void input_key_released(SDL_Scancode sc) { g_kdown[sc] = false; }

// Game-bound keys: functions first (their keys may have been rebound), then the fixed map,
// then the layout-dependent text keys.
void input_key_event(SDL_Scancode sc, bool down, SDL_Keycode keycode) {
  if (down && g_boot_hold) return;  // not even remembered: it must not appear pressed afterwards
  if (keycode == SDLK_UNKNOWN) keycode = SDL_GetKeyFromScancode(sc);
  g_kdown[sc] = down;
  bool owned = false;
  for (size_t i = 0; i < ctl_count(); ++i)
    for (SDL_Scancode k : ctl_keys(i))
      if (k == sc) {
        owned = true;
        if (g_game->functions[i].code) send_func(i, down);
      }
  if (owned) return;
  for (const auto &e : g_game->key_map)
    if (e.scancode == sc) {
      send_key(e.code, down);
      return;
    }
  if (!down) {
    if (g_text_sent[sc]) send_key(g_text_sent[sc], false);
    g_text_sent[sc] = 0;
    return;
  }
  for (const auto &t : g_game->text_keys)
    if (t.keycode == keycode) {
      g_text_sent[sc] = t.code;
      send_key(t.code, true);
      return;
    }
}

bool script_parse(const std::string &spec) {
  size_t pos = 0;
  while (pos < spec.size()) {
    size_t end = spec.find(',', pos);
    if (end == std::string::npos) end = spec.size();
    std::string item = spec.substr(pos, end - pos);
    pos = end + 1;
    size_t c1 = item.find(':');
    if (c1 == std::string::npos) return false;
    Scripted sc{atoi(item.c_str()), 5, "", false};
    size_t c2 = item.find(':', c1 + 1);
    sc.what = item.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1);
    if (c2 != std::string::npos) sc.len = std::max(1, atoi(item.c_str() + c2 + 1));
    g_script.push_back(sc);
  }
  return true;
}

// Called once per emulated frame, before the backend runs.
void script_step(int tick) {
  for (auto &sc : g_script) {
    const bool should = tick >= sc.start && tick < sc.start + sc.len;
    if (should == sc.active) continue;
    sc.active = should;
    if (should && sc.what == "save") { save_state(); continue; }
    if (should && sc.what == "load") { load_state(); continue; }
    if (shell_action(sc.what, should)) continue;
    // Function ids with a `joy` are read by raw_pad() from the active flags; function ids
    // with a key code send it here.
    bool handled = false;
    for (const auto &f : g_game->functions)
      if (f.id == sc.what) {
        handled = true;
        if (f.code && !menu_open()) send_func(static_cast<size_t>(&f - &g_game->functions[0]), should);
      }
    if (!handled && !menu_open() && g_game->key_for_name) send_key(g_game->key_for_name(sc.what), should);
  }
}

// Gamepad Start / Back are shell-owned: Guide, or Start+Back held for 1 s, opens the shell
// menu and neither button reaches the game; a short press reaches it on release.
void input_pad_button(SDL_JoystickID which, SDL_GameControllerButton b, bool down) {
  int slot = slot_of(which);
  if (slot < 0) slot = 0;
  if (down) {
    if (menu_card_active()) {  // first-run card: any button dismisses it
      menu_card_dismiss();
      g_pmenu[b] = true;
      return;
    }
    if (menu_capturing() && menu_capture_pad(b)) { g_pmenu[b] = true; return; }
    device_used(Device::Pad);
    if (slot < g_npads) g_pad_style = style_of(g_pads[slot].c);
  }
  if (b == SDL_CONTROLLER_BUTTON_GUIDE) {
    if (down && !menu_open()) open_menu(false);
    return;
  }
  if (b == SDL_CONTROLLER_BUTTON_START || b == SDL_CONTROLLER_BUTTON_BACK) {
    const int i = b == SDL_CONTROLLER_BUTTON_START ? 0 : 1;
    StartBack &s = g_sb[i], &o = g_sb[1 - i];
    if (down) {
      s = StartBack();
      s.down = true;
      s.slot = slot;
      s.ts = now_ms();
      if (menu_open() || g_boot_hold) {
        s.swallow = true;
      } else if (o.down) {  // the combination: neither button is for the game
        s.swallow = o.swallow = true;
        if (o.delivered) { sb_deliver(1 - i, false); o.delivered = o.tap = false; }
      }
    } else {
      s.down = false;
      if (s.delivered) {
        if (!s.tap) { sb_deliver(i, false); s.delivered = false; }
      } else if (!s.swallow && !menu_open() && !g_boot_hold) {  // short press: pressed + released
        s.delivered = s.tap = true;
        s.release_at = now_ms() + kTapMs;
        s.ts = now_ms();
        sb_deliver(i, true);
      }
      s.swallow = false;
    }
    if (!g_sb[0].down || !g_sb[1].down) g_hold_fired = false;
    return;  // a press is delivered later (sb_step) unless it turns out to be the combination
  }
  if (menu_open()) {
    if (!down) { g_pmenu[b] = false; return; }
    g_pmenu[b] = true;  // its release (after the menu closed) is the menu's too
    switch (b) {
      case SDL_CONTROLLER_BUTTON_DPAD_UP: menu_nav(Nav::Up); break;
      case SDL_CONTROLLER_BUTTON_DPAD_DOWN: menu_nav(Nav::Down); break;
      case SDL_CONTROLLER_BUTTON_DPAD_LEFT: menu_nav(Nav::Left); break;
      case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: menu_nav(Nav::Right); break;
      case SDL_CONTROLLER_BUTTON_A: menu_nav(Nav::Select); break;
      case SDL_CONTROLLER_BUTTON_B: menu_nav(Nav::Back); break;
      case SDL_CONTROLLER_BUTTON_X: menu_nav(Nav::Delete); break;
      case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: menu_nav(Nav::TabPrev); break;
      case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: menu_nav(Nav::TabNext); break;
      default: break;
    }
    return;
  }
  if (!down && g_pmenu[b]) { g_pmenu[b] = false; return; }
  if (slot != 0) return;  // key functions follow pad #1 only
  for (size_t i = 0; i < ctl_count(); ++i)
    if (g_game->functions[i].code)
      for (int pb : ctl_pads(i))
        if (pb == b) send_func(i, down);
}

// Called before every emulated frame: runs the delayed actions that are due.
void input_ports_init() {
  for (unsigned p = 0; p < kMaxPorts && p < g_game->port_devices.size(); ++p) {
    PortDevice d = g_game->port_devices[p];
    if (d == PortDevice::JoypadOrMouse) {
      g_hot_cur[p] = d = PortDevice::Joypad;
      g_hot_init[p] = false;
    }
    g_backend->set_port_device(p, d);
  }
}

void input_actions_step() {
  for (size_t i = 0; i < g_delayed.size();) {
    if (--g_delayed[i].frames > 0) { ++i; continue; }
    auto fn = std::move(g_delayed[i].fn);
    g_delayed.erase(g_delayed.begin() + static_cast<long>(i));
    fn();
  }
}

void pad_guide_or_hold_check() {
  sb_step();
  if (menu_open() || g_boot_hold || !g_sb[0].down || !g_sb[1].down || g_hold_fired) return;
  if (now_ms() - std::max(g_sb[0].ts, g_sb[1].ts) >= 1000) {
    g_hold_fired = true;
    open_menu(false);
  }
}

// Left stick drives the menu (edge-triggered per axis).
void input_axis(SDL_GameControllerAxis a, int value) {
  int idx = a == SDL_CONTROLLER_AXIS_LEFTX ? 0 : a == SDL_CONTROLLER_AXIS_LEFTY ? 1 : -1;
  if (idx < 0) return;
  int &st = g_axis_state[idx];
  const int ns = value > 20000 ? 1 : value < -20000 ? -1 : (std::abs(value) < 8000 ? 0 : st);
  if (ns == st) return;
  st = ns;
  if (ns) device_used(Device::Pad);
  if (!ns || !menu_open()) return;
  if (idx == 0) menu_nav(ns > 0 ? Nav::Right : Nav::Left);
  else menu_nav(ns > 0 ? Nav::Down : Nav::Up);
}

// Adds the pad with this joystick device index unless it is already open: the pads present at
// start are opened by input_open_pads() AND announced by SDL_CONTROLLERDEVICEADDED.
void input_pad_added(int idx) {
  if (!SDL_IsGameController(idx)) return;
  if (g_test_run && !SDL_JoystickIsVirtual(idx)) return;  // tests are independent of the pads on the machine
  const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(idx);
  if (slot_of(id) >= 0 || g_npads >= kMaxPads) return;
  SDL_GameController *c = SDL_GameControllerOpen(idx);
  if (!c) return;
  g_pads[g_npads].c = c;
  g_pads[g_npads].id = id;
  ++g_npads;
  if (g_npads == 1) g_pad_style = style_of(c);
  vlog("pad added: slot %d of %d (%s)\n", g_npads - 1, g_npads, SDL_GameControllerName(c) ? SDL_GameControllerName(c) : "?");
}

bool input_has_pad() { return g_npads > 0; }
size_t input_pad_count() { return static_cast<size_t>(g_npads); }

void input_open_pads() {
  for (int i = 0; i < SDL_NumJoysticks(); ++i) input_pad_added(i);
}

void input_test_vpads(int n, int type) {
  for (int i = 0; i < n; ++i) vpad_add(type);
}

void input_pad_removed(SDL_JoystickID id) {
  const int slot = slot_of(id);
  if (slot < 0) return;
  SDL_GameControllerClose(g_pads[slot].c);
  for (int i = slot; i + 1 < g_npads; ++i) g_pads[i] = g_pads[i + 1];  // the others keep their order
  g_pads[--g_npads] = Pad();
  vlog("pad removed: slot %d, %d left\n", slot, g_npads);
  for (auto &s : g_sb) s = StartBack();
  for (auto &l : g_latch) l = 0;
  // An unplugged pad must not leave a key held in the game.
  for (unsigned code : g_game->release_on_pad_loss) send_key(code, false);
  if (g_npads) g_pad_style = style_of(g_pads[0].c);
}

void input_close_pads() {
  for (auto &p : g_pads) {
    if (p.c) SDL_GameControllerClose(p.c);
    p = Pad();
  }
  g_npads = 0;
}

}  // namespace detail
}  // namespace retro_shell
