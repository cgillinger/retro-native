# Contributing

Thanks for helping. Please read the standard in `docs/STANDARD.md` first; it is normative.

## Hard rules

- **No game data, ever.** Do not attach, paste, upload or link ROMs, disk/tape images, system
  ROMs/BIOS, game executables, manuals, cover art or screenshots of games.
  **Issues or pull requests that include or link to game data will be closed.**
- Do not ask where to get games. You must own the original game you package.
- No game-specific code or `#ifdef`s in `shell/`; games use hooks in their own `src/game/`.
- Dependencies only from official upstream sources, as git submodules under `third_party/`.
- Own code builds with `-Wall -Wextra -Werror` (C11/C++17, CMake >= 3.20, Ninja).
- Run `tools/publish-check.sh` before opening a pull request.
- Test windows must not steal focus: use `SDL_WINDOW_NO_ACTIVATION_WHEN_SHOWN=1`, keep runs short.

## Changing the standard

Edit `docs/STANDARD.md`, bump the version, update `docs/CHECKLIST.md`, and add the reasoning to
`docs/LESSONS.md`.
