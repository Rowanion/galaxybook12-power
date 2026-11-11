# galaxybook12-power

Power fixes for the Samsung Galaxy Book 12 (SM-W720) on Fedora: battery that
drains while charging, battery that drains during sleep, a power button
that sends the tablet straight back to sleep, and a brightness slider that
does nothing. Developed and measured on an
SM-W720 (i5-7200U, BIOS `P04HAC.000.180220`), Fedora 44, kernel 7.3.

```bash
sudo ./install.sh      # safe to re-run; ./uninstall.sh reverts everything
```

> **Fedora only.** Tested on Fedora 44 only. The power-cap script, udev rule and systemd units should work on
> other distributions, but the install scripts are written for Fedora (TuneD for
> the sleep fix, GNOME dconf for the power button, SELinux labels).
> On other distributions they are untested and will likely fail.

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

## 4. Brightness slider does nothing

The SM-W720 has a Samsung AMOLED panel (EDID `SDC a029`). Linux shows a
brightness slider (`intel_backlight`), but the panel ignores it: its
brightness is set through private panel registers over the eDP AUX channel.

**Fix:** `brightness/` — a small service that follows the slider and writes the
panel's own brightness levels 10–101 (with a short fade), computed from the
panel's factory calibration. It only runs on a Galaxy Book 12 with exactly
this panel. Installed by `install.sh`; on its own: `sudo brightness/install.sh`
/ `sudo brightness/uninstall.sh`. Manual test:
`sudo /usr/local/sbin/galaxybook12-brightness set 60` (range 1–101; below 40
some panels may flicker).

**thermald:** the firmware's thermal tables (DPTF, `INT3400` `data_vault`,
PSVT tables `Camera`/`Clamshell`/`NonCamera`/`highperf`) list the display
(`\_SB_.PCI0.DPLY`) as a passive cooling device for the skin sensor `SEN2`:
dim the screen when the tablet gets hot in your hands — a comfort feature;
the CPU (`B0D4`) entries are the actual heat protection. For a cool tablet the
table says "display: MAX". Windows' DPTF treats that as a ceiling (the user may
go lower); thermald `--adaptive` (Fedora's default) treats it as a target and
sets the backlight back to 100 % in 1 % steps every 4 s, even at a cool 32 °C
skin temperature, overriding the slider. Before this service that value never
reached the panel, so nobody noticed.

The installer therefore adds a drop-in
(`/etc/systemd/system/thermald.service.d/50-galaxybook-no-backlight.conf`)
that makes the backlight read-only for thermald only — both the backlight
itself and the firmware's display cooling device `DPLY`
(`/sys/class/thermal/cooling_device*`), through which thermald reaches the
backlight via ACPI as well. Lost: dimming the screen
when the skin gets hot — which never worked on this panel under Linux anyway.
Unchanged: thermald's CPU throttling (still adaptive) and the hardware's own
protections (CPU self-throttling, firmware critical shutdown). Possible later
improvement: cap the brightness in this service above a skin temperature taken
from the same PSVT table, the way Windows does.

Change from the original: the service sleeps until the slider actually moves
(the kernel notifies `actual_brightness` changes) instead of checking it 10
times a second, and a system-sleep hook makes it re-initialise the panel
immediately after resume.

Credit: the service is from
[fxjosemi/Samsung-Galaxy-Book-12-Linux](https://github.com/fxjosemi/Samsung-Galaxy-Book-12-Linux)
(commit `9245ef7`); the panel protocol, calibration algorithm and register
tables are from Aurélien Croc's
[SamsungGalaxyBook12](https://github.com/Teetoow/SamsungGalaxyBook12).
GPL-2.0-only.

## Fedora release upgrades

The camera, audio and accel drivers are DKMS packages (separate repos) and
rebuild themselves for new kernels. A release upgrade (e.g. 44 → 45) adds three
risks: a driver patch that no longer applies to the new kernel, the patched
libcamera (front camera) being versionlocked to the old release, and
systemd-homed homes, which deserve a backup before any big change.

```bash
sudo upgrade/pre-upgrade            # target = next release; --releasever N to choose
sudo dnf upgrade --refresh          # reboot if it installed anything
sudo dnf system-upgrade download --releasever=45
sudo dnf offline reboot
sudo upgrade/post-upgrade           # after the upgrade
```

`upgrade/pre-upgrade`:

1. Backs up every systemd-homed home (LUKS image, user record, and the
   machine's homed signing keys `local.private`/`local.public`) to
   `/var/backups/galaxybook-upgrade/` — a reflink copy on btrfs (instant, no
   extra space), else a full copy. The homes must be logged out. Use
   `--backup-dir` for an external drive; a backup on the same disk does not
   help if the disk dies.
2. Downloads the target release's `kernel-devel` and test-builds the installed
   drivers against it. It also caches that kernel's source, so the drivers
   already build during the offline upgrade (which has no network).
3. Only if everything passed: removes the libcamera versionlock, so the
   upgrade can replace the old build.

It upgrades nothing and is safe to re-run. `upgrade/pre-upgrade --kernel-only`
runs only step 2, without root, and changes nothing — useful to check early
whether the next release's kernel still works with the drivers.

`upgrade/post-upgrade` checks the drivers (rebuilding missing ones), the
cameras, the accelerometer, audio, libcamera, the power fixes and the homed
homes, and lists what is left to do. Typically that is: rebuild libcamera
(`~/galaxybook12-camera/scripts/install-libcamera.sh` as your user — not
needed if the new stock libcamera already has the IMX241 helper), and log in
once as each homed user.

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

## AI assistance

This repository was developed with AI assistance (Claude by Anthropic). Every
change was reviewed and tested on real SM-W720 hardware.

## License

GPL-2.0.
