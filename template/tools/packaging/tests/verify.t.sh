#!/usr/bin/env bash
# Tests for verify.sh with a fake podman (no containers are started).
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
verify="$here/../verify.sh"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/verify-t.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT

mkdir -p "$tmp/fakebin" "$tmp/out"
cat > "$tmp/fakebin/podman" <<'EOS'
#!/usr/bin/env bash
# Ignores its arguments; prints canned marker lines chosen by FAKE_PODMAN_MODE.
case "${FAKE_PODMAN_MODE:-ok}" in
  noimage) echo "Error: unable to pull image" >&2; exit 125 ;;
  installfail) echo CONTAINER-UP; echo "dpkg: dependency problems"; exit 100 ;;
  smokefail) echo CONTAINER-UP; echo SMOKE-FAIL ;;
  *) echo CONTAINER-UP ;;
esac
case "${FAKE_PODMAN_MODE:-ok}" in
  smokefail) ;;
  *) echo SMOKE-OK ;;
esac
case "${FAKE_PODMAN_MODE:-ok}" in
  shortcutfail) echo SHORTCUT-FAIL ;;
  *) echo SHORTCUT-OK ;;
esac
echo SHORTCUT-NOT-RECREATED; echo SHORTCUT-REMOVED
[ "${FAKE_PODMAN_MODE:-ok}" = noremove ] || echo REMOVE-OK
echo RPM-VERIFY-OK
EOS
printf '#!/bin/sh\nexit 0\n' > "$tmp/fakebin/desktop-file-validate"
chmod +x "$tmp/fakebin/"*
cat > "$tmp/test.conf" <<'EOS'
GAME_ID=tgame
BIN_NAME=tgame
SMOKE_PATTERN="ok"
SMOKE_ENV=()
EOS
: > "$tmp/out/tgame_1_amd64.deb"; : > "$tmp/out/tgame-1.rpm"

# Minimal PATH without podman for the "absent" case (symlinks to the tools verify.sh needs)
mkdir -p "$tmp/nopodman"
for t in bash env ls head basename dirname cat tee grep rm sed; do ln -s "$(command -v $t)" "$tmp/nopodman/$t"; done

failed=0
run_case() { # run_case <name> <mode|ABSENT> <expected exit> <expected output regex...>
  local name="$1" mode="$2" want="$3" rc o pathv; shift 3
  if [ "$mode" = ABSENT ]; then pathv="$tmp/nopodman"; else pathv="$tmp/fakebin:$PATH"; fi
  o="$(PATH="$pathv" FAKE_PODMAN_MODE="$mode" bash "$verify" "$tmp/test.conf" "$tmp/out" "" 2>&1)"; rc=$?
  local ok=1 re
  [ "$rc" = "$want" ] || ok=0
  for re in "$@"; do grep -qE "$re" <<<"$o" || ok=0; done
  if [ $ok = 1 ]; then echo "PASS: $name (exit $rc)"; else echo "FAIL: $name (exit $rc, want $want)"; echo "$o" | sed 's/^/    /'; failed=1; fi
}

run_case "all markers ok"      ok          0 'deb ubuntu:24.04: PASS' 'deb ubuntu:22.04: PASS' 'rpm fedora: PASS'
run_case "smoke fails"         smokefail   1 'deb ubuntu:24.04: FAIL' 'deb ubuntu:22.04: FAIL' 'rpm fedora: FAIL'
run_case "shortcut fails"      shortcutfail 1 'deb ubuntu:24.04: FAIL' 'rpm fedora: FAIL'
run_case "missing REMOVE-OK"   noremove    1 'deb ubuntu:24.04: FAIL' 'rpm fedora: FAIL'
run_case "install fails (no markers)" installfail 1 'deb ubuntu:24.04: FAIL' 'rpm fedora: FAIL'
run_case "podman exits 125"    noimage     2 'deb ubuntu:24.04: NOT TESTED' 'rpm fedora: NOT TESTED'
mv "$tmp/out/tgame_1_amd64.deb" "$tmp/out/x.deb.off"
run_case "missing .deb"        ok          1 'deb ubuntu:24.04: FAIL' 'rpm fedora: PASS'
mv "$tmp/out/x.deb.off" "$tmp/out/tgame_1_amd64.deb"
run_case "podman absent"       ABSENT      2 'deb ubuntu:24.04: NOT TESTED' 'rpm fedora: NOT TESTED'
exit $failed
