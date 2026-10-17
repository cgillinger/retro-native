// Desktop shortcut helpers (Linux). The shell uses them for the first-start fallback
// (STANDARD section 1); they are public so a game's tests can exercise them directly.
#pragma once

#include <string>

#include "retro_shell/game.h"

namespace retro_shell {

enum class ShortcutResult { Created, Replaced, Unchanged, Failed };

// True when `exe` lives in an installed location (/usr/ or /opt/): a package put it there.
// Build trees, dist/ and home directories are never "installed".
bool exe_is_installed(const std::string &exe);

// The user's desktop directory: $XDG_DESKTOP_DIR, else XDG_DESKTOP_DIR in
// ~/.config/user-dirs.dirs, else ~/Desktop. "" when it does not exist.
std::string desktop_dir();

// Writes <dir>/<entry.id>.desktop (mode 0755, so Plasma trusts it) for `exe`:
//  - no file: created;  - a file whose Exec already names `exe`: left alone (Unchanged);
//  - a file whose Exec names another program: replaced (Replaced).
ShortcutResult desktop_shortcut_sync(const DesktopEntry &entry, const std::string &exe, const std::string &dir);

// The first-start rule on top of desktop_shortcut_sync. `remembered` is the exe path the shortcut was
// last made for (the shell keeps it in the ini): equal to `exe` -> nothing at all (a shortcut the
// player deleted stays deleted); otherwise sync once and remember `exe` unless it failed.
ShortcutResult desktop_shortcut_ensure(const DesktopEntry &entry, const std::string &exe, const std::string &dir,
                                       std::string &remembered, bool &skipped);

}  // namespace retro_shell
