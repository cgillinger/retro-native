// File and user-directory helpers shared by the shell and the game's content staging.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace retro_shell {

// Absolute user directory: <exe dir>/saves if writable, otherwise the per-user data dir.
// Valid after the shell has initialised SDL (before Game::stage_content runs).
const std::string &save_dir();
void set_save_dir(const std::string &dir);  // e.g. fallback after a failed staging attempt
std::string pref_dir();                     // per-user data dir without trailing separator, "" if none

void fs_mkdir(const std::string &path);
void fs_remove(const std::string &path);
bool fs_replace(const std::string &from, const std::string &to);
bool fs_mtime(const std::string &path, long long &t);  // seconds since the epoch, false = no such file

// Paths are UTF-8 on every platform.
bool read_file(const std::string &path, std::vector<uint8_t> &out);
bool write_file(const std::string &path, const void *data, size_t n);
// Writes <path>.tmp and renames it over the target, so a crash never leaves a torn file.
bool write_file_atomic(const std::string &path, const void *data, size_t n);
// Writes only when the file does not already hold exactly this content (a second instance
// must never truncate a file the first one has open).
bool stage_file(const std::string &path, const void *data, size_t n);

// Lower-case hex SHA-1 (identifies game data, e.g. in the boot snapshot header).
std::string sha1_hex(const void *data, size_t n);

// Fatal / user-visible error: stderr plus a message box (not in --frames test runs).
void show_error(const std::string &msg);

}  // namespace retro_shell
