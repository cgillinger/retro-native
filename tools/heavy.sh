#!/usr/bin/env bash
# Runs a heavy command (compile, packaging, long emulator test) under a machine-wide lock so
# several jobs on the same computer never build at the same time.
# Usage: tools/heavy.sh <command> [args...]
lock="${XDG_RUNTIME_DIR:-/tmp}/retro-native-heavy.lock"
exec 9>"$lock"
if ! flock -n 9; then
  echo "heavy.sh: waiting for another heavy job ($lock)..." >&2
  flock 9
fi
# Hard memory cap: if the job outgrows it, the kernel kills the job (the build fails) instead of
# the whole desktop freezing in swap. HEAVY_MEM / HEAVY_SWAP override (systemd units, e.g. 20G).
mem="${HEAVY_MEM:-16G}" swap="${HEAVY_SWAP:-1G}"
if command -v systemd-run >/dev/null && systemd-run --user --scope -q true 2>/dev/null; then
  exec systemd-run --user --scope -q -p MemoryMax="$mem" -p MemorySwapMax="$swap" nice -n 10 "$@"
fi
echo "heavy.sh: systemd-run --user unavailable, running WITHOUT memory cap" >&2
exec nice -n 10 "$@"
