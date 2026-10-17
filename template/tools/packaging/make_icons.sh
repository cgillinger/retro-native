#!/usr/bin/env bash
# make_icons.sh <square.png> <outdir> <game-id>
# From ONE square PNG (256x256 or larger, readable at 16 px) makes
#   <outdir>/hicolor/<N>x<N>/apps/<game-id>.png   N = 16 24 32 48 64 128 256 512 (Linux packages)
#   <outdir>/<game-id>.ico                        16..256 (Windows installer icon)
# Needs ImageMagick (`magick`). A source smaller than 512 is upscaled for the 512 entry (soft, but
# the hicolor theme expects the size to exist).
set -euo pipefail
src="$1" out="$2" id="$3"
[ -f "$src" ] || { echo "make_icons: no such file $src" >&2; exit 1; }
for n in 16 24 32 48 64 128 256 512; do
  d="$out/hicolor/${n}x${n}/apps"; mkdir -p "$d"
  magick "$src" -background none -resize "${n}x${n}" -gravity center -extent "${n}x${n}" "PNG32:$d/$id.png"
done
magick "$src" -background none -define icon:auto-resize=256,128,64,48,32,24,16 "$out/$id.ico"
