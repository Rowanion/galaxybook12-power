#!/bin/bash
# Install the AMOLED brightness service (also called by ../install.sh).
set -euo pipefail
[ "$(id -u)" = 0 ] || exec sudo "$0" "$@"
cd "$(dirname "$0")"
need=(); for c in make gcc; do command -v $c >/dev/null || need+=($c); done
[ ${#need[@]} -eq 0 ] || dnf install -y "${need[@]}"
make -s
[ -n "${SUDO_USER:-}" ] && chown "$SUDO_USER": galaxybook12-brightness   # keep the repo user-owned
./galaxybook12-brightness check          # refuses on any other machine/panel
install -m 755 galaxybook12-brightness /usr/local/sbin/
install -m 644 galaxybook12-brightness.service /etc/systemd/system/
install -d /etc/systemd/system-sleep
install -m 755 galaxybook12-brightness.sleep /etc/systemd/system-sleep/galaxybook12-brightness
restorecon /usr/local/sbin/galaxybook12-brightness /etc/systemd/system/galaxybook12-brightness.service \
  /etc/systemd/system-sleep/galaxybook12-brightness 2>/dev/null || true
systemctl daemon-reload
systemctl enable galaxybook12-brightness.service
systemctl restart galaxybook12-brightness.service
sleep 1; systemctl is-active galaxybook12-brightness.service
