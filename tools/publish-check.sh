#!/usr/bin/env bash
# Refuses publication if the tree contains game data, derived blobs, save states
# or personal paths. Run from the repo root before every public push.
set -u
fail=0
bad_ext='\.(d64|d71|d81|g64|t64|tap|prg|crt|smc|sfc|fig|swc|nes|gb|gbc|gba|smd|gen|bin|rom|dat|iso|cue|adf|ipf|state|sav|srm|ss[0-9])$'
files=$(git ls-files)
# incoming/ must stay ignored except its README
if git ls-files incoming | grep -v "^incoming/README.md$"; then echo "FAIL: tracked files in incoming/"; fail=1; fi
if echo "$files" | grep -iE "$bad_ext"; then
  echo "FAIL: tracked files with game-data/save extensions (above)"; fail=1
fi
if echo "$files" | grep -iE '(^|/)(data\.dat|ds_static|exe_static|kernal|basic|chargen|1541|dos1541)'; then
  echo "FAIL: tracked files that look like game data or system ROMs (above)"; fail=1
fi
big=$(git ls-files -z | xargs -0 -I{} sh -c 'test -f "{}" && [ $(stat -c%s "{}") -gt 524288 ] && echo "{}"')
if [ -n "$big" ]; then echo "$big"; echo "FAIL: files > 512 KB (above) — check they are not data"; fail=1; fi
if git grep -nE '/home/[a-z]+/|Dokument/Github' -- . ':!tools/publish-check.sh'; then
  echo "FAIL: personal absolute paths (above)"; fail=1
fi
if git grep -nIE '\.incbin|0x[0-9a-fA-F]{2}, 0x[0-9a-fA-F]{2}, 0x[0-9a-fA-F]{2}, 0x[0-9a-fA-F]{2}, 0x[0-9a-fA-F]{2}, 0x[0-9a-fA-F]{2}, 0x[0-9a-fA-F]{2}, 0x[0-9a-fA-F]{2}' -- ':!*.md' ':!tools/publish-check.sh' | grep -v 'incbin.*@' | head -20; then
  echo "WARN: embedded byte arrays / incbin found (above) — verify none carry game data"
fi
[ $fail -eq 0 ] && echo "OK: nothing forbidden found" || exit 1
