# Shared helpers for the release scripts (sourced, never executed).

die() { echo "release: $*" >&2; exit 1; }
say() { echo "== $*"; }

# render <template> <out> VAR...: replaces @VAR@ with the value of shell variable VAR
render() {
  local tpl="$1" out="$2" content v; shift 2
  content="$(cat "$tpl")"
  for v in "$@"; do content="${content//@$v@/${!v-}}"; done
  if grep -q '@[A-Z_]*@' <<<"$content"; then
    echo "render: unreplaced token in $tpl: $(grep -o '@[A-Z_]*@' <<<"$content" | sort -u | tr '\n' ' ')" >&2; return 1
  fi
  printf '%s\n' "$content" > "$out"
}

# Debian long description: every line indented by one space, empty lines become " ."
deb_long_desc() { sed -e 's/[[:space:]]*$//' -e 's/^$/./' -e 's/^/ /' <<<"$1"; }

# Windows VIProductVersion needs four numbers <= 65535: version parts that are too large
# (a date like 20261017) are replaced by the build date's MMDD.
vi_version() {
  local IFS=. parts out=() p i; read -ra parts <<<"$1"
  for i in 0 1 2 3; do
    p="${parts[$i]:-0}"; p="${p%%[!0-9]*}"; p="$((10#${p:-0}))"
    if [ "$p" -gt 65535 ]; then p="$((10#$(date +%m%d)))"; fi
    out+=("$p")
  done
  echo "${out[0]}.${out[1]}.${out[2]}.${out[3]}"
}
