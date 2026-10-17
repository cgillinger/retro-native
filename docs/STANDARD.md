> Translated from the maintainer's master copy (Swedish). The master is authoritative.

# Retro-native standard - v1.2 (2026-10-17)

Applies to every game project. MUST = requirement. SHOULD = default that may be deviated from
with a justification in the project's `docs/CONFORMANCE.md` (a copy of `CHECKLIST.md`).

## 0. Principle and audience
The player should experience an ordinary PC game: double-click, the game starts. No emulator UI,
no launcher, no console windows, no settings that require knowledge of the original platform.
Every game is fully self-contained (no shared launcher or multi-game menu).
Builds with embedded game data are for private use only and are never published (`dist/` never
leaves the owner's computer). You must have the lawful right to use the game and to make the
copies the build involves (see the legal section in the repository README).

## 1. Delivery
- MUST: Linux x86_64 = ONE executable. Static SDL, static libstdc++/libgcc; dynamic only glibc and
  what SDL loads itself (X11/Wayland/GL/audio).
- MUST: the Linux release is built in a podman container, ubuntu:22.04 (glibc 2.35), so it runs on
  both Fedora and Kubuntu LTS. (A build on a current Fedora requires a newer glibc and does not
  start on Kubuntu 24.04.) Game data is mounted read-only into the container and is never copied
  into an image or layer cache. Local development builds may be native.
- MUST: Windows 11 = ONE .exe, GUI subsystem (`-mwindows`), static, no DLLs beyond the system's
  (checked automatically in the build), icon and version resources, Per-Monitor DPI.
- MUST: game data (ROM/disk/tape/DATA.DAT) and images of the original program file (e.g.
  `ds_static.bin`/`exe_static.bin`) are ALWAYS embedded in what is delivered. The release build
  does not look for an external file. External loading (`--rom`/`--disk`) only in development
  builds; there: search case-insensitively in the executable's directory and the cwd, and show a
  clear dialog if missing. Optional extra content (e.g. an MT-32 ROM) may be external and optional.
- MUST: icon derived from the cover in `docs/` (square, readable at 16 px): executable resource
  (.ico 16-256), window icon, installer, start/programs menu and desktop shortcut (Linux hicolor
  16-512). Done when the game is created, not at delivery.
- MUST: installation files - the player installs like any other program:
  - Windows: `<Game>-Setup.exe` (NSIS, built on Linux): installs under Program Files, start menu
    shortcut + desktop shortcut (default on), uninstall entry in "Apps". User data goes to the
    per-user pref path (Program Files is not writable).
  - Linux Kubuntu/Ubuntu: `.deb` (built in the ubuntu:22.04 container). Double-click opens the
    software centre -> Install. Binary in `/usr/games` or `/opt/<game>`, `.desktop` in
    `/usr/share/applications`, icon in hicolor. Uninstall via the software centre.
  - Linux Fedora: `.rpm` with the same content.
  - Linux desktop shortcut: the package puts the shortcut on the desktop at install time, for
    every logged-in user (XDG desktop dir, owned by the user, executable so Plasma trusts it),
    also when installed through a software centre - on fresh install only; upgrades and
    reinstalls do not touch the desktop (a deleted shortcut does not come back). Uninstall removes
    it if it still points at the game. The game itself creates it on first start only as a
    fallback (users who were not logged in), once, and only from an installed copy
    (`/usr/`, `/opt/`) - never from development or test runs.
  - No AppImage/Flatpak/Snap (libfuse2 is missing on Kubuntu; Flatpak is not preinstalled).
  - The loose binary (`dist/`) remains for development and portable use.
- MUST: a release counts as tested only once it has been run on Fedora, Kubuntu and Windows 11
  (a VM is enough). Cross-building does not count.

## 2. Start
- MUST: the first picture the player sees is the game's own graphics. No BASIC screen, load text
  or emulator logo.
- MUST: platforms with load times (C64, disk) start directly through a snapshot taken at build
  time and embedded; fallback = hidden warp start-up with a controlled loop (see section 5).
- SHOULD: screens that are not part of the original (for example a cracker intro on the user's
  own, lawfully held copy) are skipped. The original's own logos/intro are kept (and can be
  skipped as in the original). The shell provides no means of defeating copy protection.
- MUST: window title = the game's name (nothing else). stdout/stderr silent except with
  `--verbose`. Errors are shown in a dialog box.

## 3. Keys
The shell's reserved keys - the only ones:
| Key | Function |
|---|---|
| Esc | The shell's pause menu: Resume / Save / Load / Settings / Quit. The game is paused. |
| F1 | Pause menu directly on Settings |
| F5 / F9 | Quick save / quick load |
| F11, Alt+Enter | Fullscreen on/off |
| Gamepad Guide, or Start+Back held >= 1 s | Pause menu (a short press reaches the game) |
- MUST: a shell shortcut never reaches the game (Alt+Enter is swallowed completely, including
  the Alt part).
- MUST: Esc never quits directly. Quit = menu item or window close.
- MUST: if the original uses a reserved key (Esc pause, C64 F5, ...) it is moved to another key,
  shown under Controls and rebindable. The original's own pause menu may stay on e.g. P.
- MUST: action keys are read as scancodes (layout independent); text input in the game
  (passwords, names, Y/N) follows the keyboard layout (keycode/TEXTINPUT).
- MUST: default map: arrows = movement, Z/X(/A/S) = buttons, Enter = Start, RShift = Select; the
  project README documents its map.
- MUST: gamepads work without configuration (SDL GameController, hotplug, Xbox-positional);
  several pads are supported (pad 2+ = player 2+ where the game has that).
- MUST: mouse games: relative mouse only with focus; the focus click is swallowed.

## 4. Settings
- MUST: `<game>.ini` in the user directory (section 6), saved immediately on change. The key map
  may live in a separate file in the same directory. Never in the cwd.
- MUST: the menu (pause + settings) is drawn in the game's pixel grid (pixel font, fits the
  platform's width, e.g. 256 px SNES = about 40 columns) and operated with keyboard, gamepad AND
  mouse. When the menu opens, the relative mouse is released and the pointer shown; on close the
  click is swallowed.
- Menu text: English. If the original game has several languages, the game language is a choice in
  the main settings, never under Advanced.
- MUST tabs:
  - Controls: rebind key and gamepad button per function; a key already in use swaps places
    instead of duplicating; a reserved key gives a toast; restore defaults. Mouse games: mouse
    on/off and sensitivity.
  - Video: fullscreen, scaling (fit / integer), aspect ratio (the platform's correct one +
    alternatives; SHOULD: widened playfield where the engine can), filter (Sharp / CRT +
    strength), border (black; SHOULD glow), vsync.
  - Audio: volume, mute.
  - Advanced: see section 10. The player should never need to go here.
- MUST: every menu choice exists as an ini key. CLI flags are for test/development.
- SHOULD: pause on focus loss (setting, default on).
- SHOULD: settings that affect the game are locked during recording/playback/netplay, with a toast.

## 5. Picture, timing, audio
- MUST: resizable window; start size = largest integer multiple <= 90 % of the screen height,
  counted in drawable pixels (HiDPI).
- MUST: default = the platform's correct aspect ratio, fit scaling, filter Sharp, border as the
  original showed it.
- MUST: CRT filter with a common look and common parameters. Reference = the GLSL shader in this
  repository (OpenGL 3.3); games with another renderer port it with the same parameters.
- MUST: toasts and menu are drawn in the game's pixel grid and go through the same filter.
- MUST: timing follows the core's/game's fps (never a hard-coded 60). Result requirements: no
  drift, no stutter, no judder beyond the unavoidable 50-on-60. Reference method: vsync +
  accumulator, at most 3 frames per loop, audio ring with dynamic rate adjustment. Timing must
  not block VRR: with VRR, present at the core's rate. A hidden fast-forward start-up is
  interrupted the instant it is done.
- MUST: game speed is exactly the original's regardless of computer and display: a faster
  CPU/GPU, 60/120/144/240 Hz, VRR, vsync off or a vsync that does not block (minimised/hidden
  window) must never yield higher game speed. Timing is driven by a monotonic clock against the
  game's fps, never by "number of presented frames". Tested: measured game fps = nominal +-0.5 %
  over 60 s with vsync on and off (and for engines without an emulator: at 60 and 144 Hz).
- SHOULD: 50 Hz content can be shown fullscreen at 50 Hz if the display supports it.
- MUST: the mouse pointer is hidden except in the menu or when the game uses the mouse.

## 6. User directory and saving
- MUST: user directory = `<exe dir>/saves/` if writable, otherwise `SDL_GetPrefPath(<game name>)`.
  Ini, key map and save files live there.
- MUST: a quick slot (F5/F9) plus at least 5 numbered slots in the menu that the player manages:
  free choice of slot to save to and load from, date/time and a thumbnail per slot, most recently
  used highlighted, confirmation before overwriting, and the ability to delete a slot (with
  confirmation).
- MUST: atomic writes (tmp + rename). Test runs never touch the user directory. Rebuilds never
  touch the player's files.
- SHOULD: the game's own save function (SRAM/disk) is persisted automatically.

## 7. File structure
MUST for own projects, SHOULD for games built on someone else's engine (they follow at least
`docs/STANDARD.md`, `docs/CONFORMANCE.md`, `tools/release.sh` and keep shell code in separate
files where possible).
```
README.md            for the player: start, keys, settings
docs/STANDARD.md     copy of the standard version the game follows
docs/CONFORMANCE.md  filled-in checklist + deviations with justification
docs/STATUS.md       work log, newest first, never overwritten
docs/cover.jpg       cover -> icon (protected artwork: private build only, never published)
cmake/               mingw64-toolchain.cmake etc.
shell/               submodule: shared shell (libretro games)
src/game/            game-specific: hooks for title, key map, core options, data, boot, mouse/port
src/port/            optional, route B (native port)
third_party/         submodules: SDL, core
tests/               smoke.sh etc.
tools/release.sh     dist/: binaries + .deb + .rpm (container) + Setup.exe (NSIS), dependency check
rom/ | disk/ | data/ git-ignored game data
build/ build-win/ build-dbg/ dist/   git-ignored
```

## 8. Verification (definition of done)
- Headless smoke (unpaced) AND a paced windowed run with a CPU check.
- SHOULD: deterministic input recording/playback for headless tests.
- Confirmation in the real desktop view (a screenshot of the actual screen) before anything is
  called fixed.
- Fresh review before merge. The status log is updated every session.
- Release: installed and run through the installation file on Fedora, Kubuntu (a tester) and
  Windows 11 (VM); uninstall tried; dependency check of the binaries.

## 9. Where the standard lives
Both:
- This repository = master: the standard (versioned), the checklist, `template/` (release tooling),
  `shell/` shared shell, `crt/` reference shader.
- Each game: a copy `docs/STANDARD.md` + a filled-in `docs/CONFORMANCE.md`. Upgrading = a
  deliberate version bump and a walk through the checklist. The standard is changed only here.
- Shared shell (`shell/`, a submodule pinned to a commit per game): no game-specific `#ifdef`;
  the game supplies hooks in `src/game/`. The shell is written against an internal backend
  interface (run_frame, serialize, input, av_info) - libretro is one backend, a native port
  another. Menu, ini, toasts, font, CRT shader, save slots and user directory live in the shell.
- Games on someone else's engine: own git fork with upstream as a remote; general improvements
  (ini location, GUI subsystem, icon) are proposed upstream.

## 10. Default mode and optimisation
Basic rule: the player should never need to open the settings. Everything that can be decided
automatically is decided automatically. Unlike some retro frontends, Advanced is only for those
who want it.

### Picture
- MUST: start in borderless fullscreen on the current display (no mode switch). F11/menu ->
  window, which then opens at the largest integer multiple <= 90 % of the screen height.
- MUST: fill the height with the correct aspect ratio, fractional scaling, sharp-bilinear base.
  Border as the original showed it; side areas black (glow selectable under Video).
- MUST: the filter default depends on the original's signal type:
  - TV/15 kHz (C64, NES, SNES, Amiga, consoles): **Auto** = light CRT (scanlines, slight
    horizontal softness, brightness-compensated, no curvature, no heavy mask) when the vertical
    scale factor >= 3.5; below that Sharp; phosphor mask only at >= 6.
  - PC/VGA 31 kHz (DOS): **Sharp**. CRT is selectable.
- MUST (requirement on the shared CRT shader): the beam profile is computed per source line
  (Gaussian, width by brightness) so fractional scaling gives no moire; brightness compensation
  is mandatory.
- MUST: before Auto is locked for a game, screenshots Sharp / CRT / CRT+mask at 1080p and 1440p
  are taken and reviewed (an item in CONFORMANCE.md).
- Engine improvements that are pure wins (e.g. subpixel/camera smoothing) are on. Anything that
  changes the game's content or difficulty (widened field, balance changes) is off.

### Timing and audio
- MUST: VRR Auto. The audio buffer adapts itself (grows on underrun). Volume 100 %.
- SHOULD: 50 Hz fullscreen as an Advanced option, only for platforms with 50 Hz content.
- MUST: pause on focus loss on - never during netplay.

### Controls must be visible
- MUST: first start shows a controls card for the game: active device (keyboard, detected gamepad
  with the right symbols, or mouse first in mouse games) -> function. Game-specific content is
  allowed (e.g. one row per character). It disappears on any key.
- MUST: every start shows a help box about how to reach the game's menu, for the active device and
  readable (not a small corner text), e.g. "PRESS ESC FOR MENU & CONTROLS" / "PRESS GUIDE (OR HOLD
  START+BACK) FOR MENU". The game stands still behind the box until the player chooses OK
  (Enter/A/click) or "DON'T SHOW AGAIN" (stored in the ini; can be re-enabled under Settings).
  Keys that acknowledge the box never reach the game. On first start the controls card comes first.
- MUST: the first pause-menu item after "Resume" is "Controls" = the same card, with "Change..."
  for rebinding. The card switches symbols automatically when the player switches device. Keys are
  shown with understandable names (arrow symbols or "UP ARROW", ENTER, NUMPAD 8, LEFT CTRL), never
  SDL's raw names; four directions on the arrow keys are merged into one row "MOVE" with arrows.
- MUST: mouse games: sensitivity 1:1 against the screen by default; Esc/the menu always release
  the pointer.
- The controls card's content is verified against the game/manual - never guessed.

### Advanced - contents
Only: VSync/VRR (Auto/on/off), frame delay, audio buffer (manual), 50 Hz fullscreen, pause on
focus loss, restore all settings, and safe platform extras (C64: SID 6581/8580; a source port:
subpixel/motion/camera, statistics, external-ROM audio, etc.). Never: region, machine model, disk
drive emulation, ports, cheats/trainers. Netplay (host/join) and game language belong to the main
menu.
