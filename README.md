<div align="center">

# retro-native

**Turn one old game you own into a native-feeling PC game. Double-click, play. No emulator UI.**

[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
![Platforms](https://img.shields.io/badge/platforms-Linux%20%7C%20Windows-blue)
![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white)
![SDL2](https://img.shields.io/badge/SDL-2-orange)
![libretro](https://img.shields.io/badge/libretro-cores-purple)
![CMake](https://img.shields.io/badge/build-CMake%20%2B%20Ninja-064F8C?logo=cmake)
![Packages](https://img.shields.io/badge/packages-.deb%20%7C%20.rpm%20%7C%20Setup.exe-lightgrey)

*retro-gaming - libretro - SDL2 - game preservation - CRT shader - packaging*

</div>

---

## Legal: you must own the game

> **This project contains no game data and never will.**
>
> - No ROMs, disk images, tapes, game executables, system ROMs/BIOS, manuals, cover art or
>   screenshots of any game are included, linked or distributed here.
> - **You must legally own the original game** (the real cartridge, disk, tape or a licensed
>   digital copy) that you package with these tools. This project does not provide, link to or
>   help you obtain games, ROMs, disk images or system ROMs. Do not ask.
> - **Copyright infringement is not okay and is not supported**: downloading or sharing games
>   you do not own, or distributing a build that has game data embedded, is unlawful in most
>   places and is explicitly against the purpose of this project.
> - A build with embedded game data is **strictly for the owner's private use**. Never upload,
>   share or publish it. Publish only the data-free tooling in this repository.
> - Issues or pull requests that include or link to game data will be closed.

The repository ships a guard, `tools/publish-check.sh`, that refuses publication when it finds
ROM/disk/save extensions, system-ROM-like names, large blobs or personal paths.

---

## What is this?

retro-native is a **standard, a shared SDL2 shell, a CRT shader and a packaging template** for
turning *one* old game into a self-contained, native-feeling PC game:

- the player double-clicks an icon (or installs a package) and sees **the game**, not a launcher,
  a frontend or an emulator configuration screen;
- one game = one repository = one binary per OS, with the (owner's own) game data baked in;
- sensible defaults for everything, so nobody has to know what the original platform was.

It is for people who do **not** want a whole retro platform with menus, scrapers and libraries.
They just want their one favourite game from back then to be playable like any other PC game.

## How it works

There are two routes, both using the same shell and the same user experience.

**Route A - libretro core, statically linked.** A libretro core (C64, NES, SNES, Mega Drive,
Amiga, DOS via DOSBox, ...) is linked statically into the game binary together with `shell/`.
The shell provides window, video, audio, input, menu, settings and save slots; a small set of
per-game hooks (`src/game/` in the game repository) provides the title, default key map, core
options, the data to load, boot handling and mouse/port quirks. The game data is embedded in the
executable and handed to the core from memory.

**Route B - source port.** If a reconstructed engine or source port exists, the game repository is
a fork of it. The same standard applies; the shell code lives in separate files where possible,
behind the same backend interface (`run_frame`, `serialize`, `input`, `av_info`).

## Features

- **Direct start**: platforms with load times (tape/disk) boot from a snapshot taken at build
  time, so the first thing you see is the game itself.
- **Pause menu** (Esc): Resume, Controls, Save, Load, Settings, Quit. Drawn in the game's own pixel
  grid, operated by keyboard, gamepad and mouse.
- **Save slots with thumbnails**: quick slot (F5/F9) plus numbered slots with date/time and a
  screenshot each; overwrite and delete confirmations; atomic writes.
- **Controls card**: shown on first start and from the menu, with the symbols of the active device
  (keyboard, detected gamepad, mouse).
- **Rebinding**: keyboard and gamepad, conflicts swap instead of duplicating.
- **Gamepad out of the box**: SDL GameController, hotplug, several pads.
- **Video**: borderless fullscreen by default, correct aspect ratio, fit or integer scaling,
  Sharp-bilinear or **CRT filter** (scanlines per source line, brightness-compensated, optional
  phosphor mask; "Auto" picks by signal type and scale), optional border glow.
- **Timing**: speed is tied to a monotonic clock against the game's own frame rate, so a faster
  CPU/GPU, 60-240 Hz or VRR never changes game speed.
- **Packaging**: a static Linux binary built in an Ubuntu 22.04 container (runs on current
  Fedora and Ubuntu/Kubuntu), `.deb`, `.rpm`, and a Windows `.exe` plus an NSIS `Setup.exe`
  with start-menu entry, desktop shortcut and uninstaller. Icons in all sizes.
- **No console windows, no log spam**; errors appear in a dialog box.

The full, normative list is in [`docs/STANDARD.md`](docs/STANDARD.md); every game fills in
[`docs/CHECKLIST.md`](docs/CHECKLIST.md) to document conformance.

## Repository layout

| Path | Contents |
|---|---|
| `docs/STANDARD.md` | The normative standard (versioned). |
| `docs/CHECKLIST.md` | Conformance checklist, copied into each game as its conformance file. |
| `docs/LESSONS.md` | Hard-won technical lessons (timing, packaging, CRT, input, ...). |
| `shell/` | Shared SDL2/OpenGL shell library (menu, settings, saves, input, audio, video, libretro backend). No game-specific code; games supply hooks. |
| `crt/` | Reference CRT / Sharp shader (GLSL 3.30), preset logic, unit test, screenshot gate. |
| `template/tools/` | Release tooling to copy into a game repo: `release.sh`, container recipe, `.deb`/`.rpm`/NSIS packaging, icon generation, verification. |
| `tools/heavy.sh` | Runs a heavy command under a machine-wide lock and memory cap. |
| `tools/publish-check.sh` | Refuses publication if game data, save files or personal paths are present. |

## Starting a new game (short)

1. Create a repository for the game; add this repository as a submodule (or copy `shell/`), plus
   SDL2 and the libretro core as submodules under `third_party/`.
2. Copy `template/tools/` to the game's `tools/` and fill in `tools/release.conf`
   (see `release.conf.example`).
3. Keep your game data in a git-ignored folder; never commit it.
4. Write the game hooks in `src/game/` (title, key map, core options, data, boot, mouse/port).
5. Make a square icon from your own artwork or the cover of the copy you own; the packaging
   scripts generate all sizes.
6. Walk through `docs/CHECKLIST.md`; run `tools/release.sh` to produce the packages.

Details for the release tooling: [`template/tools/README.md`](template/tools/README.md).

## Build requirements

- A Linux host (developed on Fedora); CMake >= 3.20, Ninja, a C11/C++17 compiler
- **podman** (Linux release builds in an Ubuntu 22.04 container, for old-glibc compatibility)
- **mingw-w64** (Windows cross-build) and **NSIS** (`makensis`, for `Setup.exe`)
- SDL2 development files for local builds (releases link SDL statically)
- Own code builds with `-Wall -Wextra -Werror`; third-party code does not.

Heavy jobs should run through `tools/heavy.sh` so several builds never compete for RAM.

## Third-party components and licences

- The shell, CRT shader, standard and tooling in this repository: **MIT** (see [`LICENSE`](LICENSE)).
- **SDL2**: zlib licence.
- **libretro cores** (for example snes9x, VICE, DOSBox variants, ...): licences vary and many are
  **GPL**. A game binary that statically links a GPL core is itself bound by that GPL; the
  corresponding source must be available to anyone who receives the binary. Check the licence of
  each core you use. (This does not change the legal section above: builds with embedded game
  data are never to be distributed at all.)
- Cores and SDL are fetched from their official upstream repositories as git submodules in the
  game repositories, not stored here.

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md). Changes to the standard are made in this repository
only, with a version bump and an entry in `docs/LESSONS.md`.

## Disclaimer

All game titles, platform names and trademarks belong to their respective owners. This project
is not affiliated with or endorsed by any game publisher or platform holder.
