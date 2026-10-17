// retro_shell - reusable SDL2/OpenGL frontend for statically linked emulator cores.
#pragma once

#include "retro_shell/backend.h"
#include "retro_shell/files.h"
#include "retro_shell/game.h"
#include "retro_shell/log.h"

namespace retro_shell {

// Runs the whole application (window, audio, input, main loop). Returns the exit code.
// The shell's own flags (per-run overrides of the ini): --fullscreen --window --integer-scale
// --shader auto|sharp|crt; test/development: --frames N --screenshot file.bmp --ini file
// --no-boot-hold --press SPEC --test-toast --test-hint --verbose; plus game.cli_flags.
// Instant start: --write-boot-state FILE (build time: hidden boot, snapshot, exit; 0 = written),
// --boot-state FILE (use this snapshot instead of the embedded one), --no-boot-state.
// Environment (<INI_NAME in capitals>_...): USER_DIR redirects the user directory, VERBOSE=1 acts
// like --verbose, NO_DESKTOP_SHORTCUT=1 disables the desktop shortcut fallback (Game::desktop).
// Settings live in <user dir>/<Game::ini_name>.ini; test runs (--frames) never read or write
// it unless --ini is given (the ini's directory then also holds the save slots).
int run(int argc, char **argv, Game &game, Backend &backend);

// Short on-screen message in the picture's pixel grid (letters, digits and spaces).
void toast(const char *text);

// True when running with --frames (a test run). Valid after run() has started.
bool is_test_run();

}  // namespace retro_shell
