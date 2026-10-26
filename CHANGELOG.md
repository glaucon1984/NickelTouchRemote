# Changelog

## Unreleased

- Hold the first Bluetooth connection attempt after a wake, or after Bluetooth comes on,
  while Wi-Fi is on, for at most `wifi_hold` seconds (default 30). Bluetooth paging and a
  Wi-Fi transfer share the radio, and a reading-position sync right after a wake could
  fail while the mod was paging for a switched-off ring.

## v0.2.1 (2026-10-23)

- Self-repair after a crash in NickelHook's fail-safe window. A Nickel crash inside that
  window at start-up leaves the library renamed to `.failsafe`; Qt still loads it but
  NickelHook refuses to start it, so the mod was silently inert from then on. The mod
  now puts the file right when it is loaded that way, and the next restart brings it
  back. The window is also shortened from 5 to 2 seconds, `repair.sh` ships in
  `.adds/touchremote` for older installs, and the README has a troubleshooting entry.

## v0.2.0 (2026-10-11)

- Auto-connect: the last used remote is connected by the mod itself whenever Bluetooth
  is on and no remote is connected, with quick attempts right after Bluetooth comes on
  or the Kobo wakes, slower ones afterwards, and an immediate one when Nickel's settings
  page sees the remote in a scan. `auto_connect = false` disables it.

## v0.1.0 (2026-10-11)

First release.

- Touch-style Bluetooth remotes ("TikTok rings") turn pages in Kobo's reader and in
  Kobalt: driverless Bluetooth HID devices are bound to the kernel's generic HID driver,
  their simulated finger gestures become taps and swipes, and the configured action is
  delivered as the arrow key a Bluetooth keyboard would send.
- Actions `next`, `prev`, `swap` (exchange the page buttons) and `invert` (turn the screen
  upside down, page buttons follow), configurable per gesture and per key code in
  `.adds/touchremote/config.ini`.
- Keeps Nickel's Bluetooth radio and the Kobo awake while the remote is in use.
- Diagnostics through a `dumplog` flag file; removal through an `uninstall` flag file.
- Tested on a Kobo Clara BW with firmware 4.45.23792 and the WX02 ring.
