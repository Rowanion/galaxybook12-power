#!/bin/bash
# Galaxy Book 12 power fixes. Run: sudo ./install.sh   (safe to re-run)
#  - CPU power cap while charging (udev + service + resume hook)
#  - SATA DevSleep via TuneD post-loaded profile, so s2idle reaches S0ix
#  - power button only wakes (all users + login screen)
#  - AMOLED brightness: the brightness slider controls the panel
set -euo pipefail; cd "$(dirname "$0")"
[ "$(id -u)" = 0 ] || exec sudo "$0" "$@"
install -m 755 bin/galaxybook-ac-power /usr/local/sbin/
[ -e /etc/galaxybook-power.conf ] || install -m 644 conf/galaxybook-power.conf /etc/
install -m 644 systemd/galaxybook-ac-power.service /etc/systemd/system/
install -d /etc/systemd/system-sleep
install -m 755 systemd/galaxybook-ac-power.sleep /etc/systemd/system-sleep/galaxybook-ac-power
install -m 644 udev/90-galaxybook-ac-power.rules /etc/udev/rules.d/
restorecon -R /usr/local/sbin/galaxybook-ac-power /etc/galaxybook-power.conf \
  /etc/systemd/system/galaxybook-ac-power.service /etc/systemd/system-sleep \
  /etc/udev/rules.d/90-galaxybook-ac-power.rules 2>/dev/null || true
# SATA DevSleep via TuneD post-loaded profile (tuned would override a udev rule)
install -d /etc/tuned/profiles/galaxybook-s0ix
install -m 644 tuned/galaxybook-s0ix/tuned.conf /etc/tuned/profiles/galaxybook-s0ix/
restorecon -R /etc/tuned/profiles/galaxybook-s0ix 2>/dev/null || true
cur="$(cat /etc/tuned/post_loaded_profile 2>/dev/null)"
case " $cur " in *" galaxybook-s0ix "*) ;; *) echo "${cur:+$cur }galaxybook-s0ix" > /etc/tuned/post_loaded_profile ;; esac
# Power button only wakes (all users + login screen)
install -m 644 dconf/50-galaxybook-power-button /etc/dconf/db/local.d/
dconf update
systemctl daemon-reload; udevadm control --reload
systemctl restart tuned
systemctl enable --now galaxybook-ac-power.service
systemctl start galaxybook-ac-power.service
journalctl -u galaxybook-ac-power -n 1 -o cat
grep -H . /sys/class/powercap/intel-rapl:0/constraint_[01]_power_limit_uw /sys/class/powercap/intel-rapl-mmio:0/constraint_0_power_limit_uw
tuned-adm active
echo "SATA LPM: $(cat /sys/class/scsi_host/host*/link_power_management_policy | paste -sd' ')"
# AMOLED brightness: make the brightness slider control the panel
brightness/install.sh
