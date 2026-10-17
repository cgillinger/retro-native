// Menu: pause menu and settings, drawn into the game's pixel grid (so it passes through the
// same filter as toasts). Keyboard, gamepad and mouse all end up in menu_nav() / the mouse
// functions; the game is paused while the menu is open (see app.cpp).
#include <algorithm>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>

#include "internal.h"

namespace retro_shell {
namespace detail {
namespace {

enum class Scr { Main, Save, Load, Settings, Controls, Rebind, Card, Help, ConfirmOverwrite, ConfirmDelete, ConfirmQuit, ConfirmReset };
enum Tab { TAB_CONTROLS = 0, TAB_VIDEO, TAB_AUDIO, TAB_ADVANCED, TAB_COUNT };
const char *kTabNames[TAB_COUNT] = {"CONTROLS", "VIDEO", "AUDIO", "ADVANCED"};

struct Item {
  std::string label, value;
  bool selectable = true;   // false: read-only row
  bool enabled = true;      // false: shown dimmed, Enter does nothing
  bool adjustable = false;  // left/right change the value
  std::function<void(int)> act;  // dir: 0 = select / click, +1 / -1 = adjust
};

struct View {
  std::string title;
  std::vector<std::string> body;  // lines under the title (confirmations)
  std::vector<Item> items;
  bool tabs = false;
  bool compact = false;  // tighter rows (the controls card has many lines)
  std::string hint;      // bottom line, "" = the generic navigation hint
  bool thumb = false;    // Save / Load page: room for the slot preview under the list
};

struct Rect { int x = 0, y = 0, w = 0, h = 0; };

struct Layout {
  int s = 1, cw = 6, lh = 11, ty = 2, pad = 5, cols = 36;
  Rect panel;
  int title_y = 0, tabs_y = 0, body_y = 0, items_y = 0, hint_y = 0;
  int first = 0, count = 0;           // visible window of items
  Rect tab_rects[TAB_COUNT];
  std::vector<Rect> item_rects;       // for items first .. first+count-1
  bool hint = false;
  Rect thumb;                         // preview box (w == 0: none)
};

// ---- state
bool g_open = false;
std::vector<Scr> g_stack;
int g_tab = TAB_CONTROLS;
int g_pending_slot = 0;
std::map<int, int> g_sel;  // selection per screen (-1 = tab row)
int g_scroll = 0;
bool g_swallow_up = false;
int g_capture = -1;         // function waiting for a key / button on the rebinding screen
bool g_capture_pad = false; // ... and whether the answer is a gamepad button

int sel_key() { return g_stack.back() == Scr::Settings ? 100 + g_tab : static_cast<int>(g_stack.back()); }

std::string to_upper(std::string s) {
  for (auto &c : s) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  return s;
}

std::string slot_label(int slot) { return slot == 0 ? "QUICK SLOT" : "SLOT " + std::to_string(slot); }

// Preview of the highlighted slot, loaded on demand (cleared whenever slots change).
int g_thumb_slot = -1;
bool g_thumb_ok = false;
Thumb g_thumb;
void thumb_reset() { g_thumb_slot = -1; }
const Thumb *thumb_for(int slot) {
  if (slot != g_thumb_slot) {
    g_thumb_slot = slot;
    g_thumb_ok = thumb_load(slot, g_thumb);
  }
  return g_thumb_ok ? &g_thumb : nullptr;
}

std::string slot_value(int slot) {
  long long t;
  if (!slot_time(slot, t)) return "EMPTY";
  const time_t tt = static_cast<time_t>(t);
  const struct tm *lt = localtime(&tt);
  if (!lt) return "?";
  char buf[64];
  snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d", lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour,
           lt->tm_min);
  return buf;
}

void push(Scr s) {
  g_stack.push_back(s);
  g_scroll = 0;
}

void pop() {
  g_capture = -1;
  g_stack.pop_back();
  g_scroll = 0;
  if (g_stack.empty()) menu_close();
}

Item action(const std::string &label, std::function<void(int)> f) {
  Item i;
  i.label = label;
  i.act = std::move(f);
  return i;
}

Item toggle(const std::string &label, bool on, std::function<void(bool)> set) {
  Item i;
  i.label = label;
  i.value = on ? "ON" : "OFF";
  i.adjustable = true;
  i.act = [on, set](int) { set(!on); };
  return i;
}

// Cycles through `names`: `cur` is the current index, `set` receives the new one.
Item choice(const std::string &label, const std::vector<std::string> &names, int cur, std::function<void(int)> set) {
  Item i;
  i.label = label;
  const int n = static_cast<int>(names.size());
  i.value = names[static_cast<size_t>(cur)];
  i.adjustable = true;
  i.act = [n, cur, set](int dir) { set((cur + (dir == 0 ? 1 : dir) + n) % n); };
  return i;
}

Item info(const std::string &label, const std::string &value) {
  Item i;
  i.label = label;
  i.value = value;
  i.selectable = false;
  return i;
}

Item game_option(size_t idx) {
  const GameOption &o = g_game->options[idx];
  std::vector<std::string> names;
  int cur = 0;
  const std::string v = option_value(idx);
  for (size_t k = 0; k < o.choices.size(); ++k) {
    names.push_back(o.choices[k].label);
    if (o.choices[k].value == v) cur = static_cast<int>(k);
  }
  return choice(o.label, names, cur, [idx](int n) { set_option(idx, g_game->options[idx].choices[static_cast<size_t>(n)].value); });
}

void add_options(View &v, const char *section) {
  for (size_t i = 0; i < g_game->options.size(); ++i) {
    const std::string &sec = g_game->options[i].section;
    const bool known = sec == "video" || sec == "audio";
    if (sec == section || (!known && !strcmp(section, "advanced"))) v.items.push_back(game_option(i));
  }
}

void do_save(int slot) {
  thumb_reset();
  if (!save_slot(slot)) return;
  menu_close();
  toast(slot == 0 ? "SAVED" : ("SAVED SLOT " + std::to_string(slot)).c_str());
}

void do_load(int slot) {
  if (!load_slot(slot)) {
    toast("LOAD FAILED");
    return;
  }
  menu_close();
  toast("LOADED");
}

// The controls card: every game function with the key / button of the active input device,
// the game's own notes and the shell hotkeys. `with_actions` adds the "Change..." entry.
void add_controls_rows(View &v, bool with_actions) {
  const Device d = g_device;
  const unsigned bit = d == Device::Pad ? DEV_PAD : d == Device::Mouse ? DEV_MOUSE : DEV_KEYBOARD;
  // The four directions collapse into one MOVE row when they sit on their default controls
  // (arrow keys / D-pad + left stick); anything rebound is listed one by one.
  int dir_idx[4] = {-1, -1, -1, -1};
  for (size_t i = 0; i < ctl_count(); ++i) {
    const int j = g_game->functions[i].joy;
    if (j >= static_cast<int>(PAD_UP) && j <= static_cast<int>(PAD_RIGHT) && (g_game->functions[i].devices & bit)) dir_idx[j - static_cast<int>(PAD_UP)] = static_cast<int>(i);
  }
  bool collapse = d != Device::Mouse;
  static const SDL_Scancode arrows[4] = {SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT};
  for (int k = 0; k < 4 && collapse; ++k) {
    if (dir_idx[k] < 0) { collapse = false; break; }
    const size_t i = static_cast<size_t>(dir_idx[k]);
    if (d == Device::Pad) collapse = g_game->functions[i].pad == SDL_CONTROLLER_BUTTON_INVALID;
    else {
      const auto ks = ctl_keys(i);
      collapse = ks.size() == 1 && ks[0] == arrows[k];
    }
  }
  bool move_done = false;
  for (size_t i = 0; i < ctl_count(); ++i) {
    if (!(g_game->functions[i].devices & bit)) continue;
    const int j = g_game->functions[i].joy;
    if (collapse && j >= static_cast<int>(PAD_UP) && j <= static_cast<int>(PAD_RIGHT)) {
      if (!move_done) {
        move_done = true;
        const std::string arrows_txt = std::string(1, kArrowLeft) + " " + kArrowRight + " " + kArrowUp + " " + kArrowDown;
        v.items.push_back(info(g_game->move_label, d == Device::Pad ? "D-PAD OR LEFT STICK" : arrows_txt));
      }
      continue;
    }
    v.items.push_back(info(g_game->functions[i].label, ctl_value(i, d)));
  }
  for (const auto &n : g_game->control_notes) {
    if (!(n.devices & bit)) continue;
    v.items.push_back(info(n.action, d == Device::Pad && !n.pad.empty()     ? n.pad
                                     : d == Device::Mouse && !n.mouse.empty() ? n.mouse
                                                                              : n.keyboard));
  }
  v.items.push_back(info("MENU", d == Device::Pad ? pad_button_text(SDL_CONTROLLER_BUTTON_GUIDE) + " OR " +
                                                          pad_button_text(SDL_CONTROLLER_BUTTON_START) + "+" +
                                                          pad_button_text(SDL_CONTROLLER_BUTTON_BACK) + " 1S"
                                                    : "ESC F1"));
  v.items.push_back(info("QUICK SAVE / LOAD", "F5 / F9"));
  v.items.push_back(info("FULLSCREEN", "F11 ALT+ENTER"));
  if (with_actions) v.items.push_back(action("CHANGE...", [](int) { push(Scr::Rebind); }));
}

// Mouse rows of the Controls tab (mouse games).
void add_mouse_rows(View &v) {
  v.items.push_back(toggle("MOUSE", g_set.mouse_on, [](bool b) { set_mouse_on(b); }));
  Item sens;
  sens.label = "MOUSE SENSITIVITY";
  sens.value = std::to_string(g_set.mouse_sens) + "%";
  sens.adjustable = true;
  const int cur = g_set.mouse_sens;
  sens.act = [cur](int dir) { set_mouse_sens(dir == 0 ? (cur >= 300 ? 20 : cur + 20) : cur + 10 * dir); };
  v.items.push_back(sens);
}

// The start help (STANDARD section 10): where the menu and the controls are, for the device
// in use (rebuilt every frame, so it follows a change of device while it is up).
std::vector<std::string> start_help_lines() {
  std::vector<std::string> l;
  if (g_device == Device::Pad)
    l.push_back("PRESS " + pad_button_text(SDL_CONTROLLER_BUTTON_GUIDE) + " OR HOLD " +
                pad_button_text(SDL_CONTROLLER_BUTTON_START) + "+" + pad_button_text(SDL_CONTROLLER_BUTTON_BACK) + " FOR MENU");
  else
    l.push_back("PRESS ESC FOR MENU & CONTROLS");
  if (g_game->uses_mouse && g_device == Device::Mouse) l.push_back("ESC RELEASES THE MOUSE");
  return l;
}

View build_view() {
  View v;
  switch (g_stack.back()) {
    case Scr::Main:
      v.title = g_game->title;
      v.items = {action("RESUME", [](int) { menu_close(); }), action("CONTROLS", [](int) { push(Scr::Controls); }),
                 action("SAVE", [](int) { push(Scr::Save); }), action("LOAD", [](int) { push(Scr::Load); }),
                 action("SETTINGS", [](int) { push(Scr::Settings); }), action("QUIT", [](int) { push(Scr::ConfirmQuit); })};
      break;
    case Scr::Save:
    case Scr::Load: {
      const bool saving = g_stack.back() == Scr::Save;
      v.title = saving ? "SAVE GAME" : "LOAD GAME";
      v.thumb = true;
      const int latest = latest_slot();
      for (int slot = 0; slot <= 5; ++slot) {
        long long t;
        const bool exists = slot_time(slot, t);
        Item i = action(slot_label(slot) + (slot == latest ? " *" : ""), [saving, slot, exists](int) {
          if (saving) {
            if (exists) {
              g_pending_slot = slot;
              push(Scr::ConfirmOverwrite);
              g_sel[static_cast<int>(Scr::ConfirmOverwrite)] = 1;  // default: NO
            } else {
              do_save(slot);
            }
          } else {
            do_load(slot);
          }
        });
        i.value = slot_value(slot);
        i.enabled = saving || exists;
        v.items.push_back(i);
      }
      break;
    }
    case Scr::Settings: {
      v.title = "SETTINGS";
      v.tabs = true;
      switch (g_tab) {
        case TAB_CONTROLS:
          add_controls_rows(v, true);
          if (g_game->uses_mouse) add_mouse_rows(v);
          v.compact = true;
          break;
        case TAB_VIDEO:
          v.items.push_back(toggle("FULLSCREEN", g_set.fullscreen, [](bool b) { set_fullscreen(b); }));
          v.items.push_back(choice("SCALING", {"FIT", "INTEGER"}, g_set.integer ? 1 : 0, [](int n) { set_integer(n == 1); }));
          if (aspect_count() > 1) {
            std::vector<std::string> names;
            for (size_t i = 0; i < aspect_count(); ++i) names.push_back(aspect_label(i));
            v.items.push_back(choice("ASPECT", names, g_set.aspect, [](int n) { set_aspect(n); }));
          }
          v.items.push_back(choice("FILTER",
                                   {g_set.filter == FILTER_AUTO ? std::string("AUTO (") + filter_active_name() + ")" : "AUTO",
                                    "SHARP", "CRT", "CRT+MASK"},
                                   g_set.filter, [](int n) { set_filter(n); }));
          {
            Item st;
            st.label = "CRT STRENGTH";
            st.value = std::to_string(g_set.crt_strength) + "%";
            st.adjustable = true;
            const int cur = g_set.crt_strength;
            st.act = [cur](int dir) { set_crt_strength(dir == 0 ? (cur <= 0 ? 100 : cur - 25) : cur + 25 * dir); };
            st.enabled = g_set.filter != FILTER_SHARP;  // Sharp has nothing to scale
            v.items.push_back(st);
          }
          if (video_has_crop()) v.items.push_back(toggle("GAME BORDER", g_set.game_border, [](bool b) { set_game_border(b); }));
          add_options(v, "video");
          break;
        case TAB_AUDIO: {
          Item vol;
          vol.label = "VOLUME";
          vol.value = std::to_string(g_set.volume) + "%";
          vol.adjustable = true;
          const int cur = g_set.volume;
          vol.act = [cur](int dir) { set_volume(dir == 0 ? (cur >= 100 ? 0 : cur + 10) : cur + 10 * dir); };
          v.items.push_back(vol);
          v.items.push_back(toggle("MUTE", g_set.mute, [](bool b) { set_mute(b); }));
          add_options(v, "audio");
          break;
        }
        default:
          v.items.push_back(toggle("PAUSE ON FOCUS LOSS", g_set.pause_on_focus_loss, [](bool b) { set_pause_on_focus_loss(b); }));
          v.items.push_back(toggle("SHOW HELP AT START", g_set.show_start_help, [](bool b) { set_show_start_help(b); }));
          v.items.push_back(choice("VSYNC", {"AUTO", "ON", "OFF"}, g_set.vsync, [](int n) { set_vsync(n); }));
          v.items.push_back(choice("AUDIO BUFFER", {"AUTO", "SMALL", "MEDIUM", "LARGE"}, g_set.audio_buffer,
                                   [](int n) { set_audio_buffer(n); }));
          v.items.push_back(toggle("FRAME DELAY", g_set.frame_delay, [](bool b) { set_frame_delay(b); }));
          add_options(v, "advanced");
          v.items.push_back(action("RESET ALL SETTINGS", [](int) {
            push(Scr::ConfirmReset);
            g_sel[static_cast<int>(Scr::ConfirmReset)] = 1;
          }));
          break;
      }
      break;
    }
    case Scr::Controls:
    case Scr::Card: {
      const bool card = g_stack.back() == Scr::Card;
      v.title = std::string("CONTROLS - ") + (g_device == Device::Pad ? "GAMEPAD" : g_device == Device::Mouse ? "MOUSE" : "KEYBOARD");
      v.compact = true;
      add_controls_rows(v, !card);
      if (card)
        v.hint = g_device == Device::Pad ? "PRESS ANY BUTTON TO CONTINUE"
                 : g_device == Device::Mouse ? "PRESS ANY KEY OR CLICK" : "PRESS ANY KEY TO CONTINUE";
      break;
    }
    case Scr::Help:
      v.title = g_game->title;
      v.body = start_help_lines();
      v.items = {action("OK", [](int) { menu_close(); }), action("DON'T SHOW AGAIN", [](int) {
                   set_show_start_help(false);
                   menu_close();
                   toast("HELP OFF - SETTINGS > ADVANCED");
                 })};
      break;
    case Scr::Rebind: {
      const bool pad = g_capture >= 0 ? g_capture_pad : g_device == Device::Pad;
      v.title = pad ? "CHANGE BUTTONS" : "CHANGE KEYS";
      if (g_capture >= 0) {
        const std::string &name = g_game->functions[static_cast<size_t>(g_capture)].label;
        if (pad) v.body = {"PRESS A BUTTON FOR " + name, "THE SAME BUTTON KEEPS IT"};
        else v.body = {"PRESS A KEY FOR " + name, "ESC OR RIGHT CLICK CANCELS"};
        v.hint = " ";
        break;
      }
      const Device d = pad ? Device::Pad : Device::Keyboard;
      for (size_t i = 0; i < ctl_count(); ++i) {
        if (!ctl_rebindable(i, d)) continue;
        Item it = action(g_game->functions[i].label, [i, pad](int) {
          g_capture = static_cast<int>(i);
          g_capture_pad = pad;
        });
        it.value = ctl_value(i, d);
        v.items.push_back(it);
      }
      v.items.push_back(action("RESET TO DEFAULTS", [](int) {
        ctl_reset();
        toast("DEFAULTS RESTORED");
      }));
      v.compact = v.items.size() > 12;
      break;
    }
    case Scr::ConfirmOverwrite:
      v.title = "SAVE GAME";
      v.body = {"OVERWRITE " + slot_label(g_pending_slot) + "?"};
      v.items = {action("YES", [](int) { const int s = g_pending_slot; do_save(s); }), action("NO", [](int) { pop(); })};
      break;
    case Scr::ConfirmDelete:
      v.title = "DELETE SAVE";
      v.body = {"DELETE " + slot_label(g_pending_slot) + "?"};
      v.items = {action("YES", [](int) {
                   const int s = g_pending_slot;
                   delete_slot(s);
                   thumb_reset();
                   pop();
                   toast(s == 0 ? "QUICK SLOT DELETED" : ("SLOT " + std::to_string(s) + " DELETED").c_str());
                 }),
                 action("NO", [](int) { pop(); })};
      break;
    case Scr::ConfirmQuit:
      v.title = "QUIT?";
      v.body = {"LEAVE THE GAME?"};
      v.items = {action("YES", [](int) { menu_close(); request_quit(); }), action("NO", [](int) { pop(); })};
      break;
    case Scr::ConfirmReset:
      v.title = "RESET?";
      v.body = {"RESET ALL SETTINGS TO DEFAULTS?"};
      v.items = {action("YES", [](int) { reset_all_settings(); pop(); }), action("NO", [](int) { pop(); })};
      break;
  }
  return v;
}

int first_selectable(const View &v) {
  for (size_t i = 0; i < v.items.size(); ++i)
    if (v.items[i].selectable) return static_cast<int>(i);
  return -1;
}

// Current selection, defaulting sensibly; -1 = tab row (only valid with tabs).
int current_sel(const View &v) {
  auto it = g_sel.find(sel_key());
  int s = it != g_sel.end() ? it->second : (v.tabs ? -1 : first_selectable(v));
  if (s >= static_cast<int>(v.items.size())) s = static_cast<int>(v.items.size()) - 1;
  if (s < 0 && !v.tabs) s = first_selectable(v);
  if (s >= 0 && !v.items[static_cast<size_t>(s)].selectable) s = first_selectable(v);
  return s;
}

Layout make_layout(const View &v, int W, int H, int sel, bool with_thumb = true) {
  Layout L;
  L.s = (W >= 600 && H >= 400) ? 2 : 1;
  const int s = L.s;
  L.cw = 6 * s;
  L.lh = (v.compact ? 9 : 11) * s;
  L.ty = (L.lh - 7 * s) / 2;
  L.pad = 5 * s;
  L.cols = std::max(20, std::min(40, (W - 4 * L.pad) / L.cw));
  const int gap = 3 * s;
  const int tw = W / 4, th = H / 4;  // preview box, 1/4 of the picture
  const bool thumb = v.thumb && with_thumb && tw >= 16 && th >= 16 && tw <= L.cols * L.cw;
  const int thumb_h = thumb ? th + 2 + gap : 0;
  const int fixed_h = L.lh /*title*/ + gap + (v.tabs ? L.lh + gap : 0) +
                      (v.body.empty() ? 0 : static_cast<int>(v.body.size()) * L.lh + gap) + gap + L.lh /*hint*/ + thumb_h;
  const int avail = H - 2 * (6 * s) - 2 * L.pad - fixed_h;
  const int max_rows = std::max(1, avail / L.lh);
  const int n = static_cast<int>(v.items.size());
  if (thumb && max_rows < n) return make_layout(v, W, H, sel, false);  // not enough room: no preview
  L.count = std::min(n, max_rows);
  if (sel >= 0) {
    if (sel < g_scroll) g_scroll = sel;
    if (sel >= g_scroll + L.count) g_scroll = sel - L.count + 1;
  }
  g_scroll = std::max(0, std::min(g_scroll, n - L.count));
  L.first = g_scroll;
  const int inner_w = L.cols * L.cw;
  const int panel_h = 2 * L.pad + fixed_h + L.count * L.lh;
  L.panel = {(W - inner_w - 2 * L.pad) / 2, (H - panel_h) / 2, inner_w + 2 * L.pad, panel_h};
  const int ix = L.panel.x + L.pad;
  int y = L.panel.y + L.pad;
  L.title_y = y;
  y += L.lh + gap;
  if (v.tabs) {
    L.tabs_y = y;
    int x = ix;
    for (int t = 0; t < TAB_COUNT; ++t) {
      const int w = text_width(kTabNames[t], s);
      L.tab_rects[t] = {x - s * 2, y, w + s * 4, L.lh};
      x += w + 2 * L.cw;
    }
    y += L.lh + gap;
  }
  L.body_y = y;
  if (!v.body.empty()) y += static_cast<int>(v.body.size()) * L.lh + gap;
  L.items_y = y;
  for (int i = 0; i < L.count; ++i) L.item_rects.push_back({ix, y + i * L.lh, inner_w, L.lh});
  y += L.count * L.lh + gap;
  if (thumb) {
    L.thumb = {L.panel.x + (L.panel.w - tw - 2) / 2, y, tw + 2, th + 2};
    y += thumb_h;
  }
  L.hint_y = y;
  L.hint = true;
  return L;
}

bool in_rect(const Rect &r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

void set_sel(const View &v, int s) {
  if (s >= -1 && s < static_cast<int>(v.items.size())) g_sel[sel_key()] = s;
}

void switch_tab(int dir) {
  g_tab = (g_tab + dir + TAB_COUNT) % TAB_COUNT;
  g_scroll = 0;
}

void activate(const View &v, int s, int dir) {
  if (s < 0 || s >= static_cast<int>(v.items.size())) return;
  const Item &it = v.items[static_cast<size_t>(s)];
  if (!it.enabled || !it.selectable || !it.act) return;
  if (dir != 0 && !it.adjustable) return;
  it.act(dir);
}

}  // namespace

bool menu_open() { return g_open; }

static void open_with(Scr first) {
  if (g_open) return;
  g_open = true;
  g_capture = -1;
  g_stack.clear();
  g_stack.push_back(first);
  g_scroll = 0;
  g_sel.clear();
  thumb_reset();
  SDL_ShowCursor(SDL_ENABLE);  // the main loop drops relative mouse mode while a menu is open
}

void menu_show(bool settings) { open_with(settings ? Scr::Settings : Scr::Main); }
void menu_show_first_run() { open_with(Scr::Card); }
void menu_show_start_help() { open_with(Scr::Help); }
bool menu_help_active() { return g_open && !g_stack.empty() && g_stack.back() == Scr::Help; }

bool menu_card_active() { return g_open && !g_stack.empty() && g_stack.back() == Scr::Card; }

void menu_card_dismiss() {
  if (!menu_card_active()) return;
  g_set.first_run_done = true;
  g_ini.set("general", "first_run_done", "true");
  menu_close();
}

bool menu_capturing() { return g_open && g_capture >= 0; }

static void capture_done(const std::string &fname, const std::string &what, BindResult r, const std::string &other) {
  toast(r == BindResult::Swapped ? ("SWAPPED WITH " + other).c_str() : (fname + " = " + what).c_str());
  g_capture = -1;
}

bool menu_capture_key(SDL_Scancode sc) {
  if (g_capture < 0) return false;
  if (sc == SDL_SCANCODE_ESCAPE) {  // cancels, and says why it cannot be a binding
    toast("ESC IS RESERVED");
    g_capture = -1;
    return true;
  }
  if (g_capture_pad) return true;  // waiting for a button: keys are ignored
  std::string other;
  const size_t i = static_cast<size_t>(g_capture);
  const BindResult r = ctl_bind_key(i, sc, other);
  if (r == BindResult::Reserved) {
    toast((key_text(sc) + " IS RESERVED").c_str());
    return true;
  }
  if (r == BindResult::Fixed) {  // a key the game itself needs for its menus
    toast((key_text(sc) + " IS A FIXED GAME KEY").c_str());
    return true;
  }
  capture_done(g_game->functions[i].label, key_text(sc), r, other);
  return true;
}

bool menu_capture_pad(int button) {
  if (g_capture < 0) return false;
  if (!g_capture_pad) return true;  // waiting for a key: buttons are ignored
  std::string other;
  const size_t i = static_cast<size_t>(g_capture);
  const BindResult r = ctl_bind_pad(i, button, other);
  if (r == BindResult::Reserved) {
    toast((pad_button_text(button) + " IS RESERVED").c_str());
    return true;
  }
  capture_done(g_game->functions[i].label, pad_button_text(button), r, other);
  return true;
}

void menu_toggle_settings() {
  if (!g_open) menu_show(true);
  else if (g_stack.back() == Scr::Settings) menu_close();
  else push(Scr::Settings);
}

void menu_close() {
  if (!g_open) return;
  g_open = false;
  g_capture = -1;
  g_stack.clear();
  SDL_ShowCursor(g_game->uses_mouse ? SDL_ENABLE : SDL_DISABLE);
}

bool mouse_click_swallowed(bool up) {
  if (!g_swallow_up) return false;
  if (up) g_swallow_up = false;
  return true;
}

// Save / Load pages: the highlighted slot, if it holds a save, goes to the delete confirmation.
static bool slot_page() { return !g_stack.empty() && (g_stack.back() == Scr::Save || g_stack.back() == Scr::Load); }
bool menu_slot_page() { return g_open && slot_page() && g_capture < 0; }

static void ask_delete(const View &v, int sel) {
  long long t;
  if (!slot_page() || sel < 0 || sel >= static_cast<int>(v.items.size()) || !slot_time(sel, t)) return;
  g_pending_slot = sel;
  push(Scr::ConfirmDelete);
  g_sel[static_cast<int>(Scr::ConfirmDelete)] = 1;  // default: NO
}

void menu_nav(Nav n) {
  if (!g_open || g_stack.empty()) return;
  if (menu_card_active()) {
    menu_card_dismiss();
    return;
  }
  if (g_capture >= 0) {  // the rebinding prompt: Back cancels, nothing else navigates
    if (n == Nav::Back) g_capture = -1;
    return;
  }
  const View v = build_view();
  int sel = current_sel(v);
  const int count = static_cast<int>(v.items.size());
  auto step = [&](int dir) {
    int s = sel;
    for (int guard = 0; guard <= count + 1; ++guard) {
      s += dir;
      if (s >= count) s = v.tabs ? -1 : 0;
      else if (s < -1 || (s < 0 && !v.tabs)) s = count - 1;
      if (s == -1 && v.tabs) return s;
      if (s >= 0 && v.items[static_cast<size_t>(s)].selectable) return s;
      if (count == 0) return -1;
    }
    return sel;
  };
  switch (n) {
    case Nav::Up: set_sel(v, step(-1)); break;
    case Nav::Down: set_sel(v, step(+1)); break;
    case Nav::Left:
    case Nav::Right:
      if (v.tabs && sel < 0) switch_tab(n == Nav::Right ? 1 : -1);
      else activate(v, sel, n == Nav::Right ? 1 : -1);
      break;
    case Nav::TabPrev:
    case Nav::TabNext:
      if (v.tabs) switch_tab(n == Nav::TabNext ? 1 : -1);
      break;
    case Nav::Select: activate(v, sel, 0); break;
    case Nav::Back: pop(); break;
    case Nav::Delete: ask_delete(v, sel); break;
  }
}

void menu_mouse_move(int wx, int wy) {
  int fx, fy;
  if (!g_open || g_stack.empty() || !window_to_frame(wx, wy, fx, fy)) return;
  const View v = build_view();
  const Layout L = make_layout(v, g_frame.w, g_frame.h, current_sel(v));
  if (v.tabs && in_rect({L.panel.x, L.tabs_y, L.panel.w, L.lh}, fx, fy)) {
    set_sel(v, -1);
    return;
  }
  for (int i = 0; i < L.count; ++i)
    if (in_rect(L.item_rects[static_cast<size_t>(i)], fx, fy) && v.items[static_cast<size_t>(L.first + i)].selectable) {
      set_sel(v, L.first + i);
      return;
    }
}

void menu_mouse_click(int wx, int wy, int button) {
  if (!g_open || g_stack.empty()) return;
  if (menu_card_active()) {
    menu_card_dismiss();
    g_swallow_up = true;  // the click that dismissed the card must not reach the game
    return;
  }
  if (g_capture >= 0) {
    if (button == SDL_BUTTON_RIGHT) g_capture = -1;
    return;
  }
  if (button == SDL_BUTTON_RIGHT) {
    int rx, ry;
    if (slot_page() && window_to_frame(wx, wy, rx, ry)) {  // right click on a filled slot row: delete it
      const View v = build_view();
      const Layout L = make_layout(v, g_frame.w, g_frame.h, current_sel(v));
      for (int i = 0; i < L.count; ++i)
        if (in_rect(L.item_rects[static_cast<size_t>(i)], rx, ry)) {
          set_sel(v, L.first + i);
          ask_delete(v, L.first + i);
          return;
        }
    }
    menu_nav(Nav::Back);
    g_swallow_up = !g_open;
    return;
  }
  if (button != SDL_BUTTON_LEFT) return;
  int fx, fy;
  if (!window_to_frame(wx, wy, fx, fy)) { vlog("click %d,%d outside\n", wx, wy); return; }
  vlog("click %d,%d -> frame %d,%d\n", wx, wy, fx, fy);
  const View v = build_view();
  const Layout L = make_layout(v, g_frame.w, g_frame.h, current_sel(v));
  if (v.tabs)
    for (int t = 0; t < TAB_COUNT; ++t)
      if (in_rect(L.tab_rects[t], fx, fy)) {
        g_tab = t;
        g_scroll = 0;
        return;
      }
  for (int i = 0; i < L.count; ++i)
    if (in_rect(L.item_rects[static_cast<size_t>(i)], fx, fy)) {
      set_sel(v, L.first + i);
      activate(v, L.first + i, 0);
      g_swallow_up = !g_open;  // the click that closed the menu must not reach the game
      return;
    }
}

void menu_wheel(int dy) {
  if (g_open && dy != 0) menu_nav(dy > 0 ? Nav::Up : Nav::Down);
}

void menu_draw(Frame &f) {
  if (g_stack.empty()) return;
  const View v = build_view();
  const int sel = current_sel(v);
  const Layout L = make_layout(v, f.w, f.h, sel);
  const int s = L.s;
  if (verbose() && (g_stack.back() == Scr::Controls || g_stack.back() == Scr::Card || g_stack.back() == Scr::Rebind ||
                    g_stack.back() == Scr::Help)) {
    std::string sig = v.title;
    for (const auto &b : v.body) sig += "\n" + b;  // log the card text whenever it changes (tests read it)
    for (const auto &it : v.items) sig += "\n" + it.label + "=" + it.value;
    static std::string last;
    if (sig != last) {
      last = sig;
      vlog("card: %s\n", glyph_utf8(sig).c_str());
    }
  }
  if (verbose() && slot_page()) {
    static int last_latest = -2;
    const int l = latest_slot();
    if (l != last_latest) { last_latest = l; vlog("latest slot: %d\n", l); }
  }
  const uint32_t kText = 0xD6D6DE, kDim = 0x6E6E7A, kGold = 0xFFD66B, kValue = 0x9CCBFF, kBar = 0x2F3F78;

  fill_rect(f, 0, 0, f.w, f.h, 0x000000, 0.62f);
  fill_rect(f, L.panel.x, L.panel.y, L.panel.w, L.panel.h, 0x0B0D1A, 0.94f);
  const uint32_t border = 0x6C7AB4;
  fill_rect(f, L.panel.x, L.panel.y, L.panel.w, s, border, 1);
  fill_rect(f, L.panel.x, L.panel.y + L.panel.h - s, L.panel.w, s, border, 1);
  fill_rect(f, L.panel.x, L.panel.y, s, L.panel.h, border, 1);
  fill_rect(f, L.panel.x + L.panel.w - s, L.panel.y, s, L.panel.h, border, 1);

  const int ix = L.panel.x + L.pad, inner_w = L.cols * L.cw;
  auto fit = [&](std::string t, int max_chars) {
    t = to_upper(t);
    if (static_cast<int>(t.size()) > max_chars) t = t.substr(0, static_cast<size_t>(std::max(0, max_chars)));
    return t;
  };
  draw_text(f, ix, L.title_y + L.ty, fit(v.title, L.cols), kGold, 1, s, true);
  fill_rect(f, ix, L.title_y + L.lh - s, inner_w, s, 0x3A4270, 1);

  if (v.tabs) {
    if (sel < 0) fill_rect(f, ix - 2 * s, L.tabs_y, inner_w + 4 * s, L.lh, kBar, 1);
    for (int t = 0; t < TAB_COUNT; ++t) {
      const bool active = t == g_tab;
      draw_text(f, L.tab_rects[t].x + 2 * s, L.tabs_y + L.ty, kTabNames[t], active ? kGold : kDim, 1, s, false);
      if (active) fill_rect(f, L.tab_rects[t].x + 2 * s, L.tabs_y + L.lh - s, text_width(kTabNames[t], s), s, kGold, 1);
    }
  }
  for (size_t i = 0; i < v.body.size(); ++i)
    draw_text(f, ix, L.body_y + static_cast<int>(i) * L.lh + L.ty, fit(v.body[i], L.cols), kText, 1, s, false);

  for (int r = 0; r < L.count; ++r) {
    const int idx = L.first + r;
    const Item &it = v.items[static_cast<size_t>(idx)];
    const Rect &rc = L.item_rects[static_cast<size_t>(r)];
    const bool selected = idx == sel && it.selectable;
    if (selected) fill_rect(f, rc.x - 2 * s, rc.y, rc.w + 4 * s, rc.h, kBar, 1);
    uint32_t col = !it.enabled ? kDim : selected ? 0xFFFFFF : kText;
    if (!it.selectable) col = 0xB4B4C0;
    std::string val = it.value;
    if (selected && it.adjustable && it.enabled) val = "< " + val + " >";
    val = to_upper(val);
    if (selected) draw_text(f, rc.x, rc.y + L.ty, ">", kGold, 1, s, false);
    const int label_max = L.cols - 2 - static_cast<int>(val.size()) - (val.empty() ? 0 : 1);
    draw_text(f, rc.x + 2 * L.cw, rc.y + L.ty, fit(it.label, label_max), col, 1, s, false);
    if (!val.empty())
      draw_text(f, rc.x + rc.w - text_width(val, s), rc.y + L.ty, val, it.enabled ? kValue : kDim, 1, s, false);
  }
  // Scroll indicators.
  if (L.first > 0) draw_text(f, ix + inner_w - L.cw, L.items_y - L.lh + L.ty, "+", kDim, 1, s, false);
  if (L.first + L.count < static_cast<int>(v.items.size()))
    draw_text(f, ix + inner_w - L.cw, L.hint_y - 3 * s - L.lh + L.ty, "+", kDim, 1, s, false);

  if (L.thumb.w > 0 && sel >= 0) {
    const Rect &tb = L.thumb;
    fill_rect(f, tb.x, tb.y, tb.w, tb.h, 0x3A4270, 1);
    fill_rect(f, tb.x + 1, tb.y + 1, tb.w - 2, tb.h - 2, 0x000000, 1);
    if (const Thumb *t = thumb_for(sel)) {
      for (int y = 0; y < tb.h - 2; ++y)
        for (int x = 0; x < tb.w - 2; ++x) {
          const size_t o = (static_cast<size_t>(y * t->h / (tb.h - 2)) * t->w + static_cast<size_t>(x * t->w / (tb.w - 2))) * 3;
          plot(f, tb.x + 1 + x, tb.y + 1 + y, (t->rgb[o] << 16) | (t->rgb[o + 1] << 8) | t->rgb[o + 2], 1);
        }
    } else {
      const std::string msg = "NO PREVIEW";
      draw_text(f, tb.x + (tb.w - text_width(msg, 1)) / 2, tb.y + tb.h / 2 - 3, msg, kDim, 1, 1, false);
    }
  }
  const bool pad = g_device == Device::Pad;
  std::string hint = pad ? ((L.cols >= 36 ? "D-PAD MOVE  " : "") + pad_hint_keys())
                         : (L.cols >= 36 ? "UP/DOWN MOVE  ENTER SELECT  ESC BACK" : "ENTER SELECT  ESC BACK");
  if (slot_page()) {
    const bool wide = L.cols >= 36;
    hint = pad ? (pad_button_text(SDL_CONTROLLER_BUTTON_A) + " SELECT  " + pad_button_text(SDL_CONTROLLER_BUTTON_X) + " DELETE  " +
                  pad_button_text(SDL_CONTROLLER_BUTTON_B) + " BACK")
               : (wide ? "ENTER SELECT  DEL DELETE  ESC BACK" : "ENTER OK  DEL DELETE  ESC");
    if (g_device == Device::Mouse && wide) hint = "CLICK SELECT  RIGHT CLICK DELETE";
  }
  if (!v.hint.empty()) hint = v.hint;
  draw_text(f, ix, L.hint_y + L.ty, fit(hint, L.cols), kDim, 1, s, false);
}

}  // namespace detail
}  // namespace retro_shell
