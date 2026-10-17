// Controls: the game's rebindable functions (keyboard + gamepad), their ini storage
// ([keys] / [pad], by function id) and the active input device that decides what the
// controls card shows.
#include <algorithm>
#include <cctype>

#include "internal.h"

namespace retro_shell {
namespace detail {

Device g_device = Device::Keyboard;
PadStyle g_pad_style = PadStyle::Xbox;

namespace {

struct Bind {
  SDL_Scancode key = SDL_SCANCODE_UNKNOWN;
  int pad = SDL_CONTROLLER_BUTTON_INVALID;
};
std::vector<Bind> g_bind;

const std::vector<ControlFunc> &funcs() { return g_game->functions; }

bool is_dpad(int b) {
  return b == SDL_CONTROLLER_BUTTON_DPAD_UP || b == SDL_CONTROLLER_BUTTON_DPAD_DOWN ||
         b == SDL_CONTROLLER_BUTTON_DPAD_LEFT || b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
}

// A key of the game's fixed key map (menus of the original) cannot be bound to a function.
bool key_fixed(SDL_Scancode sc) {
  for (const auto &e : g_game->key_map)
    if (e.scancode == sc) return true;
  return false;
}

std::string upper(std::string s) {
  for (auto &c : s) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  return s;
}

void store(size_t i) {
  const ControlFunc &f = funcs()[i];
  if (g_bind[i].key == f.key) g_ini.erase("keys", f.id);
  else g_ini.set("keys", f.id, g_bind[i].key == SDL_SCANCODE_UNKNOWN ? "none" : SDL_GetScancodeName(g_bind[i].key));
  if (g_bind[i].pad == f.pad) g_ini.erase("pad", f.id);
  else
    g_ini.set("pad", f.id,
              g_bind[i].pad == SDL_CONTROLLER_BUTTON_INVALID
                  ? "none"
                  : SDL_GameControllerGetStringForButton(static_cast<SDL_GameControllerButton>(g_bind[i].pad)));
}

}  // namespace

void device_used(Device d) {
  if (d == Device::Mouse && !g_game->uses_mouse) return;
  g_device = d;
}

void controls_init() {
  g_bind.assign(funcs().size(), Bind());
  for (size_t i = 0; i < funcs().size(); ++i) {
    g_bind[i].key = funcs()[i].key;
    g_bind[i].pad = funcs()[i].pad;
    std::string v;
    if (g_ini.get("keys", funcs()[i].id, v)) {
      if (v == "none") g_bind[i].key = SDL_SCANCODE_UNKNOWN;
      else if (SDL_GetScancodeFromName(v.c_str()) != SDL_SCANCODE_UNKNOWN && !key_reserved(SDL_GetScancodeFromName(v.c_str())) &&
               !key_fixed(SDL_GetScancodeFromName(v.c_str())))
        g_bind[i].key = SDL_GetScancodeFromName(v.c_str());
    }
    // A button can only be (re)bound where the game allows it.
    if (funcs()[i].pad != SDL_CONTROLLER_BUTTON_INVALID && g_ini.get("pad", funcs()[i].id, v)) {
      const SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(v.c_str());
      if (v == "none") g_bind[i].pad = SDL_CONTROLLER_BUTTON_INVALID;
      else if (b != SDL_CONTROLLER_BUTTON_INVALID && b != SDL_CONTROLLER_BUTTON_GUIDE && !is_dpad(b))
        g_bind[i].pad = b;
    }
  }
  // Drop duplicates a hand-edited ini could contain (first function keeps the key).
  for (size_t i = 0; i < g_bind.size(); ++i)
    for (size_t j = i + 1; j < g_bind.size(); ++j) {
      if (g_bind[j].key != SDL_SCANCODE_UNKNOWN && g_bind[j].key == g_bind[i].key) g_bind[j].key = funcs()[j].key;
      if (g_bind[j].pad != SDL_CONTROLLER_BUTTON_INVALID && g_bind[j].pad == g_bind[i].pad) g_bind[j].pad = funcs()[j].pad;
    }
}

size_t ctl_count() { return funcs().size(); }

bool key_reserved(SDL_Scancode sc) {
  switch (sc) {
    case SDL_SCANCODE_ESCAPE:
    case SDL_SCANCODE_F1:
    case SDL_SCANCODE_F5:
    case SDL_SCANCODE_F9:
    case SDL_SCANCODE_F11:
    case SDL_SCANCODE_LALT:
    case SDL_SCANCODE_RALT:
      return true;
    default: return false;
  }
}

std::vector<SDL_Scancode> ctl_keys(size_t i) {
  std::vector<SDL_Scancode> out;
  if (i >= g_bind.size()) return out;
  if (g_bind[i].key != SDL_SCANCODE_UNKNOWN) out.push_back(g_bind[i].key);
  if (g_bind[i].key == funcs()[i].key)
    for (SDL_Scancode alt : funcs()[i].alt_keys) {
      bool claimed = false;
      for (size_t j = 0; j < g_bind.size(); ++j) claimed |= j != i && g_bind[j].key == alt;
      if (!claimed) out.push_back(alt);
    }
  return out;
}

std::vector<int> ctl_pads(size_t i) {
  std::vector<int> out;
  if (i >= g_bind.size()) return out;
  if (g_bind[i].pad != SDL_CONTROLLER_BUTTON_INVALID) out.push_back(g_bind[i].pad);
  if (g_bind[i].pad == funcs()[i].pad && funcs()[i].pad != SDL_CONTROLLER_BUTTON_INVALID)
    for (SDL_GameControllerButton alt : funcs()[i].alt_pads) {
      bool claimed = false;
      for (size_t j = 0; j < g_bind.size(); ++j) claimed |= j != i && g_bind[j].pad == alt;
      if (!claimed) out.push_back(alt);
    }
  return out;
}

bool ctl_key_owned(SDL_Scancode sc) {
  for (size_t i = 0; i < g_bind.size(); ++i)
    for (SDL_Scancode k : ctl_keys(i))
      if (k == sc) return true;
  return false;
}

bool ctl_rebindable(size_t i, Device d) {
  if (i >= funcs().size()) return false;
  if (d == Device::Pad) return funcs()[i].pad != SDL_CONTROLLER_BUTTON_INVALID;
  return funcs()[i].key != SDL_SCANCODE_UNKNOWN;
}

// One mapping table on top of SDL_GetScancodeName: what a player would call the key.
std::string key_text(SDL_Scancode sc) {
  std::string n = upper(SDL_GetScancodeName(sc));
  if (sc == SDL_SCANCODE_UP) return std::string(1, kArrowUp);
  if (sc == SDL_SCANCODE_DOWN) return std::string(1, kArrowDown);
  if (sc == SDL_SCANCODE_LEFT) return std::string(1, kArrowLeft);
  if (sc == SDL_SCANCODE_RIGHT) return std::string(1, kArrowRight);
  if (n.compare(0, 7, "KEYPAD ") == 0) return "NUMPAD " + n.substr(7);
  static const struct { const char *from, *to; } names[] = {
      {"RETURN", "ENTER"}, {"ESCAPE", "ESC"}, {"DELETE", "DEL"}, {"PAGEUP", "PGUP"}, {"PAGEDOWN", "PGDN"},
      {"LEFT GUI", "LEFT WIN"}, {"RIGHT GUI", "RIGHT WIN"}, {"CAPSLOCK", "CAPS LOCK"}};
  for (const auto &a : names)
    if (n == a.from) return a.to;
  if (n.empty()) return "KEY " + std::to_string(static_cast<int>(sc));
  return n;
}

// Labels follow the pad's printing (the mapping itself is positional: SDL's A is the south
// button on every pad). Xbox: A B X Y LB RB START BACK; PlayStation: CROSS CIRCLE SQUARE
// TRIANGLE L1 R1 OPTIONS SHARE; Nintendo: B A Y X L R + -.
std::string pad_button_text(int b) {
  if (g_pad_style == PadStyle::PlayStation) {
    switch (b) {
      case SDL_CONTROLLER_BUTTON_A: return "CROSS";
      case SDL_CONTROLLER_BUTTON_B: return "CIRCLE";
      case SDL_CONTROLLER_BUTTON_X: return "SQUARE";
      case SDL_CONTROLLER_BUTTON_Y: return "TRIANGLE";
      case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return "L1";
      case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return "R1";
      case SDL_CONTROLLER_BUTTON_START: return "OPTIONS";
      case SDL_CONTROLLER_BUTTON_BACK: return "SHARE";
      case SDL_CONTROLLER_BUTTON_GUIDE: return "PS";
      default: break;
    }
  } else if (g_pad_style == PadStyle::Nintendo) {
    switch (b) {
      case SDL_CONTROLLER_BUTTON_A: return "B";
      case SDL_CONTROLLER_BUTTON_B: return "A";
      case SDL_CONTROLLER_BUTTON_X: return "Y";
      case SDL_CONTROLLER_BUTTON_Y: return "X";
      case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return "L";
      case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return "R";
      case SDL_CONTROLLER_BUTTON_START: return "+";
      case SDL_CONTROLLER_BUTTON_BACK: return "-";
      case SDL_CONTROLLER_BUTTON_GUIDE: return "HOME";
      default: break;
    }
  }
  switch (b) {
    case SDL_CONTROLLER_BUTTON_A: return "A";
    case SDL_CONTROLLER_BUTTON_B: return "B";
    case SDL_CONTROLLER_BUTTON_X: return "X";
    case SDL_CONTROLLER_BUTTON_Y: return "Y";
    case SDL_CONTROLLER_BUTTON_BACK: return "BACK";
    case SDL_CONTROLLER_BUTTON_GUIDE: return "GUIDE";
    case SDL_CONTROLLER_BUTTON_START: return "START";
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: return "L3";
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return "R3";
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return "LB";
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return "RB";
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return "DPAD UP";
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return "DPAD DOWN";
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return "DPAD LEFT";
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return "DPAD RIGHT";
    case SDL_CONTROLLER_BUTTON_INVALID: return "-";
    default: return "BTN " + std::to_string(b);
  }
}

std::string pad_hint_keys() {
  return pad_button_text(SDL_CONTROLLER_BUTTON_A) + " SELECT  " + pad_button_text(SDL_CONTROLLER_BUTTON_B) + " BACK";
}

std::string ctl_value(size_t i, Device d) {
  if (i >= funcs().size()) return "";
  const ControlFunc &f = funcs()[i];
  std::string out;
  auto add = [&out](const std::string &t) { out += (out.empty() ? "" : " ") + t; };
  if (d == Device::Mouse && !f.mouse_text.empty()) return f.mouse_text;
  if (d == Device::Pad) {
    if (f.pad == SDL_CONTROLLER_BUTTON_INVALID) return f.pad_text.empty() ? "-" : f.pad_text;
    for (int b : ctl_pads(i)) add(pad_button_text(b));
  } else {
    for (SDL_Scancode k : ctl_keys(i)) add(key_text(k));
  }
  return out.empty() ? "-" : out;
}

BindResult ctl_bind_key(size_t i, SDL_Scancode sc, std::string &other) {
  other.clear();
  if (i >= g_bind.size() || sc == SDL_SCANCODE_UNKNOWN || key_reserved(sc)) return BindResult::Reserved;
  if (key_fixed(sc)) return BindResult::Fixed;
  if (g_bind[i].key == sc) return BindResult::Ok;
  BindResult r = BindResult::Ok;
  for (size_t j = 0; j < g_bind.size(); ++j)
    if (j != i && g_bind[j].key == sc) {  // in use: swap places instead of duplicating
      g_bind[j].key = g_bind[i].key;
      other = funcs()[j].label;
      store(j);
      r = BindResult::Swapped;
    }
  g_bind[i].key = sc;
  store(i);
  return r;
}

BindResult ctl_bind_pad(size_t i, int button, std::string &other) {
  other.clear();
  if (i >= g_bind.size() || button == SDL_CONTROLLER_BUTTON_INVALID || button == SDL_CONTROLLER_BUTTON_GUIDE ||
      is_dpad(button))  // the D-pad always moves; it is not a function button
    return BindResult::Reserved;
  if (funcs()[i].pad == SDL_CONTROLLER_BUTTON_INVALID) return BindResult::Reserved;
  if (g_bind[i].pad == button) return BindResult::Ok;
  BindResult r = BindResult::Ok;
  for (size_t j = 0; j < g_bind.size(); ++j)
    if (j != i && g_bind[j].pad == button) {
      g_bind[j].pad = g_bind[i].pad;
      other = funcs()[j].label;
      store(j);
      r = BindResult::Swapped;
    }
  g_bind[i].pad = button;
  store(i);
  return r;
}

void ctl_reset() {
  for (size_t i = 0; i < g_bind.size(); ++i) {
    g_bind[i].key = funcs()[i].key;
    g_bind[i].pad = funcs()[i].pad;
    store(i);  // equal to the defaults: removes the ini entries
  }
}

}  // namespace detail
}  // namespace retro_shell
