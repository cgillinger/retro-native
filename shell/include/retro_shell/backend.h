// retro_shell - emulation backend interface.
// The shell never talks to an emulator core directly; it drives a Backend. A statically
// linked libretro core is one implementation (backend_libretro.h), a native port another.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace retro_shell {

enum class PixelFormat { RGB565, XRGB8888, RGB1555 };

// Abstract pad buttons. The numeric values equal the libretro RetroPad ids, so a pad
// state is a bit mask with bit N = button N.
enum PadButton : unsigned {
  PAD_B = 0, PAD_Y, PAD_SELECT, PAD_START, PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT,
  PAD_A, PAD_X, PAD_L, PAD_R, PAD_L2, PAD_R2, PAD_L3, PAD_R3
};

// What is plugged into a controller port. The shell routes keyboard/gamepads to Joypad ports
// and the relative mouse to the first Mouse port.
// JoypadOrMouse: a shared port the shell hot-switches between the two on real input (keyboard /
// pad activity -> Joypad, mouse motion / click -> Mouse; at most one switch per 250 ms) by
// calling Backend::set_port_device again. The port starts as Joypad.
enum class PortDevice { None, Joypad, Mouse, JoypadOrMouse };
constexpr unsigned kMaxPorts = 4;

// Full state of one pad: all 16 RetroPad buttons (bit N = PadButton N) and, optionally, the
// analog sticks (-32768..32767, y down). Backends that do not use analog input ignore it.
struct PadState {
  uint16_t buttons = 0;
  int16_t lx = 0, ly = 0, rx = 0, ry = 0;
};

// Mouse state for one frame: motion since the previous call in the game's logical pixels
// (the shell has applied picture scale and the sensitivity setting), button bits 0 = left,
// 1 = right, 2 = middle.
struct MouseState {
  int dx = 0, dy = 0;
  uint8_t buttons = 0;
};

struct AvInfo {
  unsigned width = 0, height = 0;  // base picture size
  double aspect = 0.0;             // display aspect ratio (0 = unknown)
  double fps = 0.0;
  double sample_rate = 0.0;
  std::string region;              // informational ("PAL", "NTSC", ...), may be empty
};

struct FrameDesc {
  const void *data = nullptr;  // null = duplicate frame (keep the previous picture)
  unsigned width = 0, height = 0;
  size_t pitch = 0;  // bytes per source row
  PixelFormat format = PixelFormat::RGB565;
};

// What the shell offers to a backend (all calls happen on the main thread, from inside
// Backend::run_frame() or the init/load calls).
class Host {
 public:
  virtual ~Host() = default;
  virtual void video(const FrameDesc &frame) = 0;
  virtual size_t audio(const int16_t *interleaved_stereo, size_t frames) = 0;
  virtual const std::string &save_dir() const = 0;  // may change until load() has returned
  virtual void request_shutdown() = 0;
};

class Backend {
 public:
  virtual ~Backend() = default;
  // Initialise the engine. Called after the shell has staged the game content.
  virtual bool init(Host &host) = 0;
  // Load the content produced by Game::stage_content.
  virtual bool load(const std::string &content_path) = 0;
  // Engines that read the content from memory (libretro need_fullpath = false) return true; the
  // shell then calls load_memory with the game data instead of staging files. Default: false.
  virtual bool wants_memory() { return false; }
  virtual bool load_memory(const void *data, size_t size, const std::string &name_hint) {
    (void)data; (void)size; (void)name_hint;
    return false;
  }
  virtual AvInfo av_info() const = 0;
  // Name and version of the engine/core. Part of the boot snapshot header: a snapshot is only
  // used by the engine build that wrote it. Empty = unknown (snapshots are then never trusted).
  virtual std::string identity() const { return ""; }
  // Hard reset of the loaded content (used when a snapshot failed to load half-way). Default: none.
  virtual bool reset() { return false; }
  virtual void run_frame() = 0;
  virtual size_t serialize_size() = 0;
  virtual bool serialize(void *dst, size_t size) = 0;
  virtual bool unserialize(const void *src, size_t size) = 0;
  // Device plugged into `port` (< kMaxPorts); called once after load() for every port.
  virtual void set_port_device(unsigned port, PortDevice device) { (void)port; (void)device; }
  // Pad state for `port`; applied to the following run_frame() calls.
  virtual void set_pad(unsigned port, const PadState &state) = 0;
  // Mouse motion/buttons for `port`; the motion is consumed by the next run_frame().
  virtual void set_mouse(unsigned port, const MouseState &state) { (void)port; (void)state; }
  // The game's own battery/save RAM. The shell loads it after load() and stores it in the user
  // directory (<ini_name>.sram) when it changes and at exit. Size 0 = none.
  virtual size_t save_ram_size() { return 0; }
  virtual bool get_save_ram(void *dst, size_t size) { (void)dst; (void)size; return false; }
  virtual bool set_save_ram(const void *src, size_t size) { (void)src; (void)size; return false; }
  // Key passthrough. `code` is a backend-defined key code (libretro: RETROK_*), as given
  // in the Game key map.
  virtual void key_event(unsigned code, bool down) = 0;
  // Changes an engine option while running (libretro: a core variable). Default: ignored.
  virtual void set_option(const std::string &key, const std::string &value) { (void)key; (void)value; }
  // The machine's main RAM (optional; for game hooks that need to read a game variable).
  virtual bool read_memory(size_t addr, void *dst, size_t n) { (void)addr; (void)dst; (void)n; return false; }
  virtual void shutdown() = 0;
};

}  // namespace retro_shell
