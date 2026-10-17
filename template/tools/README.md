# template/tools — release packaging for an SDL game

Written for someone who knows nothing about this setup. Goal (STANDARD §1): from a game repo,
one command produces `dist/` with a static Linux binary, a `.deb`, an `.rpm`, a Windows `.exe` and
a `Setup.exe` (NSIS). Proven on The Lost Vikings (`vikings/LostVikings/tools/`, same files).

## What is in here
| File | Role |
|---|---|
| `release.sh` | Orchestrator. Steps: `linux repack rpm windows nsis smoke dist verify` (`repack` = re-stage + `.deb` from the already built binary, no compile) (default = all but `verify`). Reads `tools/release.conf`. |
| `release.conf.example` | Copy to `release.conf` and fill in: the ONLY game-specific file. |
| `packaging/Containerfile` | ubuntu:22.04 + clang 18 + SDL2 built from source (static lib, dlopen'ed backends). No game data. |
| `packaging/container-build.sh` | Runs inside the container: build, strip, `check_deps.sh`, stage tree, `.deb` (dpkg-deb). |
| `packaging/make_rpm.sh`, `rpm/package.spec.in` | Host `rpmbuild` repackaging the staged tree (no debuginfo, no brp-strip). |
| `packaging/win-build.sh` | Host mingw cross build + strip + DLL allow-list check. |
| `packaging/nsis/installer.nsi` | MUI2 installer, parameterised by `-D` defines from release.sh. |
| `packaging/deb/{control,desktop,copyright}.in` | Templates (`@VAR@` tokens, see `lib.sh: render`). |
| `packaging/make_icons.sh` | One square PNG -> hicolor 16..512 + `.ico`. |
| `packaging/check_deps.sh` | Linux: only glibc libs, max `GLIBC_` <= `MAX_GLIBC`. Windows: only allow-listed DLLs. |
| `packaging/desktop-shortcut.sh` | Installed as `/usr/lib/<id>/desktop-shortcut.sh`; called by deb `postinst`/`prerm` (`deb/{postinst,prerm}.in`) and rpm `%post`/`%preun`. See "Install-time desktop shortcut". |
| `packaging/verify.sh` | Installs the .deb in ubuntu 24.04 + 22.04 and the .rpm in fedora containers, smoke-runs, removes. |

## Install-time desktop shortcut
Installing the `.deb`/`.rpm` (fresh install only; an upgrade or reinstall neither creates nor removes it, so a deleted shortcut stays deleted) puts `<id>.desktop` on the desktop of every logged-in user at once (a shortcut that
only appears after the first start is never found). `desktop-shortcut.sh install|remove <id> <exec path>`:
- Users: `loginctl list-sessions` (works under Discover/PackageKit, no `SUDO_USER` needed), uid 1000..59999, deduplicated,
  plus `SUDO_USER` and the `PKEXEC_UID` user. No `loginctl` (containers) and no override = nobody is served.
- Run-as-user rule: every file operation runs as that user (`runuser -u`, else `setpriv`), never as root: no symlink
  attacks, correct ownership. `HOME` is passed explicitly, `XDG_CONFIG_HOME` dropped.
- Desktop dir: `XDG_DESKTOP_DIR` from `$HOME/.config/user-dirs.dirs` (`$HOME` expanded), else `~/Desktop`; if it does not
  exist the user is skipped (never created). Same rules as the game's `v2_desktop_shortcut`.
- Target `<desktop>/<id>.desktop`: identical content = nothing; `Exec=` is our binary (user may have customised it) = left
  alone; `Exec=` elsewhere (old dev shortcut) = replaced; absent = copy of `/usr/share/applications/<id>.desktop`.
  Written atomically (tmp + mv), mode 0755 (KDE trusts it), plus best-effort `gio set metadata::trusted true` when the
  user's bus `/run/user/<uid>/bus` exists.
- Triggers: deb `postinst` when `$1 = configure` and `$2` is empty; rpm `%post` when `$1 == 1`.
- Uninstall: deb `prerm` only on `remove` (not upgrade), rpm `%preun` only when `$1 == 0`: the file is removed only if
  its `Exec=` is still our binary. Everything is best effort (`|| true`), one log line per user, never fails the transaction.
- In-game fallback: the game's first-launch shortcut stays for users not logged in at install time. Package `.desktop`
  (`deb/desktop.in`, `Exec=/usr/games/<bin>`) and the game's file use the same Exec, so there are no duplicates.
- Windows: NSIS creates its shortcut at install; unchanged.
- Test override: `RN_SHORTCUT_USERS="u1 u2"` (env reaches apt/dnf scriptlets) or the file `/etc/<id>-shortcut-users`
  replaces user detection. `verify.sh` uses it (user `vtest`, checks owner/mode and removal).
- Test without a rebuild: `tools/release.sh repack rpm dist` (needs an earlier `linux` run).

## Adopt it for a new game (step by step)
1. Copy `template/tools/` into the game repo's `tools/` (release.sh, release.conf.example, packaging/).
   Keep them identical to the template; improve them HERE first and copy back.
2. `cp tools/release.conf.example tools/release.conf` and fill in: `GAME_ID` (lowercase, becomes the
   package name, `<id>.desktop` and hicolor icon name), `GAME_NAME`, `BIN_NAME`, `ICON_PNG`,
   description, maintainer, `CATEGORIES`, `DATA_FILES`, the two build commands and output names.
   The game must already: build with clang-18 + static SDL (`pkg-config --static sdl2`), embed its data
   (STANDARD §1), write user files to a per-user dir when the exe dir is not writable (§6), and offer
   env switches so the smoke test never touches the real user dir / desktop.
3. Icon: a square PNG >= 256 px readable at 16 px (`docs/icon.png`); `make_icons.sh` derives the rest.
   The game's own `.ico` exe resource is separate (the repo's windres input).
4. Game id consistency: if the game writes its own desktop shortcut/icon lookup (vikings does:
   `~/Desktop/lost-vikings.desktop`, `Icon=lost-vikings`), the package must use the same id.
5. Add `/dist/` to `.gitignore`. Commit `tools/` BEFORE releasing: the build uses a `git archive` of
   HEAD (+ submodules at their recorded commits), never your working tree, so container and host
   objects can never mix with stale ones.
6. Run: `tools/release.sh` (steps can be named: `tools/release.sh linux rpm`). Work dir:
   `~/.cache/retro-native-release/<GAME_ID>/`. First run builds the image (~10 min, cached after).
7. Verify: `tools/release.sh verify`, then the manual tests (Fedora host, Kubuntu, Windows 11 VM).
   STANDARD §1: a release only counts as tested after those.

## Resource rules (a build once froze the desktop)
- release.sh calls `retro-native/tools/heavy.sh` itself for every heavy step (machine-wide lock +
  16 GB cap). NEVER wrap release.sh in heavy.sh (nested lock = deadlock). It may wait for another heavy job.
- `JOBS` defaults to 4, `LOAD` to 8 (`-j4 -l8`); release.sh refuses JOBS > 4. Never `$(nproc)`.
- heavy.sh's cap does not cover rootless podman, so every `podman build/run` carries
  `--memory=14g --memory-swap=15g`. Keep that in any container command you add.
- Never run two builds at once (Linux and Windows steps are sequential).
- Huge generated sources: clang needs `ulimit -s unlimited` (done in container-build.sh); gcc may need > 8 GB per unit.

## Legal
The release embeds the copyrighted game data. Everything in `dist/` is private: it never leaves the
machine, is never pushed, uploaded or attached anywhere. Data is mounted read-only into the container
(`-v DATA_SRC:/data:ro`), the image context is `packaging/` only, so it is never in an image layer.

## Version
Exact git tag `vX.Y` -> `X.Y`; otherwise `<last numeric tag or 1.0>.<YYYYMMDD>.<HHMM>` (so two builds on one day still upgrade); `VERSION=` in the conf
overrides. The NSIS `VIProductVersion` needs four numbers <= 65535, so a date part is replaced by MMDD.

## Testing
- Smoke (any binary): `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 8 <bin>` with the game's
  isolation env (`SMOKE_ENV`, e.g. `V2_NO_DESKTOP_SHORTCUT=1`; the user dir is redirected via the env var named
  by `SMOKE_USER_DIR_VAR`, default `V2_USER_DIR`); output must hold `SMOKE_PATTERN`.
  `release.sh smoke` does it for the Linux binary; `verify` does it for the installed packages.
- Dependencies: `packaging/check_deps.sh linux <bin> 2.35` and `... windows <exe>` (already run by the build).
- `.deb`: `dpkg-deb -I/-c`, install in ubuntu:22.04 and 24.04 containers (verify does).
- `.rpm`: `rpm -qlpv`, `rpm -qp --requires`, install in a fedora container (verify does).
- NSIS: warnings from makensis; `wine` silent install (`/S`) if wine exists, else only the Windows 11 VM counts.
- Installing on the host needs sudo: leave that to the user, write him exact steps.

## Migration note: hand-made menu entries shadow the package
A hand-made `~/.local/share/applications/<id>.desktop` (or a desktop shortcut) overrides
`/usr/share/applications/<id>.desktop` of the package, so the menu keeps launching the OLD path. After
installing the package, tell the user, and ask before removing the hand-made file. NEVER delete it
automatically. The game writing its own `~/<Desktop>/<id>.desktop` on first start is fine; with the same id and
`Exec=/proc/self/exe` resolved it points at the installed binary.
