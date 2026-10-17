#!/usr/bin/env bash
# Host side of the Windows cross build (started by release.sh through heavy.sh).
# Env: WIN_SRC (clean export copy) WIN_OUT_DIR DATA_DIR JOBS LOAD WIN_SDL_DIR WIN_PKG_CONFIG CONF
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
. "$here/lib.sh"
. "$CONF"
cd "$WIN_SRC"
say "build: $WIN_BUILD_CMD"
eval "$WIN_BUILD_CMD"
mkdir -p "$WIN_OUT_DIR"
x86_64-w64-mingw32-strip -o "$WIN_OUT_DIR/$GAME_ID.exe" "$WIN_OUT"
"$here/check_deps.sh" windows "$WIN_OUT_DIR/$GAME_ID.exe" "${WIN_DLL_ALLOW:-}"
