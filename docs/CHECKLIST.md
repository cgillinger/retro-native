> Translated from the maintainer's master copy (Swedish). The master is authoritative.

# Conformance checklist - Retro-native standard v1.3

Copy to the game's `docs/CONFORMANCE.md`. Tick each item, or write the deviation and its
justification under it. Section numbers refer to `STANDARD.md`.

## 1. Delivery
- [ ] **MUST** Linux x86_64 = ONE executable. Static SDL, static libstdc++/libgcc; dynamic only glibc and what SDL loads itself (X11/Wayland/GL/audio).
- [ ] **MUST** Linux release built in a podman ubuntu:22.04 container (glibc 2.35) so it runs on both Fedora and Kubuntu LTS. Game data mounted read-only, never copied into an image or layer cache. Local development builds may be native.
- [ ] **MUST** Windows 11 = ONE .exe, GUI subsystem (`-mwindows`), static, no DLLs beyond the system's (checked automatically in the build), icon and version resources, Per-Monitor DPI.
- [ ] **MUST** game data and images of the original program file are ALWAYS embedded in what is delivered. The release build does not look for an external file. External loading (`--rom`/`--disk`) only in development builds (case-insensitive search in exe dir and cwd, clear dialog if missing). Optional extras (e.g. an MT-32 ROM) may be external and optional.
- [ ] **MUST** icon from the cover (square, readable at 16 px): exe resource (.ico 16-256), window icon, installer, start/programs menu and desktop shortcut (Linux hicolor 16-512). Done when the game is created.
- [ ] **MUST** installation files (Setup.exe via NSIS, .deb, .rpm; desktop shortcut at install time for logged-in users, never recreated after removal; the game creates one itself only as a one-time fallback from an installed copy).
- [ ] **MUST** a release counts as tested only once run on Fedora, Kubuntu and Windows 11 (a VM is enough). Cross-building does not count.

## 2. Start
- [ ] **MUST** the first picture is the game's own graphics. No BASIC screen, load text or emulator logo.
- [ ] **MUST** platforms with load times (C64, disk) start directly via a build-time snapshot that is embedded; fallback = hidden warp start-up with a controlled loop (see section 5).
- [ ] **SHOULD** screens that are not part of the original (for example a cracker intro on the user's own, lawfully held copy) are skipped. The original's own logos/intro are kept. The shell provides no means of defeating copy protection.
- [ ] **MUST** window title = the game's name only. stdout/stderr silent except with `--verbose`. Errors shown in a dialog.

## 3. Keys
- [ ] **MUST** a shell shortcut never reaches the game (Alt+Enter swallowed completely, including the Alt part).
- [ ] **MUST** Esc never quits directly. Quit = menu item or window close.
- [ ] **MUST** if the original uses a reserved key (Esc pause, C64 F5, ...) it is moved to another key, shown under Controls and rebindable.
- [ ] **MUST** if the original's own screen tells the player to press a reserved key (C64 "PRESS F1") the game takes that key over (F1 only; `game_owns_f1`) instead of moving it; Esc still reaches the menu/Settings and the controls card does not name F1 as a shell shortcut. Check the original's screens for key prompts.
- [ ] **MUST** action keys are read as scancodes (layout independent); text input follows the keyboard layout (keycode/TEXTINPUT).
- [ ] **MUST** default map: arrows = movement, Z/X(/A/S) = buttons, Enter = Start, RShift = Select; the project README documents its map.
- [ ] **MUST** gamepads work without configuration (SDL GameController, hotplug, Xbox-positional); several pads supported (pad 2+ = player 2+).
- [ ] **MUST** mouse games: relative mouse only with focus, the focus click is swallowed.

## 4. Settings
- [ ] **MUST** `<game>.ini` in the user directory (section 6), saved immediately on change. The key map may be a separate file there. Never in the cwd.
- [ ] **MUST** menu (pause + settings) drawn in the game's pixel grid (pixel font, fits the platform width), operated with keyboard, gamepad AND mouse. Opening the menu releases the relative mouse and shows the pointer; closing swallows the click.
- [ ] **MUST** every menu choice exists as an ini key. CLI flags are for test/development.
- [ ] **SHOULD** pause on focus loss (setting, default on).
- [ ] **SHOULD** settings that affect the game are locked during recording/playback/netplay, with a toast.

## 5. Picture, timing, audio
- [ ] **MUST** resizable window; start size = largest integer multiple <= 90 % of screen height, in drawable pixels (HiDPI).
- [ ] **MUST** default = correct aspect ratio, fit scaling, filter Sharp, border as the original showed it.
- [ ] **MUST** CRT filter with the common look and parameters; reference = the GLSL shader in this repository (OpenGL 3.3); other renderers port it with the same parameters.
- [ ] **MUST** toasts and menu drawn in the game's pixel grid and passed through the same filter.
- [ ] **MUST** timing follows the core's/game's fps (never a hard-coded 60): no drift, no stutter, no judder beyond the unavoidable 50-on-60. Reference: vsync + accumulator, at most 3 frames per loop, audio ring with dynamic rate adjustment. Must not block VRR. A hidden fast-forward start-up is interrupted the instant it is done.
- [ ] **MUST** game speed is exactly the original's regardless of computer and display (fast CPU/GPU, 60-240 Hz, VRR, vsync off/non-blocking). Monotonic clock against the game's fps, never the number of presented frames. Test: game fps = nominal +-0.5 % over 60 s, vsync on and off (engines without an emulator: also at 60 and 144 Hz).
- [ ] **SHOULD** 50 Hz content can be shown fullscreen at 50 Hz if the display supports it.
- [ ] **MUST** mouse pointer hidden except in the menu or when the game uses the mouse.

## 6. User directory and saving
- [ ] **MUST** user directory = `<exe dir>/saves/` if writable, otherwise `SDL_GetPrefPath(<game name>)`. Ini, key map and saves live there.
- [ ] **MUST** quick slot (F5/F9) + at least 5 numbered slots in the menu: free choice of slot, date/time and thumbnail per slot, most recent highlighted, confirmation before overwriting, delete a slot (with confirmation).
- [ ] **MUST** atomic writes (tmp + rename). Test runs never touch the user directory. Rebuilds never touch the player's files.
- [ ] **SHOULD** the game's own save function (SRAM/disk) is persisted automatically.

## 8. Verification
- [ ] **SHOULD** deterministic input recording/playback for headless tests.

## 10. Default mode and optimisation

### Picture
- [ ] **MUST** start in borderless fullscreen on the current display (no mode switch). F11/menu -> window at the largest integer multiple <= 90 % of screen height.
- [ ] **MUST** fill the height with the correct aspect ratio, fractional scaling, sharp-bilinear base. Border as the original; side areas black (glow selectable under Video).
- [ ] **MUST** filter default by signal type: TV/15 kHz = Auto (light CRT when vertical scale >= 3.5, otherwise Sharp, mask only at >= 6); PC/VGA 31 kHz = Sharp.
- [ ] **MUST** beam profile computed per source line (Gaussian, width by brightness) so fractional scaling gives no moire; brightness compensation mandatory.
- [ ] **MUST** before Auto is locked for a game, screenshots Sharp / CRT / CRT+mask at 1080p and 1440p are taken and reviewed.

### Timing and audio
- [ ] **MUST** VRR Auto. Audio buffer adapts itself (grows on underrun). Volume 100 %.
- [ ] **SHOULD** 50 Hz fullscreen as an Advanced option, only for platforms with 50 Hz content.
- [ ] **MUST** pause on focus loss on - never during netplay.

### Controls must be visible
- [ ] **MUST** first start shows a controls card: active device (keyboard, detected gamepad with the right symbols, or mouse first in mouse games) -> function. Disappears on any key.
- [ ] **MUST** every start shows a readable help box on how to reach the menu, for the active device; the game stands still behind it until OK or "DON'T SHOW AGAIN" (stored in the ini, re-enable under Settings). Acknowledging keys never reach the game. On first start the controls card comes first.
- [ ] **MUST** first pause-menu item after "Resume" is "Controls" (same card, with "Change..." for rebinding). Symbols switch automatically with the device. Keys shown with understandable names, never SDL's raw names; four arrow directions merged into one "MOVE" row.
- [ ] **MUST** mouse games: sensitivity 1:1 against the screen by default; Esc/the menu always release the pointer.
- [ ] The controls card's content is verified against the game/manual, never guessed.
