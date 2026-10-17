#!/usr/bin/env bash
# Self-contained tests for tools/publish-check.sh. Each case builds a fresh temp git repo with
# synthetic files only. Usage: bash tools/tests/publish-check.t.sh
set -u
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
base=$(mktemp -d "${TMPDIR:-/tmp}/pubcheck.XXXXXX") || exit 2
trap 'rm -rf "$base"' EXIT
bad=0; n=0; out=""; rc=0

new() {  # fresh repo in $r with the script installed
  n=$((n+1)); r="$base/r$n"; mkdir -p "$r/tools"
  cp "$here/tools/publish-check.sh" "$r/tools/"
  git -C "$r" init -q -b main
  git -C "$r" config user.name t; git -C "$r" config user.email t@example.invalid
  cd "$r" || exit 2
}
commit() { git add -A && git -c commit.gpgsign=false -c core.quotePath=false commit -qm "${1:-x}"; }
run() { out=$(bash tools/publish-check.sh "$@" 2>&1); rc=$?; }
check() {  # name, ok-condition result
  if [ "$2" = 1 ]; then echo "ok   $1"; else echo "FAIL $1 (rc=$rc)"; echo "$out" | sed 's/^/     | /' | head -8; bad=1; fi
}
has() { [[ $'\n'$out == *"$1"* ]] && echo 1 || echo 0; }

new; echo 'int main(){}' > a.cpp; echo hi > b.md; echo 'void main(){}' > c.frag; commit
run; check "a clean repo" $([ $rc = 0 ] && [ "$(has $'\nFAIL:')$(has $'\nWARN:')" = 00 ] && echo 1 || echo 0)

new; echo x > game.d64; commit; run
check "b tracked .d64 fails" $([ $rc = 1 ] && [ "$(has game.d64)" = 1 ] && echo 1 || echo 0)

new; echo x > 'dollar$(touch pwned)'; echo y > 'back`touch pwned`tick'; commit; run
check "c injection names inert" $([ ! -e pwned ] && [ ! -e 'dollar$(touch pwned)'pwned ] && [ $rc = 0 ] && echo 1 || echo 0)

new; : > 'sp ace'; : > 'dq"uote'; : > "sq'uote"; : > $'new\nline'; : > 'ünï-çødé.txt'; : > ./-dash; commit; run
check "d odd names handled" $([ $rc = 0 ] && [ "$(has ERROR)" = 0 ] && echo 1 || echo 0)

new; echo x > kernal; commit; run
check "e kernal fails" $([ $rc = 1 ] && echo 1 || echo 0)

new; head -c 600000 /dev/zero | tr '\0' a > big.txt; commit; run
check "f 600 KB fails" $([ $rc = 1 ] && [ "$(has 512)" = 1 ] && echo 1 || echo 0)

new; printf '\x7fELF\x02\x01rest' > prog; commit; run
check "g ELF magic fails" $([ $rc = 1 ] && [ "$(has ELF)" = 1 ] && echo 1 || echo 0)

new; printf 'PK\x03\x04zz' > a.txt; printf 'MZ\x90\x00' > b.txt; printf '\x1f\x8bzz' > c.txt; commit; run
check "g2 zip/PE/gzip magic fail" $([ $rc = 1 ] && [ "$(echo "$out" | grep -c '^FAIL:')" -ge 3 ] && echo 1 || echo 0)

new; echo 'unsigned char d[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};' > t.cpp; commit; run
check "h byte array warns" $([ $rc = 0 ] && [ "$(has 'WARN:')" = 1 ] && echo 1 || echo 0)

new; echo 'int x[] = {1, 2, 3};' > t.cpp; commit; run
check "i no byte array no warn" $([ $rc = 0 ] && [ "$(has 'WARN:')" = 0 ] && echo 1 || echo 0)

new; echo x > x.nes; commit; git rm -q x.nes; commit rm
run; r1=$rc; run --history
check "j history" $([ $r1 = 0 ] && [ $rc = 1 ] && [ "$(has x.nes)" = 1 ] && echo 1 || echo 0)

new; head -c 600000 /dev/zero | tr '\0' a > big.txt; commit; git rm -q big.txt; commit rm
run; r1=$rc; run --history
check "j2 history big blob" $([ $r1 = 0 ] && [ $rc = 1 ] && [ "$(has 'blob > 512')" = 1 ] && echo 1 || echo 0)

new; printf -- '-----BEGIN RSA PRIVATE KEY-----\nabc\n' > k.txt; commit; run
check "k private key fails" $([ $rc = 1 ] && [ "$(has 'secrets')" = 1 ] && echo 1 || echo 0)

new; : > docs-basics.md; mkdir -p src/Basic notes; : > src/Basic/interp.c; : > notes/1541-drive.md; : > kernal_patch.c; commit; run
check "e2 basic/1541 as part of a longer name pass" $([ $rc = 0 ] && echo 1 || echo 0)

new; head -c 1024 /dev/urandom > disk.raw; head -c 1024 /dev/urandom > pic.png; commit; run
check "o raw binary warns, image does not" $([ $rc = 0 ] && [ "$(has 'disk.raw: binary')" = 1 ] && [ "$(has 'pic.png')" = 0 ] && echo 1 || echo 0)

new; ln -s /etc/passwd link; ln -s a.txt ok-link; echo a > a.txt; commit; run
check "l symlink outside fails" $([ $rc = 1 ] && [ "$(has 'outside the repo')" = 1 ] && [ "$(echo "$out" | grep -c 'outside')" = 1 ] && echo 1 || echo 0)

new; git -C "$here" archive HEAD shell crt template LICENSE | tar -x   # tracked files only, no build residue
src="$here/public/README.md"; [ -f "$src" ] || src="$here/README.md"   # the export has no public/
cp "$src" README.md; commit; run
check "m real shell/crt/template pass" $([ $rc = 0 ] && [ "$(has $'\nFAIL:')" = 0 ] && echo 1 || echo 0)

new; printf 'path /ho%sme/someone/x\n' '' > n.txt; commit; run
check "n personal path fails" $([ $rc = 1 ] && [ "$(has 'personal absolute')" = 1 ] && echo 1 || echo 0)

cd "$here" || true
[ $bad = 0 ] && echo "ALL $n repos OK" || echo "SOME CASES FAILED"
exit $bad
