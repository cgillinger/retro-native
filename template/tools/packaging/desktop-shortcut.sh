#!/bin/sh
# desktop-shortcut.sh install|remove <id> <exec path>
# Installed by the .deb/.rpm as /usr/lib/<id>/desktop-shortcut.sh and called from the maintainer
# scripts (postinst/prerm, %post/%preun). Puts <id>.desktop on the desktop of every logged-in user
# (and removes it again on uninstall) so the icon is there right after installing.
# Called on a FRESH install only (upgrade/reinstall neither creates nor removes it, so a deliberately
# deleted shortcut stays deleted). Runs as root but does ALL file operations AS THE USER (runuser/setpriv): no symlink attacks,
# right ownership. Best effort: never fails the package transaction; one log line per user.
# Users: sessions from `loginctl` (uid 1000..59999), plus SUDO_USER and PKEXEC_UID.
# Test override: RN_SHORTCUT_USERS="u1 u2" (or the file /etc/<id>-shortcut-users) replaces detection.
# Without loginctl and without override nobody is served (containers).
# The in-game first-launch shortcut (v2_desktop_shortcut) stays as fallback for users who were not
# logged in; both use the same rule: an existing file whose Exec= is our binary is left alone.

# ---- user side: sh desktop-shortcut.sh user-install|user-remove <id> <exec> (HOME is set by the caller)
exec_of() {   # first word of Exec= (unquoted, simple quotes only) of file $1
  sed -n 's/^Exec=//p' "$1" 2>/dev/null | head -n 1 | sed -e 's/\\\\/\\/g' -e 's/^"\([^"]*\)".*/\1/' -e 's/ .*//'
}
desktop_dir() {
  d=""
  cfg="${XDG_CONFIG_HOME:-$HOME/.config}"
  if [ -r "$cfg/user-dirs.dirs" ]; then
    d="$(sed -n 's/^[ 	]*XDG_DESKTOP_DIR=//p' "$cfg/user-dirs.dirs" | head -n 1 | sed -e 's/[ 	]*$//' -e 's/^"\(.*\)"$/\1/')"
    case "$d" in
      '$HOME'*) d="$HOME${d#\$HOME}" ;;
      '${HOME}'*) d="$HOME${d#\$\{HOME\}}" ;;
    esac
    case "$d" in /*) ;; *) d="" ;; esac
  fi
  [ -n "$d" ] || d="$HOME/Desktop"
  printf '%s\n' "$d"
}
case "${1:-}" in
  user-install)
    id="$2"; bin="$3"; src="/usr/share/applications/$id.desktop"
    [ -f "$src" ] || { echo "no $src"; exit 0; }
    dir="$(desktop_dir)"
    [ -d "$dir" ] || { echo "skipped (no desktop dir $dir)"; exit 0; }
    f="$dir/$id.desktop"
    if [ -e "$f" ]; then
      if [ "$(cat "$src")" = "$(cat "$f" 2>/dev/null)" ]; then echo "up to date $f"; exit 0; fi
      if [ "$(exec_of "$f")" = "$bin" ]; then echo "kept $f (already ours)"; exit 0; fi
    fi
    tmp="$dir/.$id.desktop.tmp.$$"
    if cp "$src" "$tmp" && chmod 0755 "$tmp" && mv -f "$tmp" "$f"; then
      echo "installed $f"
      if command -v gio >/dev/null 2>&1 && [ -S "/run/user/$(id -u)/bus" ]; then
        DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$(id -u)/bus" timeout 5 gio set "$f" metadata::trusted true >/dev/null 2>&1 || true
      fi
    else
      rm -f "$tmp"; echo "failed to write $f"
    fi
    exit 0 ;;
  user-remove)
    id="$2"; bin="$3"; f="$(desktop_dir)/$id.desktop"
    if [ -f "$f" ] && [ "$(exec_of "$f")" = "$bin" ]; then rm -f "$f" && echo "removed $f"
    elif [ -e "$f" ]; then echo "kept $f (not ours)"; fi
    exit 0 ;;
esac

# ---- root side
PATH="$PATH:/usr/sbin:/sbin:/usr/bin:/bin"; export PATH
action="${1:-}"; id="${2:-}"; bin="${3:-}"
case "$action" in install|remove) ;; *) echo "usage: $0 install|remove <id> <exec path>" >&2; exit 0 ;; esac
[ -n "$id" ] && [ -n "$bin" ] || exit 0
self="$(readlink -f "$0" 2>/dev/null || echo "$0")"

users=""
if [ -n "${RN_SHORTCUT_USERS:-}" ]; then users="$RN_SHORTCUT_USERS"
elif [ -r "/etc/$id-shortcut-users" ]; then users="$(cat "/etc/$id-shortcut-users")"
else
  if command -v loginctl >/dev/null 2>&1; then
    sess="$(loginctl list-sessions --no-legend 2>/dev/null || true)"
    while read -r _s uid _rest; do
      [ -n "$uid" ] || continue
      n="$(getent passwd "$uid" 2>/dev/null | cut -d: -f1)"; users="$users $n"
    done <<SESSIONS
$sess
SESSIONS
  fi
  [ -z "${SUDO_USER:-}" ] || users="$users $SUDO_USER"
  if [ -n "${PKEXEC_UID:-}" ]; then
    users="$users $(getent passwd "$PKEXEC_UID" 2>/dev/null | cut -d: -f1)"
  fi
fi

done_users=" "
for u in $users; do
  case "$done_users" in *" $u "*) continue ;; esac
  done_users="$done_users$u "
  ent="$(getent passwd "$u" 2>/dev/null)" || continue
  uid="$(echo "$ent" | cut -d: -f3)"; home="$(echo "$ent" | cut -d: -f6)"
  case "$uid" in ''|*[!0-9]*) continue ;; esac
  [ "$uid" -ge 1000 ] && [ "$uid" -lt 60000 ] || continue
  [ -n "$home" ] && [ -d "$home" ] || continue
  if command -v runuser >/dev/null 2>&1; then
    run="runuser -u $u --"
  elif command -v setpriv >/dev/null 2>&1; then
    run="setpriv --reuid=$uid --regid=$(echo "$ent" | cut -d: -f4) --init-groups --"
  else
    echo "$id shortcut: no runuser/setpriv, skipping $u"; continue
  fi
  out="$($run env -u XDG_CONFIG_HOME HOME="$home" sh "$self" "user-$action" "$id" "$bin" 2>&1)" || out="${out:-error}"
  echo "$id shortcut for $u: $out" | head -n 1
done
exit 0
