#!/bin/bash
#
# Install Barnstormer from this tarball.
#
#   ./install.sh                 -> ~/.local  (no root needed)
#   PREFIX=/usr/local ./install.sh
#
# On Arch and Omarchy the PKGBUILD in the repository is the better route:
# it tracks the files and uninstalls cleanly.  This script is for anyone who
# just wants the binary in place.

set -euo pipefail

PREFIX="${PREFIX:-$HOME/.local}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Ask the loader what it cannot resolve, rather than guessing from a cache.
# awk drains the pipe, so this stays honest under `set -o pipefail`.
missing=$(ldd "$HERE/bin/barnstormer" 2>/dev/null | awk '/not found/ { print $1 }')
if [[ -n $missing ]]; then
  echo "warning: barnstormer will not start, these libraries are missing:" >&2
  echo "$missing" | sed 's/^/  /' >&2
  echo "on Arch: pacman -S wayland libxkbcommon alsa-lib" >&2
fi

install -Dm755 "$HERE/bin/barnstormer" "$PREFIX/bin/barnstormer"
install -Dm644 "$HERE"/share/applications/*.desktop -t "$PREFIX/share/applications/"
install -Dm644 "$HERE"/share/icons/hicolor/scalable/apps/*.svg \
        -t "$PREFIX/share/icons/hicolor/scalable/apps/"
install -Dm644 "$HERE"/share/licenses/barnstormer/* \
        -t "$PREFIX/share/licenses/barnstormer/"
install -Dm644 "$HERE"/share/doc/barnstormer/README.md \
        -t "$PREFIX/share/doc/barnstormer/"

update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true
gtk-update-icon-cache -qtf "$PREFIX/share/icons/hicolor" 2>/dev/null || true

echo "installed to $PREFIX"
case ":$PATH:" in
  *":$PREFIX/bin:"*) ;;
  *) echo "note: $PREFIX/bin is not on your PATH" >&2 ;;
esac
echo "run 'barnstormer' for a window, 'barnstormer --breakout' for the overlay"
