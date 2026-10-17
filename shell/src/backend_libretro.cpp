// Backend for a statically linked libretro core.
#include "retro_shell/backend_libretro.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>

#include "libretro.h"
#include "retro_shell/log.h"

// Older libretro.h copies lack this one (value 64 | RETRO_ENVIRONMENT_EXPERIMENTAL).
#ifndef RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE
#define RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE (64 | RETRO_ENVIRONMENT_EXPERIMENTAL)
#endif

namespace retro_shell {
namespace {

class LibretroBackend;
LibretroBackend *g_self = nullptr;  // libretro callbacks carry no user pointer

class LibretroBackend final : public Backend {
 public:
  explicit LibretroBackend(const Game &game) : game_(game) { g_self = this; }
  ~LibretroBackend() override { shutdown(); if (g_self == this) g_self = nullptr; }

  bool init(Host &host) override {
    host_ = &host;
    // The core declares its options with SET_VARIABLES ("Description; default|other|...").
    // The store starts with the game's overrides; GET_VARIABLE answers from it.
    overrides_.clear();
    if (game_.core_options)
      for (const auto &kv : game_.core_options()) overrides_[kv.first] = kv.second;
    vars_ = overrides_;
    retro_set_environment(&LibretroBackend::environment);
    retro_init();
    inited_ = true;
    retro_set_video_refresh(&LibretroBackend::video_refresh);
    retro_set_audio_sample(&LibretroBackend::audio_sample);
    retro_set_audio_sample_batch(&LibretroBackend::audio_batch);
    retro_set_input_poll(&LibretroBackend::input_poll);
    retro_set_input_state(&LibretroBackend::input_state);
    return true;
  }

  bool load(const std::string &content_path) override {
    retro_game_info info{};
    info.path = content_path.c_str();  // the core opens the content itself (need_fullpath)
    if (!retro_load_game(&info)) return false;
    loaded_ = true;
    retro_get_system_av_info(&av_);
    return true;
  }

  bool wants_memory() override {
    retro_system_info si{};
    retro_get_system_info(&si);
    return !si.need_fullpath;  // the core takes data/size (snes9x); VICE needs a path
  }
  bool load_memory(const void *data, size_t size, const std::string &name_hint) override {
    retro_game_info info{};
    info.path = name_hint.c_str();  // hint only (extension / name); the bytes come from memory
    info.data = data;
    info.size = size;
    if (!retro_load_game(&info)) return false;
    loaded_ = true;
    retro_get_system_av_info(&av_);
    return true;
  }

  AvInfo av_info() const override {
    AvInfo a;
    a.width = av_.geometry.base_width;
    a.height = av_.geometry.base_height;
    a.aspect = av_.geometry.aspect_ratio;
    a.fps = av_.timing.fps;
    a.sample_rate = av_.timing.sample_rate;
    if (loaded_) a.region = retro_get_region() == RETRO_REGION_PAL ? "PAL" : "NTSC";
    return a;
  }

  std::string identity() const override {
    retro_system_info si{};
    retro_get_system_info(&si);
    return std::string(si.library_name ? si.library_name : "") + " " + (si.library_version ? si.library_version : "");
  }
  bool reset() override {
    if (!loaded_) return false;
    retro_reset();
    return true;
  }
  void run_frame() override { retro_run(); }
  size_t serialize_size() override { return retro_serialize_size(); }
  bool serialize(void *dst, size_t size) override { return retro_serialize(dst, size); }
  bool unserialize(const void *src, size_t size) override { return retro_unserialize(src, size); }
  void set_port_device(unsigned port, PortDevice d) override {
    if (port >= kMaxPorts) return;
    retro_set_controller_port_device(port, d == PortDevice::Mouse ? RETRO_DEVICE_MOUSE
                                           : d == PortDevice::Joypad ? RETRO_DEVICE_JOYPAD : RETRO_DEVICE_NONE);
  }
  void set_pad(unsigned port, const PadState &st) override { if (port < kMaxPorts) pad_[port] = st; }
  void set_mouse(unsigned port, const MouseState &m) override {
    if (port >= kMaxPorts) return;
    mouse_dx_[port] += m.dx;  // consumed when the core reads it
    mouse_dy_[port] += m.dy;
    mouse_btn_[port] = m.buttons;
  }
  bool read_memory(size_t addr, void *dst, size_t n) override {
    const unsigned char *p = loaded_ ? static_cast<const unsigned char *>(retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM)) : nullptr;
    const size_t size = loaded_ ? retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM) : 0;
    if (!p || addr > size || n > size - addr) return false;  // overflow-safe form of addr + n > size
    memcpy(dst, p + addr, n);
    return true;
  }
  size_t save_ram_size() override { return loaded_ ? retro_get_memory_size(RETRO_MEMORY_SAVE_RAM) : 0; }
  bool get_save_ram(void *dst, size_t n) override {
    void *p = loaded_ ? retro_get_memory_data(RETRO_MEMORY_SAVE_RAM) : nullptr;
    if (!p || retro_get_memory_size(RETRO_MEMORY_SAVE_RAM) != n) return false;
    memcpy(dst, p, n);
    return true;
  }
  bool set_save_ram(const void *src, size_t n) override {
    void *p = loaded_ ? retro_get_memory_data(RETRO_MEMORY_SAVE_RAM) : nullptr;
    if (!p || retro_get_memory_size(RETRO_MEMORY_SAVE_RAM) != n) return false;
    memcpy(p, src, n);
    return true;
  }
  void key_event(unsigned code, bool down) override {
    if (kbd_cb_ && code) kbd_cb_(down, code, 0, 0);
  }

  void set_option(const std::string &key, const std::string &value) override {
    overrides_[key] = value;
    vars_[key] = value;
    var_update_ = true;  // reported once through GET_VARIABLE_UPDATE
  }

  void shutdown() override {
    if (loaded_) retro_unload_game();
    if (inited_) retro_deinit();
    loaded_ = inited_ = false;
  }

 private:
  // ------------------------------------------------------------ callbacks
  static void video_refresh(const void *data, unsigned w, unsigned h, size_t pitch) {
    FrameDesc f;
    f.data = data; f.width = w; f.height = h; f.pitch = pitch;
    f.format = g_self->pixfmt_;
    g_self->host_->video(f);
  }
  static size_t audio_batch(const int16_t *data, size_t frames) { return g_self->host_->audio(data, frames); }
  static void audio_sample(int16_t l, int16_t r) {
    int16_t s[2] = {l, r};
    g_self->host_->audio(s, 1);
  }
  static void input_poll() {}
  static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
    // RetroPad, analog sticks and mouse per port. Keyboard state is deliberately not reported
    // (keys go through the keyboard callback instead), so the core's hotkeys never fire.
    if (port >= kMaxPorts) return 0;
    LibretroBackend &s = *g_self;
    if (device == RETRO_DEVICE_JOYPAD && index == 0) {
      const uint16_t m = s.pad_[port].buttons;
      if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return static_cast<int16_t>(m);
      return id < 16 && ((m >> id) & 1) ? 1 : 0;
    }
    if (device == RETRO_DEVICE_ANALOG && index <= RETRO_DEVICE_INDEX_ANALOG_RIGHT) {
      const PadState &p = s.pad_[port];
      if (id == RETRO_DEVICE_ID_ANALOG_X) return index == RETRO_DEVICE_INDEX_ANALOG_LEFT ? p.lx : p.rx;
      if (id == RETRO_DEVICE_ID_ANALOG_Y) return index == RETRO_DEVICE_INDEX_ANALOG_LEFT ? p.ly : p.ry;
      return 0;
    }
    if (device == RETRO_DEVICE_MOUSE && index == 0) {
      int16_t v = 0;
      switch (id) {
        case RETRO_DEVICE_ID_MOUSE_X: v = static_cast<int16_t>(s.mouse_dx_[port]); s.mouse_dx_[port] = 0; break;
        case RETRO_DEVICE_ID_MOUSE_Y: v = static_cast<int16_t>(s.mouse_dy_[port]); s.mouse_dy_[port] = 0; break;
        case RETRO_DEVICE_ID_MOUSE_LEFT: v = s.mouse_btn_[port] & 1; break;
        case RETRO_DEVICE_ID_MOUSE_RIGHT: v = (s.mouse_btn_[port] >> 1) & 1; break;
        case RETRO_DEVICE_ID_MOUSE_MIDDLE: v = (s.mouse_btn_[port] >> 2) & 1; break;
        default: break;
      }
      return v;
    }
    return 0;
  }
  static void core_log(retro_log_level, const char *fmt, ...) {
    if (!verbose()) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
  }

  void store_variables(const retro_variable *v) {
    for (; v && v->key; ++v) {
      if (!v->value) continue;
      const char *semi = strchr(v->value, ';');
      if (!semi) continue;
      std::string rest = semi + 1;
      size_t a = rest.find_first_not_of(' ');
      if (a == std::string::npos) continue;
      rest = rest.substr(a);
      vars_[v->key] = rest.substr(0, rest.find('|'));  // first listed value = default
    }
    for (const auto &o : overrides_) vars_[o.first] = o.second;
  }

  static bool environment(unsigned cmd, void *data) {
    LibretroBackend &s = *g_self;
    switch (cmd) {
      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        auto fmt = *static_cast<retro_pixel_format *>(data);
        if (fmt == RETRO_PIXEL_FORMAT_RGB565) s.pixfmt_ = PixelFormat::RGB565;
        else if (fmt == RETRO_PIXEL_FORMAT_XRGB8888) s.pixfmt_ = PixelFormat::XRGB8888;
        else if (fmt == RETRO_PIXEL_FORMAT_0RGB1555) s.pixfmt_ = PixelFormat::RGB1555;
        else return false;
        return true;
      }
      case RETRO_ENVIRONMENT_GET_CAN_DUPE: *static_cast<bool *>(data) = true; return true;
      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        // The core may write files under <system>/. Keep them out of the saves directory root.
        s.sys_dir_ = s.host_->save_dir() + "/run/sys";
        *static_cast<const char **>(data) = s.sys_dir_.c_str();
        return true;
      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
        *static_cast<const char **>(data) = s.host_->save_dir().c_str();
        return true;
      case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        static_cast<retro_log_callback *>(data)->log = core_log;
        return true;
      case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: return false;  // core falls back to SET_VARIABLES
      case RETRO_ENVIRONMENT_SET_VARIABLES: s.store_variables(static_cast<const retro_variable *>(data)); return true;
      case RETRO_ENVIRONMENT_GET_VARIABLE: {
        auto *var = static_cast<retro_variable *>(data);
        auto it = var->key ? s.vars_.find(var->key) : s.vars_.end();
        if (it == s.vars_.end()) { var->value = nullptr; return false; }
        s.var_value_buf_ = it->second;
        var->value = s.var_value_buf_.c_str();
        return true;
      }
      case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *static_cast<bool *>(data) = s.var_update_;
        s.var_update_ = false;
        return true;
      case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
        s.kbd_cb_ = static_cast<const retro_keyboard_callback *>(data)->callback;
        return true;
      case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: return true;
      case RETRO_ENVIRONMENT_SET_GEOMETRY: s.av_.geometry = *static_cast<const retro_game_geometry *>(data); return true;
      case RETRO_ENVIRONMENT_SHUTDOWN: s.host_->request_shutdown(); return true;
      case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
      case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
      case RETRO_ENVIRONMENT_SET_MESSAGE:
      case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
      case RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE:
        return true;  // accepted and ignored: no UI to show them in
      case RETRO_ENVIRONMENT_GET_FASTFORWARDING: *static_cast<bool *>(data) = false; return true;
      default: return false;
    }
  }

  const Game &game_;
  Host *host_ = nullptr;
  bool inited_ = false, loaded_ = false, var_update_ = false;
  retro_system_av_info av_{};
  PixelFormat pixfmt_ = PixelFormat::RGB565;
  retro_keyboard_event_t kbd_cb_ = nullptr;
  PadState pad_[kMaxPorts];
  int mouse_dx_[kMaxPorts] = {}, mouse_dy_[kMaxPorts] = {};
  uint8_t mouse_btn_[kMaxPorts] = {};
  std::map<std::string, std::string> vars_, overrides_;
  std::string sys_dir_;
  std::string var_value_buf_;  // retro_variable.value must outlive the call
};

}  // namespace

std::unique_ptr<Backend> make_libretro_backend(const Game &game) {
  return std::unique_ptr<Backend>(new LibretroBackend(game));
}

}  // namespace retro_shell
