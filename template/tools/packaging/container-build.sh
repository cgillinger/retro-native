#!/usr/bin/env bash
# Runs INSIDE the ubuntu:22.04 build container (started by release.sh, never by hand):
#   builds the Linux binary, checks its dependencies, strips it, and builds the .deb with dpkg-deb.
# Mounts: /build/src (clean source export, rw)  /data (game data, READ-ONLY)  /pkg (this dir, ro)
#         /release.conf (ro)  /icons (hicolor tree, ro)  /out (results, rw)
# Env: VERSION JOBS LOAD
set -euo pipefail
. /pkg/lib.sh
DATA_DIR=/data; GAME_ROOT=/build/src
. /release.conf

mkdir -p /out
if [ "${PACKAGE_ONLY:-0}" = 1 ]; then
  # `release.sh repack`: no build, reuse the already built and checked /out/$BIN_NAME
  [ -f "/out/$BIN_NAME" ] || die "PACKAGE_ONLY: /out/$BIN_NAME missing"
else
  cd /build/src
  ulimit -s unlimited || true      # clang's parser needs a deep stack for very large generated sources
  export PKG_CONFIG_PATH=/opt/sdl2/lib/pkgconfig
  say "build: $LINUX_BUILD_CMD"
  eval "$LINUX_BUILD_CMD"
  strip -o "/out/$BIN_NAME" "$LINUX_OUT"
  /pkg/check_deps.sh linux "/out/$BIN_NAME" "${MAX_GLIBC:-2.35}"
fi

# ---- staging tree (shared with the rpm, which release.sh builds on the host from /out/stage)
INSTALL_BIN="/usr/games/$BIN_NAME"
stage=/out/stage; rm -rf "$stage"
install -d -m755 "$stage/usr/games" "$stage/usr/share/applications" "$stage/usr/share/doc/$GAME_ID"
install -m755 "/out/$BIN_NAME" "$stage$INSTALL_BIN"
# install-time desktop shortcut helper (called by postinst/prerm and the rpm scriptlets)
install -d -m755 "$stage/usr/lib/$GAME_ID"
install -m755 /pkg/desktop-shortcut.sh "$stage/usr/lib/$GAME_ID/desktop-shortcut.sh"
for d in /icons/hicolor/*x*/apps; do
  n="$(basename "$(dirname "$d")")"
  install -d -m755 "$stage/usr/share/icons/hicolor/$n/apps"
  install -m644 "$d/$GAME_ID.png" "$stage/usr/share/icons/hicolor/$n/apps/$GAME_ID.png"
done
render /pkg/deb/desktop.in "$stage/usr/share/applications/$GAME_ID.desktop" GAME_NAME DESCRIPTION INSTALL_BIN GAME_ID CATEGORIES
chmod 644 "$stage/usr/share/applications/$GAME_ID.desktop"
render /pkg/deb/copyright.in "$stage/usr/share/doc/$GAME_ID/copyright" GAME_NAME
chmod 644 "$stage/usr/share/doc/$GAME_ID/copyright"
find "$stage" -type d -exec chmod 755 {} +

# ---- .deb
deb="/tmp/debroot"; rm -rf "$deb"; cp -a "$stage" "$deb"; mkdir "$deb/DEBIAN"
INSTALLED_SIZE="$(du -sk "$deb" | cut -f1)"
DEB_VERSION="${VERSION//[^A-Za-z0-9.+~]/.}"
HOMEPAGE_LINE=""; [ -z "${HOMEPAGE:-}" ] || HOMEPAGE_LINE="Homepage: $HOMEPAGE"$'\n'
DESCRIPTION_LONG_DEB="$(deb_long_desc "$DESCRIPTION_LONG")"
render /pkg/deb/postinst.in "$deb/DEBIAN/postinst" GAME_ID INSTALL_BIN
render /pkg/deb/prerm.in "$deb/DEBIAN/prerm" GAME_ID INSTALL_BIN
chmod 755 "$deb/DEBIAN/postinst" "$deb/DEBIAN/prerm"
VERSION="$DEB_VERSION" render /pkg/deb/control.in "$deb/DEBIAN/control" GAME_ID VERSION INSTALLED_SIZE MAINTAINER HOMEPAGE_LINE DESCRIPTION DESCRIPTION_LONG_DEB
(cd "$deb" && find usr -type f -print0 | sort -z | xargs -0 md5sum > DEBIAN/md5sums)
chmod 644 "$deb/DEBIAN/control" "$deb/DEBIAN/md5sums"; chmod 755 "$deb/DEBIAN"
out="/out/${GAME_ID}_${DEB_VERSION}_amd64.deb"
dpkg-deb --root-owner-group -Zxz --build "$deb" "$out"
dpkg-deb -I "$out" | sed -n '1,14p'
say "deb: $out"
