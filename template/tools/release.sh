#!/usr/bin/env bash
# Release packaging for an SDL game: Linux binary + .deb + .rpm (podman ubuntu:22.04 / rpmbuild)
# and Windows .exe + Setup.exe (mingw cross + NSIS). Game-agnostic; everything game-specific is in
# tools/release.conf. See tools/README.md.
#
#   tools/release.sh                run every step (needs podman, rpmbuild, makensis, mingw, magick)
#   tools/release.sh linux          source export + icons + container build + .deb
#   tools/release.sh repack         re-stage + .deb from the already built binary (no compile); then rpm, dist
#   tools/release.sh rpm            .rpm from the staged tree of `linux`
#   tools/release.sh windows        mingw cross build + DLL check
#   tools/release.sh nsis           Setup.exe from the `windows` result
#   tools/release.sh smoke          run the Linux binary headless (dummy drivers) on this host
#   tools/release.sh dist           copy the results to dist/
#   tools/release.sh verify         install tests of .deb/.rpm in throw-away containers
#   steps can be combined: tools/release.sh linux rpm
# Do NOT wrap this script in heavy.sh: it calls heavy.sh itself for every heavy step.
# The result in dist/ contains the copyrighted game data (embedded): never publish it.
set -euo pipefail

TOOLS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKG="$TOOLS/packaging"
GAME_ROOT="$(cd "$TOOLS/.." && pwd)"
CONF="${RELEASE_CONF:-$TOOLS/release.conf}"
. "$PKG/lib.sh"
[ -f "$CONF" ] || die "missing $CONF (copy release.conf.example and fill it in)"

JOBS="${JOBS:-4}"; LOAD="${LOAD:-8}"
[ "$JOBS" -le 4 ] || die "JOBS=$JOBS: the machine rule is -j4 -l8 at most"
# heavy.sh lives in the retro-native checkout: $RETRO_NATIVE, else a sibling of the game repo
# (../retro-native), else the checkout that provides third_party/retro-native.
if [ -z "${HEAVY:-}" ]; then
  for c in "${RETRO_NATIVE:+$RETRO_NATIVE/tools/heavy.sh}" "$GAME_ROOT/../retro-native/tools/heavy.sh" \
           "$GAME_ROOT/third_party/retro-native/tools/heavy.sh" "$GAME_ROOT/shell/../tools/heavy.sh"; do
    [ -n "$c" ] && [ -x "$c" ] && { HEAVY="$c"; break; }
  done
fi
HEAVY="${HEAVY:-heavy.sh}"
[ -x "$HEAVY" ] || die "heavy.sh not found at $HEAVY (set HEAVY=...)"
heavy() { "$HEAVY" "$@"; }
# every container gets its own memory cap: heavy.sh's systemd scope does NOT cover rootless podman
PODMAN_MEM=(--memory="${PODMAN_MEM_LIMIT:-14g}" --memory-swap="${PODMAN_SWAP_LIMIT:-15g}")   # gaming mode: 8g / 9g
# Fedora has SELinux: bind mounts need label=disable (NOT :Z, which would relabel the user's repo files)

SMOKE_ENV=(); SMOKE_PATTERN="level init complete"; SMOKE_USER_DIR_VAR=V2_USER_DIR; DATA_FILES=""
. "$CONF"
: "${GAME_ID:?}" "${GAME_NAME:?}" "${BIN_NAME:?}" "${ICON_PNG:?}" "${DESCRIPTION:?}" "${MAINTAINER:?}"
: "${CATEGORIES:?}" "${LINUX_BUILD_CMD:?}" "${LINUX_OUT:?}"
PUBLISHER="${PUBLISHER:-$MAINTAINER}"
DESCRIPTION_LONG="${DESCRIPTION_LONG:-$DESCRIPTION}"
DATA_SRC="$(cd "${DATA_SRC:-$GAME_ROOT}" && pwd)"
SETUP_NAME="${SETUP_NAME:-${GAME_NAME//[^A-Za-z0-9]/}-Setup.exe}"
# Windows folder name (install dir, Start menu folder): GAME_NAME without trailing dots/spaces,
# which Windows strips from directory names ("H.E.R.O." -> "H.E.R.O").
DIR_NAME="${DIR_NAME:-$(sed -E 's/[. ]+$//' <<<"$GAME_NAME")}"
WIN_INSTALL_EXE="${WIN_INSTALL_EXE:-$GAME_ID.exe}"
IMAGE="${IMAGE:-localhost/retro-native-release:ubuntu2204}"
WORK="${RELEASE_WORK:-$HOME/.cache/retro-native-release/$GAME_ID}"
DIST="$GAME_ROOT/dist"

# ---- version: exact tag -> tag, otherwise <last tag or 1.0>.<YYYYMMDD>.<HHMM>; conf VERSION overrides
if [ -z "${VERSION:-}" ]; then
  tag="$(git -C "$GAME_ROOT" describe --tags --abbrev=0 2>/dev/null || true)"; tag="${tag#v}"
  [[ "$tag" =~ ^[0-9]+(\.[0-9]+)*$ ]] || tag="1.0"
  if git -C "$GAME_ROOT" describe --tags --exact-match >/dev/null 2>&1; then VERSION="$tag"
  else VERSION="$tag.$(date +%Y%m%d.%H%M)"; fi   # HHMM: two builds on one day still upgrade
fi
[[ "$VERSION" =~ ^[0-9][A-Za-z0-9.+~]*$ ]] || die "VERSION '$VERSION' is not valid for deb/rpm (digits first, then [A-Za-z0-9.+~])"
VI_VERSION="$(vi_version "$VERSION")"
COMMIT="$(git -C "$GAME_ROOT" rev-parse --short HEAD)"
say "$GAME_NAME $VERSION (commit $COMMIT, VI $VI_VERSION), work dir $WORK"

SRC="$WORK/src" ICONS="$WORK/icons" OUT="$WORK/out" WINOUT="$WORK/win"
export_source() {
  say "source export of HEAD ($COMMIT) -> $SRC"
  [ -z "$(git -C "$GAME_ROOT" status --porcelain --untracked-files=no)" ] \
    || echo "NOTE: uncommitted tracked changes exist; the release builds the committed HEAD only" >&2
  rm -rf "$SRC"; mkdir -p "$SRC" "$WORK"
  git -C "$GAME_ROOT" archive HEAD | tar -x -C "$SRC"
  # Checked-out submodules only (nested ones included), each at the commit its parent recorded.
  # Uninitialised nested submodules (e.g. snes9x's unused external/*) are skipped.
  local p sha
  while read -r p sha; do
    mkdir -p "$SRC/$p"; git -C "$GAME_ROOT/$p" archive "$sha" | tar -x -C "$SRC/$p"
  done < <(git -C "$GAME_ROOT" submodule foreach --quiet --recursive 'echo "$displaypath $sha1"')
  # data must never come from the export (it is gitignored, but make sure)
  [ -z "${DATA_FILES:-}" ] || for f in $DATA_FILES; do [ ! -e "$SRC/$f" ] || die "data file $f is tracked in git"; done
}

step_linux() {
  [ -z "${DATA_FILES:-}" ] || for f in $DATA_FILES; do [ -f "$DATA_SRC/$f" ] || die "missing data file $DATA_SRC/$f"; done
  export_source
  rm -rf "$ICONS" "$OUT"; mkdir -p "$ICONS" "$OUT"
  "$PKG/make_icons.sh" "$GAME_ROOT/$ICON_PNG" "$ICONS" "$GAME_ID"
  say "container image $IMAGE (no game data in the context)"
  heavy podman build "${PODMAN_MEM[@]}" --build-arg "JOBS=$JOBS" -t "$IMAGE" -f "$PKG/Containerfile" "$PKG"
  say "container build (data mounted read-only)"
  heavy podman run --rm "${PODMAN_MEM[@]}" --security-opt label=disable \
    -e "VERSION=$VERSION" -e "JOBS=$JOBS" -e "LOAD=$LOAD" \
    -v "$SRC:/build/src:rw" -v "$DATA_SRC:/data:ro" -v "$PKG:/pkg:ro" -v "$CONF:/release.conf:ro" \
    -v "$ICONS:/icons:ro" -v "$OUT:/out:rw" \
    "$IMAGE" bash /pkg/container-build.sh
}

# packaging-only: no source export, no compile; reuses $OUT/$BIN_NAME and $ICONS of an earlier `linux`
step_repack() {
  [ -f "$OUT/$BIN_NAME" ] && [ -d "$ICONS" ] || die "run the linux step first (repack reuses its binary and icons)"
  heavy podman run --rm "${PODMAN_MEM[@]}" --security-opt label=disable \
    -e "VERSION=$VERSION" -e PACKAGE_ONLY=1 \
    -v "$DATA_SRC:/data:ro" -v "$PKG:/pkg:ro" -v "$CONF:/release.conf:ro" \
    -v "$ICONS:/icons:ro" -v "$OUT:/out:rw" \
    "$IMAGE" bash /pkg/container-build.sh
}

step_rpm() {
  [ -d "$OUT/stage" ] || die "run the linux step first"
  heavy env VERSION="$VERSION" "$PKG/make_rpm.sh" "$CONF" "$OUT/stage" "$WORK/rpmtop" "$OUT"
}

prepare_win_sdl() {
  WIN_SDL_VERSION="${WIN_SDL_VERSION:-2.32.10}"
  if [ -z "${WIN_SDL_DIR:-}" ]; then
    WIN_SDL_DIR="$HOME/.cache/retro-native-release/sdl2-mingw/SDL2-$WIN_SDL_VERSION/x86_64-w64-mingw32"
    if [ ! -f "$WIN_SDL_DIR/lib/libSDL2.a" ]; then
      say "fetching SDL2 $WIN_SDL_VERSION (mingw devel)"
      mkdir -p "$HOME/.cache/retro-native-release/sdl2-mingw"
      curl -fL -o "$HOME/.cache/retro-native-release/sdl2-mingw/sdl.tgz" \
        "https://github.com/libsdl-org/SDL/releases/download/release-$WIN_SDL_VERSION/SDL2-devel-$WIN_SDL_VERSION-mingw.tar.gz"
      tar xzf "$HOME/.cache/retro-native-release/sdl2-mingw/sdl.tgz" -C "$HOME/.cache/retro-native-release/sdl2-mingw"
    fi
  fi
  [ -f "$WIN_SDL_DIR/lib/libSDL2.a" ] || die "no libSDL2.a in WIN_SDL_DIR=$WIN_SDL_DIR"
  # the package's sdl2.pc names the builder's prefix: use a patched COPY (the SDL tree stays untouched)
  mkdir -p "$WORK/win-pc"
  sed "s|^prefix=.*|prefix=$WIN_SDL_DIR|" "$WIN_SDL_DIR/lib/pkgconfig/sdl2.pc" > "$WORK/win-pc/sdl2.pc"
  WIN_PKG_CONFIG="env PKG_CONFIG_LIBDIR=$WORK/win-pc pkg-config"
}

step_windows() {
  : "${WIN_BUILD_CMD:?conf has no WIN_BUILD_CMD}" "${WIN_OUT:?}"
  [ -d "$SRC" ] || export_source
  prepare_win_sdl
  rm -rf "$WINOUT"; mkdir -p "$WINOUT"
  if [ "${KEEP_WIN_SRC:-0}" != 1 ] || [ ! -d "$WORK/win-src" ]; then   # KEEP_WIN_SRC=1: resume an interrupted build
    rm -rf "$WORK/win-src"; cp -a "$SRC" "$WORK/win-src"       # own copy: container objects never mix with mingw objects
  fi
  heavy env WIN_SRC="$WORK/win-src" WIN_OUT_DIR="$WINOUT" DATA_DIR="$DATA_SRC" JOBS="$JOBS" LOAD="$LOAD" VERSION="$VERSION" \
    WIN_SDL_DIR="$WIN_SDL_DIR" WIN_PKG_CONFIG="$WIN_PKG_CONFIG" CONF="$CONF" "$PKG/win-build.sh"
}

step_nsis() {
  [ -f "$WINOUT/$GAME_ID.exe" ] || die "run the windows step first"
  local kb=$(( ($(stat -c %s "$WINOUT/$GAME_ID.exe") + 1023) / 1024 + 64 ))
  heavy makensis -V2 -INPUTCHARSET UTF8 \
    -DGAME_ID="$GAME_ID" -DGAME_NAME="$GAME_NAME" -DDIR_NAME="$DIR_NAME" -DVERSION="$VERSION" -DVI_VERSION="$VI_VERSION" \
    -DPUBLISHER="$PUBLISHER" -DWIN_EXE_NAME="$WIN_INSTALL_EXE" -DSETUP_NAME="$WINOUT/$SETUP_NAME" \
    -DEXE_SRC="$WINOUT/$GAME_ID.exe" -DICO_SRC="$ICONS/$GAME_ID.ico" -DESTIMATED_SIZE_KB="$kb" \
    "$PKG/nsis/installer.nsi"
  ls -l "$WINOUT/$SETUP_NAME"
}

step_smoke() {
  local bin="$OUT/$BIN_NAME" t; [ -f "$bin" ] || die "run the linux step first"
  t="$(mktemp -d "$WORK/smoke.XXXXXX")"; cp "$bin" "$t/"
  say "smoke: $BIN_NAME with dummy drivers"
  # shellcheck disable=SC2154
  (cd "$t" && heavy env "${SMOKE_ENV[@]}" "$SMOKE_USER_DIR_VAR=$t/user" SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
     timeout "${SMOKE_SECONDS:-8}" "./$BIN_NAME" >"$t/log" 2>&1 || true)
  grep -q "$SMOKE_PATTERN" "$t/log" && echo "smoke OK ('$SMOKE_PATTERN')" || { tail -20 "$t/log"; die "smoke FAILED"; }
  rm -rf "$t"
}

step_dist() {
  mkdir -p "$DIST"
  rm -f "$DIST/${GAME_ID}_"*_amd64.deb "$DIST/${GAME_ID}-"*.rpm
  cp "$OUT/$BIN_NAME" "$DIST/"
  cp "$OUT/${GAME_ID}_"*"_amd64.deb" "$OUT/${GAME_ID}-"*.rpm "$DIST/"
  if [ -f "$WINOUT/$GAME_ID.exe" ]; then cp "$WINOUT/$GAME_ID.exe" "$DIST/"; fi
  if [ -f "$WINOUT/$SETUP_NAME" ]; then cp "$WINOUT/$SETUP_NAME" "$DIST/"; fi
  ls -l "$DIST"
}

step_verify() { heavy "$PKG/verify.sh" "$CONF" "$OUT" "${PODMAN_MEM[*]}"; }

steps=("$@"); [ ${#steps[@]} -gt 0 ] || steps=(linux rpm windows nsis smoke dist)
[ ${#steps[@]} -ne 1 ] || [ "${steps[0]}" != all ] || steps=(linux rpm windows nsis smoke dist)
for s in "${steps[@]}"; do
  case "$s" in linux|repack|rpm|windows|nsis|smoke|dist|verify) "step_$s" ;; *) die "unknown step $s" ;; esac
done
say "done"
