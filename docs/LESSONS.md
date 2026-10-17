> Translated from the maintainer's master copy (Swedish). The master is authoritative.

# Lessons learned (newest first)

## Input and controls
- **Joystick port is decided only in a started game, never in a demo/attract mode.** One C64 game
  shipped on port 1 because the demo "reacted" there; in a real one-player match the game reads
  port 2 and the player could not steer at all. Every game needs a test that starts the game and
  requires the character to move (RAM position or a screenshot diff against a run without input),
  through both keyboard and pad.
- **A started game may wait for the first joystick movement**: measure only after an input,
  otherwise "nothing happens" looks like a bug - and attract-mode pictures can look like a started game.
- **The shell's pad tap lasts 60 ms of wall clock** (about 3 frames in real time, fewer in unpaced
  headless runs): C64 keys that the game polls slowly need a hook that holds the key a few frames.
- **The direction row on the controls card is not always "MOVE"**: in one game up/down means a
  climb request. Provide a `Game::move_label` (e.g. "WALK / CLIMB") when up/down mean something
  other than walking.
- **Verify mouse texts in the code, not only in the manual**: a manual's mouse section did not
  mention climbing, but the game's zone table showed that a left click above/below the character
  climbs. "NOT VERIFIED" or guesses must never reach the player.
- **SNES mouse is often read only on port 1.** Verify in the code, not the header. A port with
  automatic mouse <-> pad switching solves it with no setting.
- **SNES Start+Select** is a real game command in some games - the gamepad menu is Guide or
  Start+Back held >= 1 s.
- **A chord shortcut must never deliver a held button on timeout.** One port forwarded Start after
  250 ms, which triggered the game's GIVE UP screen before the Start+Back menu opened. Hold back
  until release; both down = swallow everything.
- **Mouse games must never capture the mouse without a visible way out.** A game started in a
  small window with a relative mouse and the player had to Alt+Tab out. Fullscreen by default, Esc
  always releases the pointer, and "ESC - MENU" plus the controls card are visible from first start.
- A common 8BitDo pad works with SDL GameController and automatic device switching.

## Timing and video
- **The game's timing must never be counted in presented frames.** A "vsync per present" scheme
  dropped ticks under 60 Hz and raced when present stopped blocking (minimised window). Count
  display refreshes, bound them with a budget on a monotonic clock, and force the timer path when
  minimised.
- **Headless is not reality.** An unpaced headless test and a GL readback showed the title screen
  while the real window was black: the main loop continued a flat-out batch (`to_run = 1000000`)
  after the hidden start-up had been released. Always verify timing (CPU ~15 %) and the actual
  on-screen view. On KDE Wayland, raise the window with a KWin script via
  `qdbus-qt6 org.kde.KWin /Scripting loadScript <js>` (set `workspace.activeWindow` to the window
  whose caption contains the game name), take the shot with `spectacle -b -n -f -o file.png`,
  then restore the terminal the same way.
- **Cracked C64 disks:** the title on the disk was a cracker intro waiting for Space. The shell
  skips such things during the hidden start-up and releases when the title screen is detected.
- **VGA games should not get TV scanlines** (31 kHz displays were sharp) - CRT default by signal type.
- **CRT brightness is measured in linear light.** Compare filters with the mean of (v/255)^2.2, not
  raw pixel values: softening in linear light spreads energy from sparse bright pixels so the
  gamma mean rises about 12 % although the energy changes only about 3 %. Never start a soft
  clipping curve below 1.0, or white ends up at 250/255. A mask that kills 2 of 3 subpixels cannot
  hold full white at 1.0 (the mask gives 0.95); accepted, hence mask only at scale >= 6.
- **The screenshot gate must check the size.** With `SDL_VIDEODRIVER=dummy` there is no GL; the
  shell falls back to raw 256x224 and ignores `--out-size`/`--shader` - 18 identical pictures
  "passed". The gate now requires exact WxH and deletes old files before each run.

## Packaging and distribution
- **glibc.** Builds on a current Fedora require a newer glibc and do not start on Kubuntu 24.04.
  Release in a podman ubuntu:22.04 container.
- **Two builds on the same day got the same version** -> dnf said "already installed". Use
  `<tag>.<YYYYMMDD>.<HHMM>`.
- **NSIS VIProductVersion** needs four numbers <= 65535: a date (20261017) does not fit; use MMDD.
- **rpm:** `%global debug_package %{nil}` + `%global __strip /bin/true` for a prebuilt large static
  binary; rpm 6 complains about `%source_date_epoch_from_changelog` without a changelog (harmless).
- **mingw `sdl2.pc`** points at the builder's path: patch a COPY (`PKG_CONFIG_LIBDIR`), never the
  SDL tree.
- **SELinux (Fedora) + podman bind mounts.** A script in a mounted directory gave "Permission
  denied". Use `--security-opt label=disable`, never `:Z` - `:Z` relabels the whole game repo
  (including the data).
- **rootless podman is not covered by the heavy.sh cap** (its own cgroup) - give every
  `podman run/build` a `--memory`.
- **`git archive` does not include submodules.** Export them separately (recorded SHA) or things
  like a bundled library will be missing. Always build from a clean export so container and host
  objects never mix.
- **A shortcut that only appears after the first start is never found** - the package puts it on
  the desktop at install time (postinst/%post -> `desktop-shortcut.sh`), for every logged-in
  user; the game's own first-start shortcut is only a fallback.
- **Package scripts run file operations as the user, never as root** (`runuser -u`), and find
  users via `loginctl`, not `SUDO_USER` (software centres/PackageKit lack it). Only remove the
  shortcut if its Exec is our binary.
- **Desktop shortcut only from an installed copy.** A test run once created a shortcut to the
  development build on the desktop. Rule: written only when `/proc/self/exe` is under `/usr/` or
  `/opt/`; never from build trees, `dist/`, headless or replay.
- **`./game --help` actually started the game** and wrote a desktop shortcut. Never run game
  binaries in tests without a dummy/offscreen driver.
- **`release.sh verify` prints a new timestamped version** but tests the packages already in
  `out/` - the number in the heading need not match the file name.
- **Log noise** (compiler warnings, 500 000+ lines): wait for the final lines instead of grepping
  for "error".

## Process and resources
- **Memory.** Several builds on one computer (31 GB): some generated sources take ~2.5 GB per
  compiler process; `make -j12` froze the desktop. Rule: at most -j6 (-j4 -l8 for heavy ones), never
  a Linux and a Windows build at the same time, never several emulator tests in parallel. Run all
  heavy jobs through `tools/heavy.sh <command>` (machine-wide lock, nice, hard 16 GB memory cap via
  `systemd-run --user --scope`; `HEAVY_MEM` per call when needed). A parallel build can freeze the
  desktop before anyone reacts - the cap must be in place beforehand.
- **heavy.sh is a lock, not a queue.** Another process running many short tests can starve a waiting
  job for hours; `release.sh` takes the lock per heavy step (not for the whole script - a nested
  lock deadlocks).
- **Never rewrite history of the master repository.** Several people/processes commit at the same
  time; an amend once swept in someone else's commit. Only new commits, never amend/rebase/reset
  of published history, and only your own paths in `git add`.
- **Building on someone else's engine:** start from upstream's default branch (not necessarily
  `master`) and put the shell's new code in separate files - conflicts then land only in files
  upstream already had.
- **Embedded data is loaded from memory** where the core allows (`need_fullpath=false`,
  `Game::memory_content`): never write the embedded ROM to disk; a copy in the user directory
  would never be uninstalled.
- **Icon without a cover:** render it at build time from the title screen, never stored in git.
- **Timing tests that open real windows** (`tests/speed.sh`) are for manual testing on a real
  screen, not for automated agents.
- **A test run with `rm -rf $VAR/...`** may be stopped by tooling as dangerous; write
  `"${VAR:?}"`.
- **Shared resources:** a permission check may deny a game session changing the shared shell. Let
  the shell's owner make the change; never route around the denial.
