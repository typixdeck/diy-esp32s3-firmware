# Raspberry Pi companion

`companion.py` is a rootless, local CDC telemetry bridge for official Raspberry Pi OS.
It reports uptime, CPU temperature and CPU frequency. Unknown readings are `-1`.
It does not report HDMI frame rate: neither a configured display refresh rate nor the
ESP's inner-panel DPI VSync is an HDMI rendering-FPS measurement.

Start with an explicitly verified physical serial path, as a regular user with access
to that device. It does not scan for a tty, install packages or change permissions:

```sh
python3 host/companion.py --device /dev/serial/by-path/YOUR_VERIFIED_PORT
```

The default is read-only. To enable the ESP screen's cooperative Linux shutdown button,
the operator may opt in with `--allow-shutdown`. The bridge advertises that capability
only when login1 `CanPowerOff` already returns `yes`; authentication challenges are
not accepted. Actual execution rechecks authorization and runs the fixed command
`/usr/bin/systemctl --no-ask-password poweroff`, without sudo or a shell. If the current
user/session is not already permitted to power off, the button remains unavailable.
This package intentionally does not add a broad Polkit rule.

`ACCEPTED` means that systemctl accepted the request. It is **never** confirmation of
unmounted storage, CM4 power removal, safe AW/GPIO control, or completed shutdown.
Missing ACK or USB disconnection is also not proof of safe shutdown. Resume-from-RAM,
hardware startup, PMIC/GLOBAL_EN control and display blanking are not implemented here.

The process takes an advisory exclusive lock on the chosen tty and exits on connection
loss. It does not automatically open a newly enumerated device. Stop this process before
Copilot flashing, serial diagnosis or any other CDC owner; other tools must cooperate
with the lock. Clearing HUPCL avoids close-time hangup, but opening a serial device is
not proof of electrically side-effect-free USB line state on every firmware.

No keyboard data, raw serial stream, hardware identity or usage data is saved or sent.
Unrecognized lines and binary data are discarded. Only short protocol lines are parsed.

## Optional user service

After verifying the path, place a user unit under `~/.config/systemd/user/` with an
absolute Python/script path and explicit port. The example is read-only and does not
auto-restart after disconnect:

```ini
[Unit]
Description=TypixDeck local telemetry

[Service]
Type=simple
ExecStart=/usr/bin/python3 /ABSOLUTE/PATH/host/companion.py --device /dev/serial/by-path/YOUR_VERIFIED_PORT
Restart=no
NoNewPrivileges=yes

[Install]
WantedBy=default.target
```

No unit is installed or enabled automatically. The shutdown option is an explicit
operator choice and still requires existing noninteractive login1 authorization.

## TD1 protocol

ASCII lines, maximum 158 bytes excluding line ending; fixed fields and no arbitrary
shell command payload. Nonces are 16 lowercase hexadecimal digits generated afresh
for host connection and firmware handshake. They bind messages to a local session;
they are replay protection, not cryptographic authentication against another process
with access to the same serial device.

```text
TD1 HELLO <host_nonce>
TD1 WELCOME <host_nonce> <device_nonce>
TD1 HB <host_nonce> <device_nonce> <seq> <shutdown_cap_0_or_1> <uptime_s> <cpu_mC> <cpu_kHz>
TD1 CMD <host_nonce> <device_nonce> 1 SHUTDOWN
TD1 ACK <host_nonce> <device_nonce> 1 ACCEPTED|DENIED|FAILED
```

Heartbeat sequence increases strictly; stale/replayed heartbeats cannot grant a
capability. Freshness lasts 8 seconds. Commands expire after 15 seconds if unanswered,
are never resent, and only one shutdown request is allowed per host connection,
including failed requests. Reconnect establishes fresh nonces; an old ACK cannot
complete a new session's request. The firmware never performs hard-power actions.

## Local tests

```sh
python3 -m unittest discover -s tests -p test_companion.py
cc -std=c11 -D_POSIX_C_SOURCE=200809L -DPI_LINK_HOST_TEST -Wall -Wextra -Werror \
  -fsanitize=address,undefined -Imain main/pi_link.c tests/test_pi_link.c \
  -o /tmp/typix-pi-link-test
/tmp/typix-pi-link-test
```

These tests use fake telemetry, time, randomness and shutdown callbacks. They never
connect to a device or power off the development computer. Hardware evidence is recorded separately in `docs/hmi-and-apps.md`; local tests do not substitute for it.

## 0.4.0 paired HTTPS, files and screenshots

The 0.4.0 candidate has been flashed through CM4 and USB pairing completed. The actual
CM4 status, file-list and screenshot endpoints passed authenticated HTTPS requests. The actual ESP framebuffer also confirms Wi-Fi/HTTPS telemetry.
Interactive file/screenshot viewing and transport-fallback acceptance remain pending. No Samba/VNC
or root daemon is required.

On the Pi, from a desktop terminal (so `WAYLAND_DISPLAY` is inherited):

```sh
python3 host/install.py --address YOUR_PI_LAN_IPV4 --start
```

Requires Raspberry Pi OS Python 3 and openssl. Screenshot support additionally needs
`grim` and `python3-pil`; the firmware displays an error when unavailable. The script
never invokes apt or sudo. Reserve the Pi address in DHCP: the certificate's IP SAN and
ESP pairing refer to it. The HTTPS service binds this address, port 38471, with a random
bearer token and local certificate under `~/.config/typixdeck/companion/` (private directory).
It logs neither tokens nor HTTP paths. Do not commit that directory or copy its private key
to the ESP. Certificate verification is mandatory; **ESP Wi-Fi and NTP must work first**.

Stop any CDC owner, then provision the companion certificate/token through the **physically
verified** board port while DIY 0.4.0 is running. This does not enter download mode:

```sh
systemctl --user stop typix-companion-cdc.service
python3 host/pair.py --device /dev/serial/by-path/YOUR_VERIFIED_PORT
```

`pair.py` checks the firmware's bounded BOOT_STATUS response, owns the tty exclusively,
and waits for each chunk's acknowledgment. It does not print credentials or save serial
traffic. Unfinished pairing leaves the previous pairing intact. The ESP "Forget pairing"
action erases only this companion entry from NVS (not Wi-Fi). Pairing is local access control,
not physical Flash encryption. NVS remains unencrypted.

For CDC fallback, re-run the installer with `--device /dev/serial/by-path/YOUR_VERIFIED_PORT`
and `--start`. **Stop `typix-companion-cdc.service` before Copilot/other flashing, and restart
it after the verified runtime device returns.** The service does not reopen or select a
new tty after a disconnect. HTTPS telemetry wins while fresh; CDC telemetry becomes active
after Wi-Fi status expires (8 seconds). File/screenshot transfers currently require Wi-Fi;
CDC fallback only carries status and existing opt-in shutdown messages.

- Put files in `~/TypixDeck/shared/`. Names: 1–40 safe ASCII letters/digits/`._-`, starting
  with a letter or digit; max 24 listed files, 256 KiB each; no symlinks or subdirectories.
- ESP caches one download in PSRAM, cleared on restart. ASCII text/CSV offers a bounded
  first-page preview; other file types report cached without executing them. This is not
  a persistent app installer or ebook reader. Refresh returns to the file list.
- Screenshot captures the current Wayland desktop on request, scales/pads to 320×240,
  transfers RGB565LE and displays it at 2×. It is a static preview, not HDMI capture or VNC.
- Neither network route exposes power, MUX, flash, arbitrary shell commands or remote input.
- If installing over SSH without a desktop environment, import the actual desktop's
  `WAYLAND_DISPLAY` into the user service manager before restarting the share unit; do not
  assume a socket name or grant root framebuffer access.
- To stop: `systemctl --user disable --now typix-companion-share.service typix-companion-cdc.service`.
  User files and pairing remain available for later reuse.

Current limitation: Python's TLS server serves one request at a time with a bounded socket
timeout. Only enable it on a trusted LAN. Certificate expiry, IP changes and missing desktop
capture are visible errors, never bypassed verification or fabricated images.

## CM4 GT911 boot-probe recovery

`touch_reprobe.py` plus the supplied system service/timer retry an unbound, DT-declared
Goodix GT911 after the display has returned to the Pi. The helper reads only its product ID
at declared addresses 0x14/0x5d, then reprobes that exact client. It exits if any GT911 is
already bound. It never unbinds a driver, changes MUX/power/reset GPIO or scans the bus.

The helper is a bounded root oneshot (8 seconds); its executable must be installed root-owned
at `/usr/local/lib/typixdeck/touch_reprobe.py` and units under `/etc/systemd/system/`. It is
separate from the rootless companion installer. On the acceptance CM4, product ID 911 was
read and `Goodix Capacitive TouchScreen` registered successfully. The user confirmed Pi desktop touch input recovered. Disable with `sudo systemctl disable --now typix-touch-reprobe.timer`.

Deployment note (2026-09-30): HTTPS is active on the acceptance CM4; the CDC daemon is
not enabled permanently. Copilot currently rejects a tty already owned by another process.
Automatic pause/resume coordination is not yet implemented; do not enable CDC and then
expect concurrent flashing to work. The rootless CDC script remains available for bounded
telemetry sessions with explicit serial ownership.
