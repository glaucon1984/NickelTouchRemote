# Changelog

## v0.2.1 (2026-10-23)

- NickelHook fail-safe window shortened from 5 to 2 seconds. A Nickel crash inside that
  window at start-up leaves the library renamed and the mod silently gone until it is
  reinstalled; the shorter window makes that less likely. README troubleshooting entry.

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
