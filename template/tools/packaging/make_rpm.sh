#!/usr/bin/env bash
# make_rpm.sh <conf> <stage-dir> <rpmtop-dir> <outdir>   (runs on the host; rpmbuild only repackages the staged tree)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
. "$here/lib.sh"
conf="$1" stage="$2" top="$3" out="$4"
. "$conf"
: "${VERSION:?}"
INSTALL_BIN="/usr/games/$BIN_NAME"; STAGE_DIR="$stage"
RPM_VERSION="${VERSION//[^A-Za-z0-9._+~]/.}"; RPM_VERSION="${RPM_VERSION//-/.}"
HOMEPAGE_RPM=""; [ -z "${HOMEPAGE:-}" ] || HOMEPAGE_RPM="URL:            $HOMEPAGE"$'\n'
rm -rf "$top"; mkdir -p "$top"/{SPECS,BUILD,RPMS,SRPMS,BUILDROOT}
render "$here/rpm/package.spec.in" "$top/SPECS/$GAME_ID.spec" GAME_ID RPM_VERSION DESCRIPTION DESCRIPTION_LONG HOMEPAGE_RPM STAGE_DIR INSTALL_BIN
rpmbuild -bb --target x86_64 --define "_topdir $top" "$top/SPECS/$GAME_ID.spec" >/dev/null
mkdir -p "$out"; cp "$top"/RPMS/x86_64/*.rpm "$out/"
ls -l "$out"/*.rpm
