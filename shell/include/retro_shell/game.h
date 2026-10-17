// Everything game-specific the shell needs, supplied by the game as one Game value.
#pragma once

#include <SDL_gamecontroller.h>
#include <SDL_keycode.h>
#include <SDL_scancode.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "retro_shell/backend.h"

namespace retro_shell {

// ---- boot hold -------------------------------------------------------------------------
// Generic support for a hidden start-up phase (machine boots and loads before the game's
// first real picture). While the hold is active the shell: shows black instead of the
// core's picture, mutes audio, drops key presses and gamepad Start/Back, refuses to save,
// and runs flat out (stopping the batch the moment the hold ends). The game decides when
// the hold is over by inspecting frames.
struct FrameView {
  const uint8_t *data = nullptr;  // tightly packed rows
  int width = 0, height = 0, bytes_per_pixel = 2;
  PixelFormat format = PixelFormat::RGB565;
};
uint32_t pixel_rgb(const FrameView &f, int x, int y);  // 0xRRGGBB

class BootControl {
 public:
  virtual ~BootControl() = default;
  // Key event straight to the backend; unlike user input it is not dropped during the hold.
  virtual void key(unsigned code, bool down) = 0;
  virtual int frame() const = 0;  // number of frames seen while held (1 on the first)
};

// What a game's function hook may do besides the plain key press (ControlFunc::on_press).
class GameActions {
 public:
  virtual ~GameActions() = default;
  virtual void key(unsigned code, bool down) = 0;           // straight to the backend
  virtual void after(int frames, std::function<void()> fn) = 0;  // before the n-th next emulated frame
  virtual bool read_memory(size_t addr, void *dst, size_t n) = 0;  // the machine's RAM, if the backend has it
  virtual FrameView frame() const = 0;                      // the picture now on screen
};

enum class BootRelease { Hook, Timeout, StateLoaded };

struct BootHold {
  bool enabled = false;
  int max_frames = 4000;  // safety net: released with BootRelease::Timeout
  // Called for every video frame while held, before it would be blanked. Return true to
  // release the hold (the frame is then shown as is).
  std::function<bool(const FrameView &, BootControl &)> on_frame;
  // Called once when the hold ends for a reason other than the hook (timeout, loaded
  // state). Optional.
  std::function<void(BootRelease, BootControl &)> on_release;
  // --write-boot-state: frames to keep running after the hook released the hold before the
  // snapshot is taken (lets injected key releases take effect inside the core).
  int state_settle_frames = 0;
};

// ---- instant start (STANDARD section 2) --------------------------------------------------
// The build runs the game once with `--write-boot-state <file>` (hidden boot to the game's
// first real picture, then Backend::serialize into a small container: magic, engine identity,
// SHA-1 of the game data, size, checksum). The result is embedded into the binary; at start
// the shell loads it instead of booting, when identity, data hash and size all match, and
// falls back to the normal hidden boot otherwise. The file holds game memory: never commit it.
struct BootSnapshot {
  // Contents of the embedded snapshot file; false/empty when the build has none.
  std::function<bool(std::vector<uint8_t> &)> embedded;
};

// Window icon: tightly packed RGBA8 pixels (null = none).
struct WindowIcon { const uint8_t *rgba = nullptr; int w = 0, h = 0; };

// Linux desktop shortcut for the first-start fallback (see desktop.h). Empty id = none.
struct DesktopEntry { std::string id, name, icon, categories, comment; };

// ---- other hooks ----------------------------------------------------------------------
struct KeyMapEntry { SDL_Scancode scancode; unsigned code; };  // code: backend key code

// One rebindable game function (STANDARD section 10: the controls card lists these, and
// "Change..." rebinds them). A function drives either an abstract pad button on port 0
// (`joy` >= 0) or sends a backend key code (`code` != 0).
struct ControlFunc {
  std::string id;     // ini key under [keys] / [pad], lower case, e.g. "fire"
  std::string label;  // card text, e.g. "FIRE"
  SDL_Scancode key = SDL_SCANCODE_UNKNOWN;  // default keyboard key
  // Extra default keys, active only while `key` is unchanged and not claimed by another function.
  std::vector<SDL_Scancode> alt_keys;
  // Default gamepad button. INVALID = not rebindable on the pad (then `pad_text` is shown,
  // e.g. "D-PAD"; the shell always maps the D-pad and left stick to the four directions).
  SDL_GameControllerButton pad = SDL_CONTROLLER_BUTTON_INVALID;
  std::vector<SDL_GameControllerButton> alt_pads;  // extra default buttons, same rule as alt_keys
  std::string pad_text;
  std::string mouse_text;  // card text for mouse-first games (not rebindable)
  unsigned devices = 7;    // DeviceMask: the card lists the function only for these devices
  int joy = -1;            // PadButton driven on pad port 0, or -1 (also the --press action name = id)
  unsigned code = 0;       // backend key code sent on press/release, or 0
  // Optional: called for every press (down) and release instead of sending `code`. Return true
  // when handled (the shell then sends nothing); false = send `code` as usual. Not called
  // during the boot hold. Lets a game turn one press into a sequence (GameActions::after).
  std::function<bool(bool down, GameActions &)> on_press;
};

// A selectable picture aspect ratio (Video page). The first entry is the platform's correct
// one and the default. ratio 0 = square pixels (picture width / height).
struct AspectChoice { std::string id, label; double ratio = 0.0; };

// Optional crop of the game's border (Video page "GAME BORDER"). x/y/w/h in frame pixels;
// `aspect` is the display aspect ratio of the cropped picture. w == 0: the game has none.
struct BorderCrop { int x = 0, y = 0, w = 0, h = 0; double aspect = 0.0; };

// Text-like key forwarded by keycode (keyboard layout), not scancode: STANDARD section 3.
struct TextKey { SDL_Keycode keycode; unsigned code; };

struct CliFlag {
  const char *name;     // e.g. "--joyport"
  bool takes_value;
  std::function<bool(const char *value)> handler;  // false = invalid command line
};

using CoreOption = std::pair<std::string, std::string>;

// Original signal type; decides the default video filter (Auto).
enum class Signal { TV, VGA };

// A game-specific option. The shell stores it in the ini under [section] key, shows it in
// the settings menu (section "video" and "audio" extend those pages, everything else lands
// on Advanced) and passes the value on: to the core option `core_key` (libretro; applied
// live) and to Game::on_option.
struct OptionChoice { std::string value, label; };
struct GameOption {
  std::string key;                   // ini key, e.g. "sid_model"
  std::string section = "advanced";  // ini section
  std::string label;                 // menu text
  std::vector<OptionChoice> choices;
  std::string def;                   // default value (one of the choices)
  std::string core_key;              // libretro core variable to set, "" = none
};

// One free-text row on the controls card (game-specific, not rebindable). `pad` replaces
// `keyboard` while a gamepad is the active device (empty = same text).
// `mouse` replaces `keyboard` while the mouse is the active device (empty = same text).
// `devices` (DeviceMask bits) says where the row is shown at all.
enum DeviceMask : unsigned { DEV_KEYBOARD = 1, DEV_PAD = 2, DEV_MOUSE = 4, DEV_ALL = 7 };
struct ControlHint {
  std::string action, keyboard, pad, mouse;
  unsigned devices = DEV_ALL;
  // Constructor (not an aggregate) so existing {action, keyboard, pad} initialisers keep
  // compiling under -Wmissing-field-initializers.
  ControlHint(std::string a, std::string k, std::string p = "", std::string m = "", unsigned d = DEV_ALL)
      : action(std::move(a)), keyboard(std::move(k)), pad(std::move(p)), mouse(std::move(m)), devices(d) {}
};

struct Game {
  const char *title = "";          // window title, message box title
  const char *user_dir_name = "";  // SDL_GetPrefPath application name
  const char *usage = "";          // text shown (after "Invalid command line.") on bad flags
  const char *ini_name = "";       // settings file <user dir>/<ini_name>.ini
  Signal signal = Signal::TV;      // TV: filter Auto by default; VGA: Sharp

  // Display aspect ratio of the picture; 0 = use the backend's.
  double display_aspect = 0.0;
  bool uses_mouse = false;  // false: the pointer stays hidden
  // true: the mouse drives the first PortDevice::Mouse port (relative, only while the window
  // has focus and no menu is open); Controls gets MOUSE / SENSITIVITY rows.
  // Selectable aspect ratios (empty: only display_aspect / the backend's).
  std::vector<AspectChoice> aspects;
  BorderCrop border_crop;
  // Device on each controller port (kMaxPorts at most). Pad 1 + keyboard drive port 0 through
  // `functions`; pads 2.. drive ports 1.. with the full positional mapping.
  std::vector<PortDevice> port_devices = {PortDevice::Joypad, PortDevice::Joypad};
  // Extra positional aliases on ports >= 1 (e.g. B also fires), on top of A->B, B->A, X->Y ...
  std::vector<std::pair<SDL_GameControllerButton, PadButton>> extra_pad_map;

  // Rebindable functions (keyboard + gamepad). Gamepads work without any configuration.
  std::vector<ControlFunc> functions;
  // Fixed keyboard -> backend key passthrough, not shown as functions (e.g. digits for menu
  // input). A key bound to a function wins over an entry here. The shell's reserved keys
  // (Esc, F1, F5, F9, F11) are never forwarded, whatever the map says.
  std::vector<KeyMapEntry> key_map;
  // Keys that follow the keyboard layout (J/N prompts, names): matched on the keycode the
  // layout produces. A function or key_map entry on the same physical key wins.
  std::vector<TextKey> text_keys;
  // Visible area for the CRT filter (0 = the backend frame size). Set when the frame is
  // 2x the logical size in some axis (hi-res / interlace).
  int logical_width = 0, logical_height = 0;
  std::vector<unsigned> release_on_pad_loss;     // keys released when a gamepad is unplugged
  std::function<unsigned(const std::string &)> key_for_name;  // --press key actions

  std::vector<ControlHint> control_notes;  // extra card rows (the shell adds its own hotkeys)
  // Label of the collapsed direction row on the keyboard and pad cards (the four directions on
  // their default controls), e.g. "WALK / CLIMB" when up/down do not simply move.
  const char *move_label = "MOVE";
  std::vector<GameOption> options;
  std::function<void(const std::string &key, const std::string &value)> on_option;  // optional

  std::vector<CliFlag> cli_flags;  // extra command line flags

  // libretro: core option overrides (key/value). Evaluated after the command line is parsed.
  std::function<std::vector<CoreOption>()> core_options;

  // Content: load_data reads/embeds the raw game data (called before SDL is initialised;
  // report problems with show_error). stage_content turns it into what the backend loads
  // (e.g. files in save_dir()/run) and returns the path to hand to Backend::load, or "".
  // test_run: the shell runs with --frames (never touch the player's files).
  // memory_content: non-empty = the content name hint; when the backend reads from memory
  // (Backend::wants_memory) the shell loads the data directly and stage_content is not called,
  // so nothing is written to disk. Path loading stays for engines that need a file.
  std::function<bool(std::vector<uint8_t> &)> load_data;
  std::function<std::string(const std::vector<uint8_t> &, bool test_run)> stage_content;
  std::string memory_content;
  // Environment variable that redirects the user directory (e.g. "KAW_USER_DIR"); empty =
  // <INI_NAME in capitals>_USER_DIR. Applied before the user directory is set up; an override that
  // cannot be written is an error, never a silent fallback to the real directory.
  std::string user_dir_env;

  BootHold boot;
  BootSnapshot boot_snapshot;
  WindowIcon icon;
  DesktopEntry desktop;
};

}  // namespace retro_shell
