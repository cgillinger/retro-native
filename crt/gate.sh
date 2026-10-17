#!/usr/bin/env bash
# Screenshot gate: renders Sharp / CRT / CRT+mask at 1080p and 1440p for review.
# Usage: gate.sh <exe> <outdir> [frames...] [-- extra exe args]
# Contract on <exe>: --frames N, --out-size WxH, --shader sharp|crt|crtmask, --screenshot FILE.
# Needs a real GL context (not SDL dummy video). Fails if an image is missing or not WxH.
set -euo pipefail
[ $# -ge 2 ] || { echo "usage: $0 <exe> <outdir> [frames...] [-- extra exe args]" >&2; exit 2; }
EXE="$1"; OUT="$2"; shift 2
FRAMES=()
while [ $# -gt 0 ] && [ "$1" != "--" ]; do FRAMES+=("$1"); shift; done
[ "${1:-}" = "--" ] && shift
EXTRA=("$@")
[ ${#FRAMES[@]} -gt 0 ] || FRAMES=(300 1500)
mkdir -p "$OUT"
LIST="$OUT/run_files.txt"; : > "$LIST"
for f in "${FRAMES[@]}"; do
  for size in 1920x1080 2560x1440; do
    h="${size#*x}"
    for sh in sharp crt crtmask; do
      img="$OUT/${f}_${h}p_${sh}.bmp"
      rm -f "$img"
      "$EXE" --frames "$f" --out-size "$size" --shader "$sh" --screenshot "$img" ${EXTRA[@]+"${EXTRA[@]}"}
      [ -s "$img" ] || { echo "FAIL: $img not written" >&2; exit 1; }
      echo "$size $f $sh $img" >> "$LIST"
    done
  done
done
python3 - "$LIST" "$OUT" <<'PY'
import sys, os
try:
    from PIL import Image
    import numpy as np
except ImportError:
    print("PIL/numpy not available: BMPs written, size check and luminance skipped")
    sys.exit(0)
rows = [l.split() for l in open(sys.argv[1])]
out = sys.argv[2]
LW = np.array([0.2126, 0.7152, 0.0722])
res = {}
fail = False
for size, f, sh, path in rows:
    W, H = map(int, size.split("x"))
    im = Image.open(path).convert("RGB")
    if im.size != (W, H):
        print(f"FAIL: {path} is {im.size[0]}x{im.size[1]}, expected {size}"); fail = True
        continue
    base = path[:-4]
    im.save(base + ".png")
    cw, ch = 480, 270                       # 1:1 centre crop, no resampling
    im.crop(((W - cw) // 2, (H - ch) // 2, (W + cw) // 2, (H + ch) // 2)).save(base + "_crop.png")
    a = np.asarray(im, dtype=float) / 255.0
    res[(size, f, sh)] = (a, path)
lines = ["files of this run:"] + [f"  {r[3]}" for r in rows]
lines.append("\nmean LINEAR luminance over the picture rectangle (mean of (v/255)^2.2), CRT vs Sharp, +-10 % criterion:")
for (size, f, sh), (a, path) in sorted(res.items()):
    if sh == "sharp":
        continue
    sa = res.get((size, f, "sharp"))
    if sa is None:
        continue
    # Picture rectangle = bounding box of the non-black pixels of the Sharp and CRT images
    # (the letterbox bars are exactly black, so they never contribute energy).
    nz = np.argwhere((sa[0].max(axis=2) > 0) | (a.max(axis=2) > 0))
    if len(nz) == 0:
        lines.append(f"  {f:>5}f {size:10s} {sh:8s} picture is black, skipped"); continue
    y0, x0 = nz.min(axis=0); y1, x1 = nz.max(axis=0) + 1
    def lin(img): return float((img[y0:y1, x0:x1] ** 2.2 @ LW).mean())
    ls, lc = lin(sa[0]), lin(a)
    ratio = lc / ls if ls > 0 else 1.0
    ok = abs(ratio - 1.0) <= 0.10 or ls < 1e-3
    fail |= not ok
    lines.append(f"  {f:>5}f {size:10s} {sh:8s} sharp={ls:.4f} crt={lc:.4f} ratio={ratio:.3f} {'PASS' if ok else 'FAIL'}")
txt = "\n".join(lines)
open(os.path.join(out, "index.txt"), "w").write(txt + "\n")
print(txt)
sys.exit(1 if fail else 0)
PY
