# galaxybook12-power

Power fixes for the Samsung Galaxy Book 12 (SM-W720) on Fedora: battery that
drains while charging, battery that drains during sleep, and a power button
that sends the tablet straight back to sleep. Developed and measured on an
SM-W720 (i5-7200U, BIOS `P04HAC.000.180220`), Fedora 44, kernel 7.3.

```bash
sudo ./install.sh      # safe to re-run; ./uninstall.sh reverts everything
```

## 1. Battery drains while charging

**Measured:** charger connected, battery reported "Charging", yet it dropped
from 50 % to 47 % in 15 min (≈13 W net drain under load). Linux has no
interface to the charger negotiation (no UCSI/Type-C class; the embedded
controller handles it), so a kernel module cannot make the charger deliver
more. What Linux *can* do is keep the CPU from out-drawing the charger: the
i5-7200U may draw 15 W sustained / 25 W bursts.

**Fix:** while on AC, the CPU package limit (MSR RAPL `intel-rapl:0`) is set
to `AC_PL1`/`AC_PL2` (default 7 W / 12 W); on battery the firmware defaults
15 W / 25 W return. Applied at boot, on every plug/unplug (udev) and after
resume. Tune it in `/etc/galaxybook-power.conf`.

The MMIO RAPL limits (`intel-rapl-mmio:0`) are left to thermald (adaptive
DPTF mode lowers them itself for thermal protection); the lower of the two
always applies, so this never overrides thermal throttling upward.

Check: `cat /sys/class/powercap/intel-rapl:0/constraint_0_power_limit_uw`
→ `7000000` on AC, `15000000` on battery.

A weak charger or cable is still the most likely root cause — the original
Samsung adapter and a good USB-C cable help most.

## 2. Battery drains during sleep

**Measured:** ≈3 %/hour while suspended. The firmware declares low-power S0
idle (`s2idle`) as the sleep mode, but the chipset never reached S0ix
(`slp_s0_residency_usec` stayed 0) although the CPU reached PC10.

`bin/s0ix-experiments` adds one change per 40 s `s2idle` round:

| step | S0ix during 40 s |
|---|---|
| baseline | 0 s |
| **+ SATA link power `min_power` (DevSleep)** | **39.2 s** |
| + PCIe ASPM powersupersave, Bluetooth off, LTR ignore, ME unloaded | no further gain |

**Fix:** SATA link power management `min_power`. TuneD sets the SATA policy
itself (`balanced`/`balanced-battery`: `med_power_with_dipm`) and re-applies
it on every AC/battery switch, so a udev rule would be overwritten; the fix is
a TuneD **post-loaded profile** (`galaxybook-s0ix`) applied on top of every
profile.

Check: `tuned-adm active` shows `Current post-loaded profile: galaxybook-s0ix`;
`cat /sys/class/scsi_host/host*/link_power_management_policy` shows
`min_power` for the disk's host. If your SSD logs errors
(`journalctl -k | grep -iE 'ata.*(error|reset|frozen)'`), uninstall.

**Why not S3 (`deep`)?** The firmware offers it, but on this unit the tablet
never resumes from real S3 and has to be forced off. `bin/suspend-bisect`
runs all `/sys/power/pm_test` stages (freezer, devices, platform, processors,
core) and they all pass, so the hang is in the firmware's power-off/resume,
not in a driver. Don't use `bin/deep-sleep-test --enable`.

## 3. Power button puts the tablet back to sleep

Pressing the power button to wake the tablet is also delivered to GNOME as a
key press; with `power-button-action='suspend'` it suspends again
immediately. **Fix:** a dconf default (`/etc/dconf/db/local.d/`) sets
`'nothing'` for all users and the login screen. Suspend via the system menu
or the screen timeout. Users can still change it in Settings → Power.

## Tools (`bin/`, run with sudo)

| tool | what it does |
|---|---|
| `galaxybook-ac-power` | the power-cap script itself (installed to `/usr/local/sbin`) |
| `s0ix-check` | one 60 s s2idle; reports S0ix/PC10 residency, PCH IP power gating, LTR |
| `s0ix-experiments` | the step-by-step S0ix test above; reverts everything |
| `suspend-bisect` | runs every `pm_test` stage with auto-resume to locate suspend hangs |
| `deep-sleep-test` | S3 drain test (see warning above) |

Tip: don't press the power button to wake the tablet during these tests (see 3).

## Other

`cleanup-old-10.6-audio.sh` moves the old Galaxy Book 10.6 userspace audio
workaround out of the way (backup kept, `--restore` puts it back). On a Book 12
its resume hook wrote the 10.6 amplifier setup to the wrong I²C addresses on
every wake. The Book 12 audio driver is in the separate `galaxybook12-audio`
repo.

## License

GPL-2.0.
