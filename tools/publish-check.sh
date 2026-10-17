#!/usr/bin/env bash
# Usage: bash tools/publish-check.sh [--history]     (run from inside the repo)
# Checks the files git tracks, with their content as it is in the working tree (identical to the
# index in the export and in CI). Submodules (gitlinks) are not inspected.
# Refuses publication if the tree contains game data, derived blobs, executables/archives,
# save states, obvious secrets or personal paths. --history also scans every commit reachable
# from --all (file names and blob sizes); it only reports, history is never rewritten.
# Output: FAIL: = blocking (exit 1), WARN: = review manually (exit 0), ERROR: = a check could
# not run (exit 2). No automatic check can guarantee that nothing sensitive slips through.
set -u
LC_ALL=C; export LC_ALL
fails=0; warns=0
fail() { echo "FAIL: $*"; fails=$((fails+1)); }
warn() { echo "WARN: $*"; warns=$((warns+1)); }
err()  { echo "ERROR: $*" >&2; rm -rf "${tmp:-}"; exit 2; }
hist=0
case "${1:-}" in --history) hist=1 ;; "") ;; *) echo "usage: $0 [--history]" >&2; exit 2 ;; esac

tmp=$(mktemp -d) || err "mktemp failed"
trap 'rm -rf "$tmp"' EXIT
root=$(git rev-parse --show-toplevel) || err "not inside a git repository"
root=$(realpath -- "$root") && cd -- "$root" || err "cannot enter repo root"

limit=524288
bad_ext='\.(d64|d71|d81|g64|t64|tap|prg|crt|smc|sfc|fig|swc|nes|gb|gbc|gba|smd|gen|bin|rom|dat|iso|cue|adf|ipf|state|sav|srm|ss[0-9])$'
bad_rom='(^|/)(data\.dat|ds_static|exe_static|kernal|basic|chargen|1541|dos1541)(\.[a-z0-9]+)?$'
img_ext='\.(png|jpe?g|gif|ico|webp|svg)$'
secrets='-----BEGIN [A-Z ]*PRIVATE KEY-----|ghp_[A-Za-z0-9]{36}|AKIA[0-9A-Z]{16}|xox[baprs]-[0-9A-Za-z-]+'
self=(':!tools/publish-check.sh' ':!tools/tests/publish-check.t.sh')

# Name-based checks for one path (case-insensitive); prints the reason, if any.
name_issue() {
  shopt -s nocasematch
  case $1 in incoming/README.md) ;; incoming/*) echo "tracked file in incoming/" ;; esac
  [[ $1 =~ $bad_ext ]] && echo "game-data/save extension"
  [[ $1 =~ $bad_rom ]] && echo "looks like game data or a system ROM"
  shopt -u nocasematch
}
# Echo tool output indented, so a line can never be read as a CI workflow command.
show() { printf '%s\n' "$1" | sed 's/^/  /'; }

# git grep wrapper: output in $gg; exit 1 = no match (fine), >= 2 = check could not run.
gg() { gg=$(git grep "$@"); local rc=$?; [ $rc -ge 2 ] && err "git grep failed (rc=$rc)"; return 0; }

git ls-files -z > "$tmp/files" || err "git ls-files failed"
while IFS= read -r -d '' f; do
  q=$(printf '%q' "$f"); p="./$f"
  while IFS= read -r why; do [ -n "$why" ] && fail "$q: $why"; done < <(name_issue "$f")
  if [ -L "$p" ]; then   # never follow symlinks; they must not leave the repo
    t=$(realpath -m -- "$p") || err "realpath failed for $q"
    case $t in "$root"/*) ;; *) fail "$q: symlink points outside the repo ($t)" ;; esac
    continue
  fi
  [ -f "$p" ] || continue
  size=$(stat -c%s -- "$p") || err "stat failed for $q"
  [ "$size" -gt $limit ] && fail "$q: $size bytes > 512 KB (check it is not data)"
  magic=$(head -c 8 -- "$p" | od -An -tx1 | tr -d ' \n')
  case $magic in
    7f454c46*)       fail "$q: ELF binary" ;;
    4d5a*)           fail "$q: PE/DOS executable (MZ)" ;;
    504b0304*)       fail "$q: zip/jar archive" ;;
    1f8b*)           fail "$q: gzip data" ;;
    377abcaf271c*)   fail "$q: 7z archive" ;;
    526172211a07*)   fail "$q: rar archive" ;;
    fd377a585a00*)   fail "$q: xz data" ;;
  esac
  # Non-text content without a recognised header (a raw disk/ROM image is just bytes).
  if [ "$size" -gt 0 ] && ! grep -Iq -- . "$p" && ! [[ ${f,,} =~ $img_ext ]]; then
    warn "$q: binary file that is not an image - verify it is not game data"
  fi
done < "$tmp/files"

gg -nIE -e '/home/[^/ ]+/' -e '/Users/[^/ ]+/' -e 'Dokument/Github' --cached -- . "${self[@]}"
[ -n "$gg" ] && { show "$gg"; fail "personal absolute paths (above)"; }

gg -lIE --cached -e "$secrets" -- . "${self[@]}"   # -l: never echo the secret itself
[ -n "$gg" ] && { show "$gg"; fail "possible secrets/private keys in the files above"; }

b='0x[0-9a-fA-F]{2}'
gg -nIE --cached -e "\.incbin|$b, $b, $b, $b, $b, $b, $b, $b" -- . ':!*.md' "${self[@]}"
gg=$(printf '%s' "$gg" | grep -v 'incbin.*@' | head -20)
[ -n "$gg" ] && { show "$gg"; warn "embedded byte arrays / incbin (above) - verify none carry game data"; }

if [ $hist -eq 1 ]; then
  git log --all -m --name-only --format= -z > "$tmp/hn" || err "git log failed"
  sort -zu "$tmp/hn" > "$tmp/hs" || err "sort failed"
  while IFS= read -r -d '' f; do
    [ -n "$f" ] || continue
    while IFS= read -r why; do [ -n "$why" ] && fail "history: $(printf '%q' "$f"): $why"; done < <(name_issue "$f")
  done < "$tmp/hs"
  git rev-list --objects --all > "$tmp/ro" || err "git rev-list failed"
  git cat-file --batch-check='%(objecttype) %(objectsize) %(rest)' < "$tmp/ro" > "$tmp/bc" || err "git cat-file failed"
  awk -v l=$limit '$1=="blob" && $2>l {sub(/^blob [0-9]+ /,""); print}' "$tmp/bc" > "$tmp/big" || err "awk failed"
  while IFS= read -r f; do fail "history: $(printf '%q' "$f"): blob > 512 KB"; done < "$tmp/big"
fi

if [ $fails -gt 0 ]; then echo "SUMMARY: $fails FAIL, $warns WARN - NOT ready to publish"; exit 1; fi
echo "SUMMARY: OK, $warns WARN - nothing forbidden found (not a guarantee)"
