#!/usr/bin/env bash
# check_deps.sh linux <binary> [max-glibc]   — only glibc-family NEEDED libs, GLIBC_ symbols <= max (default 2.35)
# check_deps.sh windows <exe> "<dll|dll|...>" — imports only from the allow-listed system DLLs
set -euo pipefail
mode="$1" bin="$2"
case "$mode" in
linux)
  max="${3:-2.35}"
  needed="$(objdump -p "$bin" | awk '/NEEDED/ {print $2}')"
  echo "NEEDED: $(echo $needed)"
  bad="$(grep -v -E '^(libc\.so\.6|libm\.so\.6|libdl\.so\.2|libpthread\.so\.0|librt\.so\.1|ld-linux-x86-64\.so\.2)$' <<<"$needed" || true)"
  [ -z "$bad" ] || { echo "FAIL: non-glibc dependency: $bad" >&2; exit 1; }
  top="$(objdump -T "$bin" | grep -o 'GLIBC_[0-9][0-9.]*' | sed 's/GLIBC_//' | sort -uV | tail -1)"
  echo "max GLIBC_ symbol version: ${top:-none} (limit $max)"
  [ -z "$top" ] || [ "$(printf '%s\n%s\n' "$top" "$max" | sort -V | tail -1)" = "$max" ] \
    || { echo "FAIL: needs GLIBC_$top > $max" >&2; exit 1; }
  echo "OK: glibc only"
  ;;
windows)
  allow="${3:-advapi32|gdi32|imm32|kernel32|msvcrt|ole32|oleaut32|setupapi|shell32|user32|version|winmm|ws2_32}"
  dlls="$(x86_64-w64-mingw32-objdump -p "$bin" | awk '/DLL Name/ {print $3}')"
  echo "DLLs: $(echo $dlls)"
  extra="$(grep -v -i -E "^($allow)\.dll$" <<<"$dlls" || true)"
  [ -z "$extra" ] || { echo "FAIL: extra DLL dependencies: $extra" >&2; exit 1; }
  echo "OK: system DLLs only"
  ;;
*) echo "usage: $0 linux|windows <file> [limit]" >&2; exit 2 ;;
esac
