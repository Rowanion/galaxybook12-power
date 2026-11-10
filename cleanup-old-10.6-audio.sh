#!/bin/bash
# Remove the leftover Galaxy Book 10.6 *userspace* audio workaround.
# This machine is a Book 12 (codec 144d:c14f) and uses the native kernel patch
# (DKMS alc298-galaxybook), which handles init, jack switching and resume.
# The leftover resume hook wrote the 10.6's amp setup (I2C 0x34/0x35) on
# every wake and forced speaker routing. Files are moved, not deleted.
#   sudo ./cleanup-old-10.6-audio.sh            # remove (with backup)
#   sudo ./cleanup-old-10.6-audio.sh --restore  # put everything back
set -euo pipefail
[ "$(id -u)" = 0 ] || exec sudo "$0" "$@"
U="${SUDO_USER:-root}"; H="$(getent passwd "$U" | cut -d: -f6)"
B="$H/galaxybook-10.6-audio-removed-backup"
WP="$H/.config/wireplumber/wireplumber.conf.d/51-alc298-no-suspend.conf"
FILES=(
  /etc/systemd/system-sleep/galaxybook-audio
  /etc/systemd/system/galaxybook-audio.service
  /etc/systemd/system/galaxybook-jack-watch.service
  /usr/local/bin/galaxybook-audio-init
  /usr/local/bin/galaxybook-jack-watch
  /usr/local/lib/galaxybook-audio
  "$WP"
)

if [ "${1:-}" = --restore ]; then
  [ -d "$B" ] || { echo "no backup at $B"; exit 1; }
  for f in "${FILES[@]}"; do
    [ -e "$B$f" ] || continue
    mkdir -p "$(dirname "$f")"; cp -a "$B$f" "$(dirname "$f")/"
  done
  restorecon -R /etc/systemd /usr/local 2>/dev/null || true
  systemctl daemon-reload
  echo "Restored (services stay disabled, as before). Log out/in for the WirePlumber rule."
  exit 0
fi

systemctl disable --now galaxybook-audio.service galaxybook-jack-watch.service 2>/dev/null || true
for f in "${FILES[@]}"; do
  [ -e "$f" ] || continue
  mkdir -p "$B$(dirname "$f")"
  mv "$f" "$B$(dirname "$f")/"
  echo "moved $f"
done
chown -R "$U": "$B"
systemctl daemon-reload
# restart the user's WirePlumber so the no-suspend rule is dropped now
uid=$(id -u "$U")
runuser -u "$U" -- env XDG_RUNTIME_DIR=/run/user/$uid DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$uid/bus \
  systemctl --user restart wireplumber 2>/dev/null && echo "WirePlumber restarted for $U" || true
echo "Done. Backup: $B   (undo: sudo $0 --restore)"
echo "The kernel audio patch (DKMS alc298-galaxybook) is untouched."
