// User directory, file helpers and atomic save/load of the quick-save slot.
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "internal.h"

namespace retro_shell {
namespace {

std::string g_save_dir = "saves";

#ifdef _WIN32
std::wstring to_wide(const std::string &u8) {
  int n = MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), -1, nullptr, 0);
  std::wstring w(static_cast<size_t>(n > 0 ? n : 1), L'\0');
  if (n > 0) MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), -1, &w[0], n);
  return w;
}
#endif

}  // namespace

const std::string &save_dir() { return g_save_dir; }
void set_save_dir(const std::string &dir) { g_save_dir = dir; }

void fs_mkdir(const std::string &p) {
#ifdef _WIN32
  CreateDirectoryW(to_wide(p).c_str(), nullptr);
#else
  mkdir(p.c_str(), 0755);
#endif
}
void fs_remove(const std::string &p) {
#ifdef _WIN32
  DeleteFileW(to_wide(p).c_str());
#else
  unlink(p.c_str());
#endif
}
bool fs_replace(const std::string &from, const std::string &to) {
#ifdef _WIN32
  return MoveFileExW(to_wide(from).c_str(), to_wide(to).c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
  return rename(from.c_str(), to.c_str()) == 0;
#endif
}

// SDL_RWFromFile takes UTF-8 paths on every platform.
bool read_file(const std::string &path, std::vector<uint8_t> &out) {
  SDL_RWops *f = SDL_RWFromFile(path.c_str(), "rb");
  if (!f) return false;
  Sint64 n = SDL_RWsize(f);
  bool ok = false;
  if (n > 0) {
    out.resize(static_cast<size_t>(n));
    ok = SDL_RWread(f, out.data(), 1, out.size()) == out.size();
  }
  SDL_RWclose(f);
  return ok;
}

bool write_file(const std::string &path, const void *data, size_t n) {
  SDL_RWops *f = SDL_RWFromFile(path.c_str(), "wb");
  if (!f) return false;
  bool ok = SDL_RWwrite(f, data, 1, n) == n;
  if (SDL_RWclose(f) != 0) ok = false;
  return ok;
}

bool write_file_atomic(const std::string &path, const void *data, size_t n) {
  const std::string tmp = path + ".tmp";
  if (!write_file(tmp, data, n)) { fs_remove(tmp); return false; }
  if (!fs_replace(tmp, path)) { fs_remove(tmp); return false; }
  return true;
}

bool stage_file(const std::string &path, const void *data, size_t n) {
  std::vector<uint8_t> cur;
  const bool same = read_file(path, cur) && cur.size() == n && (n == 0 || memcmp(cur.data(), data, n) == 0);
  return same || write_file_atomic(path, data, n);  // atomic: a second instance never reads a torn file
}

std::string pref_dir() {
  std::string d;
  if (char *pref = SDL_GetPrefPath("", detail::g_game->user_dir_name)) {
    d = pref;
    SDL_free(pref);
    while (!d.empty() && (d.back() == '/' || d.back() == '\\')) d.pop_back();
  }
  return d;
}

namespace detail {

// Absolute save directory: <exe dir>/saves if writable (portable install),
// otherwise the per-user data directory. Independent of the working directory.
std::string env_name(const char *suffix) {
  std::string n;
  for (const char *c = g_game->ini_name && *g_game->ini_name ? g_game->ini_name : "game"; *c; ++c)
    n += isalnum(static_cast<unsigned char>(*c)) ? static_cast<char>(toupper(static_cast<unsigned char>(*c))) : '_';
  return n + "_" + suffix;
}

bool setup_save_dir() {
  // Game::user_dir_env (default <INI_NAME>_USER_DIR) redirects the whole user directory (release
  // smoke tests, packaging checks, test runs). It must be writable: no silent fallback.
  const std::string var = g_game->user_dir_env.empty() ? env_name("USER_DIR") : g_game->user_dir_env;
  if (const char *redirect = getenv(var.c_str())) {
    if (*redirect) {
      g_save_dir = redirect;
      fs_mkdir(g_save_dir);
      const std::string probe = g_save_dir + "/.shell_probe";
      if (!write_file(probe, "x", 1)) {
        fprintf(stderr, "%s=%s is not writable\n", var.c_str(), redirect);
        return false;
      }
      fs_remove(probe);
      return true;
    }
  }
  if (g_test_run) {
    // Test runs never touch the player's directory: next to the --ini file, or a private
    // directory under the system temp dir.
    if (!g_opt.ini.empty()) {
      const size_t cut = g_opt.ini.find_last_of("/\\");
      g_save_dir = cut == std::string::npos ? "." : g_opt.ini.substr(0, cut);
    } else {
      const char *t = getenv("TMPDIR");
      if (!t || !*t) t = getenv("TEMP");
      std::string base = t && *t ? t : "/tmp";
      while (base.size() > 1 && (base.back() == '/' || base.back() == '\\')) base.pop_back();
      g_save_dir = base + "/" + g_game->user_dir_name + "-test";
    }
    fs_mkdir(g_save_dir);
    return true;
  }
  if (char *base = SDL_GetBasePath()) {
    std::string dir = std::string(base) + "saves";
    SDL_free(base);
    fs_mkdir(dir);
    const std::string probe = dir + "/.shell_probe";
    if (write_file(probe, "x", 1)) {
      fs_remove(probe);
      g_save_dir = dir;
      return true;
    }
  }
  const std::string pref = pref_dir();
  if (!pref.empty()) {
    g_save_dir = pref;
    return true;
  }
  g_save_dir = "saves";  // last resort
  fs_mkdir(g_save_dir);
  return true;
}

// Directory of the slot files: the user directory (test runs: their private directory).
std::string slot_dir() { return g_save_dir; }

std::string slot_path(int slot) {
  return slot_dir() + (slot == 0 ? std::string("/slotQ.sav") : "/slot" + std::to_string(slot) + ".sav");
}

std::string thumb_path(int slot) {
  return slot_dir() + (slot == 0 ? std::string("/slotQ.thm") : "/slot" + std::to_string(slot) + ".thm");
}

namespace {
constexpr int kThumbDiv = 4;  // preview = picture / 4 per axis
uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
void wr32(std::vector<uint8_t> &b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i))); }
}  // namespace

// File: "RSTH", u32 seq, u16 w, u16 h, RGB bytes.
bool thumb_load(int slot, Thumb &t) {
  std::vector<uint8_t> b;
  if (!read_file(thumb_path(slot), b) || b.size() < 12 || memcmp(b.data(), "RSTH", 4) != 0) return false;
  t.seq = rd32(&b[4]);
  t.w = b[8] | (b[9] << 8);
  t.h = b[10] | (b[11] << 8);
  if (t.w <= 0 || t.h <= 0 || b.size() != 12 + static_cast<size_t>(t.w) * t.h * 3) return false;
  t.rgb.assign(b.begin() + 12, b.end());
  return true;
}

int latest_slot() {
  int best = -1;
  long long best_key = -1;
  for (int s = 0; s <= 5; ++s) {
    long long mt;
    if (!slot_time(s, mt)) continue;
    Thumb t;
    // Sequence first (saves within one second), file time as the fallback for old slots.
    const long long key = thumb_load(s, t) ? (1LL << 40) + t.seq : mt;
    if (key > best_key) { best_key = key; best = s; }
  }
  return best;
}

// Stores the preview of the current picture next to the slot's state (best effort).
static void write_thumb(int slot) {
  const std::string path = thumb_path(slot);
  if (!g_frame.valid || g_frame.w < kThumbDiv || g_frame.h < kThumbDiv) { fs_remove(path); return; }
  uint32_t seq = 0;
  for (int s = 0; s <= 5; ++s) {
    Thumb t;
    if (thumb_load(s, t) && t.seq > seq) seq = t.seq;
  }
  FrameView fv;
  fv.data = g_frame.data.data(); fv.width = g_frame.w; fv.height = g_frame.h;
  fv.bytes_per_pixel = g_frame.bpp; fv.format = g_frame.fmt;
  const int tw = g_frame.w / kThumbDiv, th = g_frame.h / kThumbDiv;
  std::vector<uint8_t> b = {'R', 'S', 'T', 'H'};
  wr32(b, seq + 1);
  b.push_back(static_cast<uint8_t>(tw)); b.push_back(static_cast<uint8_t>(tw >> 8));
  b.push_back(static_cast<uint8_t>(th)); b.push_back(static_cast<uint8_t>(th >> 8));
  for (int y = 0; y < th; ++y)
    for (int x = 0; x < tw; ++x) {
      unsigned r = 0, g = 0, bl = 0;
      for (int dy = 0; dy < kThumbDiv; ++dy)
        for (int dx = 0; dx < kThumbDiv; ++dx) {
          const uint32_t c = pixel_rgb(fv, x * kThumbDiv + dx, y * kThumbDiv + dy);
          r += (c >> 16) & 255; g += (c >> 8) & 255; bl += c & 255;
        }
      const unsigned n = kThumbDiv * kThumbDiv;
      b.push_back(static_cast<uint8_t>(r / n)); b.push_back(static_cast<uint8_t>(g / n)); b.push_back(static_cast<uint8_t>(bl / n));
    }
  if (!write_file_atomic(path, b.data(), b.size())) fs_remove(path);
}

bool delete_slot(int slot) {
  long long t;
  const bool existed = slot_time(slot, t);
  fs_remove(slot_path(slot));
  fs_remove(thumb_path(slot));
  vlog("slot %d deleted\n", slot);
  return existed;
}

// The quick slot used to be slot0.sav.
void migrate_quick_slot() {
  const std::string old = slot_dir() + "/slot0.sav", now = slot_path(0);
  std::vector<uint8_t> tmp;
  if (!read_file(now, tmp) && read_file(old, tmp)) fs_replace(old, now);
}

bool slot_time(int slot, long long &t) { return fs_mtime(slot_path(slot), t); }

bool save_slot(int slot) {
  if (g_boot_hold) return false;  // nothing meaningful to save yet
  size_t n = g_backend->serialize_size();
  if (!n) return false;
  std::vector<uint8_t> buf(n);
  fs_mkdir(slot_dir());
  if (!g_backend->serialize(buf.data(), n) || !write_file_atomic(slot_path(slot), buf.data(), n)) {
    fprintf(stderr, "save failed (%s)\n", slot_dir().c_str());
    toast("SAVE FAILED");
    return false;
  }
  write_thumb(slot);
  vlog("state saved to slot %d: %llu bytes\n", slot, static_cast<unsigned long long>(n));
  return true;
}

bool load_slot(int slot) {
  std::vector<uint8_t> buf;
  const bool ok = read_file(slot_path(slot), buf) && g_backend->unserialize(buf.data(), buf.size());
  vlog("state load from slot %d: %s\n", slot, ok ? "ok" : "failed");
  return ok;
}

// ---- the game's own save RAM (<ini_name>.sram in the user directory)
namespace {
std::vector<uint8_t> g_sram_last;
std::string sram_path() {
  const char *n = g_game->ini_name && *g_game->ini_name ? g_game->ini_name : "game";
  return g_save_dir + "/" + n + ".sram";
}
}  // namespace

void sram_load() {
  const size_t n = g_backend->save_ram_size();
  if (!n) return;
  std::vector<uint8_t> buf;
  if (read_file(sram_path(), buf) && buf.size() == n && g_backend->set_save_ram(buf.data(), n)) {
    vlog("save RAM: loaded %d bytes\n", static_cast<int>(n));
  }
  g_sram_last.assign(n, 0);
  if (!g_backend->get_save_ram(g_sram_last.data(), n)) g_sram_last.clear();
}

void sram_poll(bool force) {
  const size_t n = g_backend->save_ram_size();
  if (!n || (g_boot_hold && !force)) return;
  std::vector<uint8_t> buf(n);
  if (!g_backend->get_save_ram(buf.data(), n)) return;
  if (buf == g_sram_last) return;
  fs_mkdir(g_save_dir);
  if (write_file_atomic(sram_path(), buf.data(), n)) {
    g_sram_last = buf;
    vlog("save RAM: stored %d bytes\n", static_cast<int>(n));
  } else {
    fprintf(stderr, "save RAM could not be written to %s\n", sram_path().c_str());
  }
}

void save_state() {
  if (save_slot(0)) toast("SAVED");
}

}  // namespace detail

bool fs_mtime(const std::string &path, long long &t) {
#ifdef _WIN32
  struct _stat64 st;
  if (_wstat64(to_wide(path).c_str(), &st) != 0) return false;
#else
  struct stat st;
  if (stat(path.c_str(), &st) != 0) return false;
#endif
  t = static_cast<long long>(st.st_mtime);
  return true;
}

}  // namespace retro_shell
