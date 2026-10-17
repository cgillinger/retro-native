#!/usr/bin/env bash
# verify.sh <conf> <outdir> "<podman memory flags>"
# Installs the .deb in ubuntu:24.04 and ubuntu:22.04 and the .rpm in fedora, runs the smoke test
# of the INSTALLED binary, then removes the package. Throw-away containers, nothing touches the host.
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
. "$here/lib.sh"
conf="$1" out="$2" mem=($3)
. "$conf"
SMOKE_PATTERN="${SMOKE_PATTERN:-level init complete}"
SMOKE_USER_DIR_VAR="${SMOKE_USER_DIR_VAR:-V2_USER_DIR}"
envs=""; for e in "${SMOKE_ENV[@]:-}"; do [ -z "$e" ] || envs+=" $e"; done
smoke="mkdir -p /tmp/u && env$envs $SMOKE_USER_DIR_VAR=/tmp/u SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout ${SMOKE_SECONDS:-8} /usr/games/$BIN_NAME > /tmp/smoke.log 2>&1 || true; grep -q '$SMOKE_PATTERN' /tmp/smoke.log && echo SMOKE-OK || { echo SMOKE-FAIL; tail -20 /tmp/smoke.log; }"
# install-time desktop shortcut: a user without a session, served through the test override
VH=/srv/vtest   # home of the throwaway test user inside the container
sc_pre="useradd -m -d $VH -u 1500 vtest; mkdir -p $VH/Desktop; chown vtest $VH/Desktop; export RN_SHORTCUT_USERS=vtest"
sc_chk="test -x $VH/Desktop/$GAME_ID.desktop && stat -c %U $VH/Desktop/$GAME_ID.desktop | grep -qx vtest && echo SHORTCUT-OK || echo SHORTCUT-FAIL"
sc_gone="rm -f $VH/Desktop/$GAME_ID.desktop"
sc_nore='test ! -e '"$VH/Desktop/$GAME_ID"'.desktop && echo SHORTCUT-NOT-RECREATED || echo SHORTCUT-RECREATED'
sc_rm="test ! -e $VH/Desktop/$GAME_ID.desktop && echo SHORTCUT-REMOVED || echo SHORTCUT-NOT-REMOVED"
fail=0
deb="$(ls "$out/${GAME_ID}_"*_amd64.deb | head -1)"; rpm="$(ls "$out/${GAME_ID}-"*.rpm | head -1)"
for img in docker.io/library/ubuntu:24.04 docker.io/library/ubuntu:22.04; do
  say "deb on $img"
  podman run --rm "${mem[@]}" --security-opt label=disable -v "$deb:/tmp/pkg.deb:ro" -e DEBIAN_FRONTEND=noninteractive "$img" bash -c "
    set -e; apt-get update -qq >/dev/null; cp /tmp/pkg.deb /tmp/p.deb
    $sc_pre
    apt-get install -y /tmp/p.deb 2>&1 | tail -5
    dpkg -L $GAME_ID; dpkg -s $GAME_ID | grep -E '^(Status|Version|Depends)'
    desktop-file-validate /usr/share/applications/$GAME_ID.desktop 2>/dev/null || true
    $sc_chk
    $sc_gone; apt-get install -y --reinstall /tmp/p.deb >/dev/null 2>&1; $sc_nore
    $smoke
    apt-get remove -y $GAME_ID >/dev/null 2>&1; $sc_rm; test ! -e /usr/games/$BIN_NAME && echo REMOVE-OK
    $( [ "$img" = docker.io/library/ubuntu:24.04 ] && echo "apt-get install -y lintian >/dev/null 2>&1 && lintian /tmp/pkg.deb || true" )
  " 2>&1 | tee "$out/verify-$(basename "$img").log" | grep -E 'SMOKE|SHORTCUT|REMOVE|Status|Depends|Version|^/|lintian|^[EWIPX]:' ; true
done
say "rpm on fedora"
for img in docker.io/library/fedora:44 docker.io/library/fedora:latest; do
  podman run --rm "${mem[@]}" --security-opt label=disable -v "$rpm:/tmp/pkg.rpm:ro" "$img" bash -c "
    set -e; cat /etc/fedora-release; cp /tmp/pkg.rpm /tmp/p.rpm
    dnf install -y util-linux shadow-utils >/dev/null 2>&1 || true
    $sc_pre
    dnf install -y /tmp/p.rpm 2>&1 | tail -4
    rpm -ql $GAME_ID | head -20; rpm -V $GAME_ID && echo RPM-VERIFY-OK
    $sc_chk
    $sc_gone; rpm -Uvh --replacepkgs /tmp/p.rpm >/dev/null 2>&1; $sc_nore
    $smoke
    dnf remove -y $GAME_ID 2>&1 | tail -2; $sc_rm; test ! -e /usr/games/$BIN_NAME && echo REMOVE-OK
  " 2>&1 | tee "$out/verify-$(basename "$img").log" | grep -E 'fedora|SMOKE|SHORTCUT|REMOVE|RPM-VERIFY|^/|Installed|Removed|Error' && break
done
exit $fail
