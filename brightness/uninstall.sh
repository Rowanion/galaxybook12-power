#!/bin/bash
# Remove the AMOLED brightness service. The panel keeps its last level until
# the next reboot or suspend.
set -euo pipefail
[ "$(id -u)" = 0 ] || exec sudo "$0" "$@"
systemctl disable --now galaxybook12-brightness.service 2>/dev/null || true
rm -f /etc/systemd/system/galaxybook12-brightness.service /usr/local/sbin/galaxybook12-brightness \
      /etc/systemd/system-sleep/galaxybook12-brightness
rm -f /etc/systemd/system/thermald.service.d/50-galaxybook-no-backlight.conf
rmdir /etc/systemd/system/thermald.service.d 2>/dev/null || true
systemctl daemon-reload
systemctl try-restart thermald.service
echo "AMOLED brightness service removed."
