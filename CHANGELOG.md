# Changelog

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
