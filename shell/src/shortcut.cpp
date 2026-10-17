// Desktop shortcut fallback for installed copies (STANDARD section 1): the package normally
// creates the shortcut at install time; this covers users who were not logged in then.
#include "retro_shell/desktop.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "internal.h"

namespace retro_shell {

#ifdef _WIN32

bool exe_is_installed(const std::string &) { return false; }
std::string desktop_dir() { return ""; }
ShortcutResult desktop_shortcut_sync(const DesktopEntry &, const std::string &, const std::string &) {
  return ShortcutResult::Failed;
}
ShortcutResult desktop_shortcut_ensure(const DesktopEntry &, const std::string &, const std::string &, std::string &, bool &skipped) {
  skipped = true;
  return ShortcutResult::Failed;
}
namespace detail {
void desktop_shortcut_first_start() {}
}  // namespace detail

#else

namespace {

bool is_dir(const std::string &p) {
  struct stat st;
  return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// Desktop Entry Exec values: quote a path that has anything beyond plain characters.
std::string exec_value(const std::string &exe) {
  bool plain = true;
  for (char c : exe)
    if (!(isalnum(static_cast<unsigned char>(c)) || strchr("/._-+", c))) plain = false;
  if (plain) return exe;
  std::string q = "\"";
  for (char c : exe) {
    if (strchr("\"`$\\", c)) q += '\\';
    q += c;
  }
  return q + "\"";
}

// The program an Exec= value starts (first word, quotes and escapes resolved, arguments dropped).
std::string exec_program(const std::string &v) {
  size_t i = v.find_first_not_of(" \t");
  if (i == std::string::npos) return "";
  std::string out;
  if (v[i] == '"') {
    for (++i; i < v.size() && v[i] != '"'; ++i) {
      if (v[i] == '\\' && i + 1 < v.size()) ++i;
      out += v[i];
    }
  } else {
    for (; i < v.size() && v[i] != ' ' && v[i] != '\t'; ++i) out += v[i];
  }
  return out;
}

bool existing_exec(const std::string &path, std::string &program) {
  std::vector<uint8_t> raw;
  if (!read_file(path, raw)) return false;
  const std::string text(raw.begin(), raw.end());
  size_t pos = 0;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    if (text.compare(pos, 5, "Exec=") == 0) {
      std::string v = text.substr(pos + 5, end - pos - 5);
      if (!v.empty() && v.back() == '\r') v.pop_back();
      program = exec_program(v);
      return true;
    }
    pos = end + 1;
  }
  program.clear();
  return true;  // a file without Exec: not ours, replace
}

std::string exe_path() {
  char buf[4096];
  const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  return n > 0 ? std::string(buf, static_cast<size_t>(n)) : std::string();
}

std::string home() {
  const char *h = getenv("HOME");
  return h && *h ? h : "";
}

}  // namespace

bool exe_is_installed(const std::string &exe) { return exe.compare(0, 5, "/usr/") == 0 || exe.compare(0, 5, "/opt/") == 0; }

std::string desktop_dir() {
  std::string d;
  if (const char *e = getenv("XDG_DESKTOP_DIR")) d = e;
  const std::string h = home();
  if (d.empty() && !h.empty()) {
    std::vector<uint8_t> raw;
    const char *cfg = getenv("XDG_CONFIG_HOME");
    const std::string file = (cfg && *cfg ? std::string(cfg) : h + "/.config") + "/user-dirs.dirs";
    if (read_file(file, raw)) {
      const std::string text(raw.begin(), raw.end());
      const size_t k = text.find("XDG_DESKTOP_DIR=");
      if (k != std::string::npos && (k == 0 || text[k - 1] == '\n')) {
        size_t end = text.find('\n', k);
        std::string v = text.substr(k + 16, end == std::string::npos ? std::string::npos : end - k - 16);
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        const std::string tag = "$HOME";
        const size_t p = v.find(tag);
        if (p == 0) v = h + v.substr(tag.size());
        d = v;
      }
    }
    if (d.empty()) d = h + "/Desktop";
  }
  while (d.size() > 1 && d.back() == '/') d.pop_back();
  return !d.empty() && is_dir(d) ? d : "";
}

ShortcutResult desktop_shortcut_sync(const DesktopEntry &e, const std::string &exe, const std::string &dir) {
  const std::string path = dir + "/" + e.id + ".desktop";
  bool existed = false;
  std::string prog;
  if (existing_exec(path, prog)) {
    existed = true;
    if (prog == exe) return ShortcutResult::Unchanged;
  }
  std::string t = "[Desktop Entry]\nType=Application\nName=" + e.name + "\n";
  if (!e.comment.empty()) t += "Comment=" + e.comment + "\n";
  t += "Exec=" + exec_value(exe) + "\n";
  if (!e.icon.empty()) t += "Icon=" + e.icon + "\n";
  t += "Terminal=false\n";
  if (!e.categories.empty()) t += "Categories=" + e.categories + "\n";
  if (!write_file_atomic(path, t.data(), t.size())) return ShortcutResult::Failed;
  chmod(path.c_str(), 0755);
  return existed ? ShortcutResult::Replaced : ShortcutResult::Created;
}

ShortcutResult desktop_shortcut_ensure(const DesktopEntry &e, const std::string &exe, const std::string &dir,
                                       std::string &remembered, bool &skipped) {
  skipped = remembered == exe;
  if (skipped) return ShortcutResult::Unchanged;
  const ShortcutResult r = desktop_shortcut_sync(e, exe, dir);
  if (r != ShortcutResult::Failed) remembered = exe;
  return r;
}

namespace detail {

// First start of an installed copy: put the shortcut on the desktop, once per installed path.
// Never from development or test runs (build tree, dist/, --frames), never when disabled by
// <PREFIX>_NO_DESKTOP_SHORTCUT. The ini remembers the exe path the shortcut was last made for, so
// a shortcut the player deletes stays deleted, while a copy installed elsewhere replaces a stale one once.
void desktop_shortcut_first_start() {
  const DesktopEntry &e = g_game->desktop;
  if (e.id.empty() || g_test_run || getenv(env_name("NO_DESKTOP_SHORTCUT").c_str())) return;
  const std::string exe = exe_path();
  if (!exe_is_installed(exe)) return;
  std::string remembered;
  g_ini.get("general", "desktop_shortcut", remembered);
  const std::string dir = desktop_dir();
  if (dir.empty()) return;
  bool skipped = false;
  const ShortcutResult r = desktop_shortcut_ensure(e, exe, dir, remembered, skipped);
  if (skipped) return;
  vlog("desktop shortcut: %s (%s)\n", r == ShortcutResult::Created ? "created" : r == ShortcutResult::Replaced ? "replaced"
                                       : r == ShortcutResult::Unchanged ? "already present" : "failed", dir.c_str());
  if (r != ShortcutResult::Failed) g_ini.set("general", "desktop_shortcut", remembered);
}

}  // namespace detail

#endif

}  // namespace retro_shell
