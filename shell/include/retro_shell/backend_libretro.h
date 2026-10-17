// Backend for any libretro core that is linked statically into the executable (the retro_*
// symbols are resolved at link time; the game's CMake supplies the core library).
#pragma once

#include <memory>

#include "retro_shell/backend.h"
#include "retro_shell/game.h"

namespace retro_shell {

// Core options come from game.core_options() (evaluated when the backend is initialised,
// i.e. after the command line has been parsed).
std::unique_ptr<Backend> make_libretro_backend(const Game &game);

}  // namespace retro_shell
