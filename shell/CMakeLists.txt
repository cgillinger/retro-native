# retro_shell: reusable SDL2/OpenGL frontend library.
#
# The parent project must provide the SDL2::SDL2-static target before add_subdirectory().
#   retro_shell          the frontend (window, GL, audio, input, menu, ini); no emulator dependency.
#                        A game links it and passes its own retro_shell::Backend to run().
#   retro_shell_libretro Backend for a statically linked libretro core. Only created when
#                        RETRO_SHELL_CORE_TARGET names the core library target (it supplies
#                        libretro.h through its include directories).
cmake_minimum_required(VERSION 3.20)

if(NOT TARGET SDL2::SDL2-static)
  message(FATAL_ERROR "retro_shell: define the SDL2::SDL2-static target first")
endif()

include(${CMAKE_CURRENT_LIST_DIR}/../crt/crt.cmake)  # shared CRT shader (target retro_crt)

add_library(retro_shell STATIC
  src/app.cpp
  src/audio.cpp
  src/draw.cpp
  src/cli.cpp
  src/controls.cpp
  src/input.cpp
  src/ini.cpp
  src/bootstate.cpp
  src/log.cpp
  src/menu.cpp
  src/platform.cpp
  src/settings.cpp
  src/sha1.cpp
  src/shortcut.cpp
  src/toast.cpp
  src/userdir.cpp
  src/video.cpp)
target_include_directories(retro_shell PUBLIC include PRIVATE src)
# The game's main() lives outside SDL's SDL_main wrapper.
target_compile_definitions(retro_shell PUBLIC SDL_MAIN_HANDLED)
target_link_libraries(retro_shell PUBLIC SDL2::SDL2-static PRIVATE retro_crt)
target_compile_options(retro_shell PRIVATE -Wall -Wextra -Werror)

if(RETRO_SHELL_CORE_TARGET)
  add_library(retro_shell_libretro STATIC src/backend_libretro.cpp)
  target_include_directories(retro_shell_libretro PUBLIC include PRIVATE src)
  target_link_libraries(retro_shell_libretro PUBLIC retro_shell PRIVATE ${RETRO_SHELL_CORE_TARGET})
  target_compile_options(retro_shell_libretro PRIVATE -Wall -Wextra -Werror)
endif()

# retro_shell_static_runtime(<target>): link libstdc++/libgcc statically (STANDARD section 1).
# A release build (CMAKE_BUILD_TYPE=Release and RETRO_RELEASE=ON) fails when static libstdc++
# is not installed (Fedora: libstdc++-static); a development build only warns and links it
# dynamically.
function(retro_shell_static_runtime target)
  include(CheckCXXSourceCompiles)
  set(CMAKE_REQUIRED_LINK_OPTIONS -static-libstdc++ -static-libgcc)
  check_cxx_source_compiles("#include <string>\nint main(){std::string s(\"x\");return (int)s.size()-1;}" RETRO_HAVE_STATIC_LIBSTDCXX)
  unset(CMAKE_REQUIRED_LINK_OPTIONS)
  target_link_options(${target} PRIVATE -static-libgcc)
  if(RETRO_HAVE_STATIC_LIBSTDCXX)
    target_link_options(${target} PRIVATE -static-libstdc++)
  elseif(RETRO_RELEASE AND CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR "RETRO_RELEASE: static libstdc++ is required (Fedora: dnf install libstdc++-static)")
  else()
    message(WARNING "static libstdc++ not available (Fedora: libstdc++-static); linking it dynamically")
  endif()
endfunction()
