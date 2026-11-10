#!/bin/bash
# Remove the power cap, restore firmware limits, and undo deep sleep.
set -euo pipefail
[ "$(id -u)" = 0 ] || exec sudo "$0" "$@"
systemctl disable --now galaxybook-ac-power.service 2>/dev/null || true
rm -f /etc/systemd/system/galaxybook-ac-power.service /etc/systemd/system-sleep/galaxybook-ac-power \
      /etc/udev/rules.d/90-galaxybook-ac-power.rules /usr/local/sbin/galaxybook-ac-power \
      /etc/systemd/sleep.conf.d/50-galaxybook-deep.conf
sed -i 's/\bgalaxybook-s0ix\b//; s/  */ /g; s/^ //; s/ $//' /etc/tuned/post_loaded_profile 2>/dev/null || true
rm -rf /etc/tuned/profiles/galaxybook-s0ix
rm -f /etc/dconf/db/local.d/50-galaxybook-power-button; dconf update
systemctl daemon-reload; udevadm control --reload
systemctl restart tuned
for z in /sys/class/powercap/intel-rapl:0; do
  echo 15000000 > $z/constraint_0_power_limit_uw; echo 25000000 > $z/constraint_1_power_limit_uw
done
echo "Removed. /etc/galaxybook-power.conf kept (delete by hand if unwanted)."
