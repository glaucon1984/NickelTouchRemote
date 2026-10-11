# NickelTouchRemote

A [NickelHook](https://github.com/pgaskin/NickelHook) mod that turns pages on a Kobo with
touch-screen style Bluetooth remotes, the cheap "TikTok ring" page turners. It works in
Kobo's own reader and in [Kobalt](https://github.com/nicoverbruggen/kobalt), because it
delivers the same arrow keys a Bluetooth keyboard would.

It is the Nickel counterpart of the touch-remote support in my
[kobo.koplugin fork](https://github.com/glaucon1984/kobo.koplugin) for KOReader.

## Why these remotes do not work out of the box

Rings such as the WX02 ("wxlj-02") announce themselves as a single-finger touch screen,
not as a keyboard: every button sends a simulated finger gesture. Two things stop them
on a Kobo:

1. The kernel tags their HID descriptor as multitouch and leaves it to a driver the Kobo
   kernel does not include, so the device connects but no input node is created.
2. Even with an input node, the buttons are touch contacts, which nothing turns into page
   turns.

The mod binds such driverless Bluetooth HID devices to the kernel's generic HID driver,
reads the contacts itself, folds each one into a tap or a swipe, and sends the arrow key
for the configured action.

## Install

1. Download `KoboRoot.tgz` (or build it, see below).
2. Connect the Kobo over USB and copy `KoboRoot.tgz` into the `.kobo` folder.
3. Eject the Kobo safely. It restarts and installs the mod.
4. Pair and connect the remote in Nickel's Bluetooth settings, as you would any remote.

To remove the mod, create an empty file called `uninstall` in `.adds/touchremote` and
restart the reader.

Tested firmware line: 4.x (Qt 5). NickelTC builds target that line. Firmware 5 and 6 use
Qt 6 and need a different toolchain; the code has nothing Qt 5 specific, but there is no
package for it.

## Default buttons (WX02 ring)

| Button | Gesture the ring sends | Action |
|---|---|---|
| lower | swipe towards larger Y (`swipe_down`) | next page |
| upper | swipe towards smaller Y (`swipe_up`) | previous page |
| middle | tap | swap next and previous (switch hands) |

Set `tap = invert` instead to turn the whole screen upside down with the middle button,
the way "invert rotation" works in KOReader: the reader can then be held with either
hand and the ring's lower button keeps turning forward, because page turns are swapped
automatically while the screen is upside down. The rotation uses the same platform call
as Nickel's own rotation and Kobalt's, so it works in both readers and stays until the
next press. Kobalt restores its own orientation setting when a book is opened or closed.

## Configuration

Copy `.adds/touchremote/config.sample.ini` to `config.ini` and edit it. The sample
documents every key. In short:

- `[gestures]`: the action for `tap`, `swipe_up`, `swipe_down`, `swipe_left` and
  `swipe_right`. Actions are `next`, `prev`, `swap`, `invert` and `none`.
- `[keys]`: actions for plain key codes, for remotes that send keys rather than touches.
- `[options]`: `swap_when_inverted`, `grab_touch_devices`, `grab_other_devices`, `inject`
  (`keys` or `view`), `power_activity` and `verbose`.

The file is read at start and again whenever a Bluetooth device connects, so switching
the remote off and on applies a change.

## Debugging

Create an empty file `dumplog` in `.adds/touchremote`, then connect the remote and press
its buttons for a minute. The mod copies the system log to
`NickelTouchRemote_<date>.log` in the drive's root and removes the flag. Set `verbose = true` first to
include every raw input event, which shows what an unknown remote sends.

## How it works

- `src/gesture.cc`: the gesture translator, with no Qt dependency. One contact becomes
  one gesture. Swipes fire as soon as the finger has moved 5% of the coordinate range,
  not when it lifts, which keeps page turns snappy. Taps fire on release. Keys that arrive
  inside a contact (the touch tool buttons) are dropped; keys outside one pass through.
- `src/touchremote.cc`: watches `/dev/input` for new Bluetooth input nodes, grabs
  touch-style ones so their simulated touches never reach the screen, and reads them with
  a socket notifier on Nickel's main thread. Bluetooth HID devices without a driver are
  handed to `hid-generic` through its `new_id` file; the ids are remembered in
  `.adds/touchremote/devices` and registered again at every start, so a known remote gets
  its input node from the kernel on its own. Bluetooth connection and power changes are
  followed over D-Bus (`org.bluez` interfaces, served by Kobo's bluedroid bridge on
  MediaTek devices) and trigger bind attempts. Each page turn also posts Nickel's
  Bluetooth heartbeat event, so the radio's inactivity timeout does not switch Bluetooth
  off while you read, and reports activity to Nickel's power manager.
- `src/main.cc`: NickelHook registration. Every Nickel symbol is optional; a missing one
  only disables the feature that needs it.

## Build

The mod builds with [NickelTC](https://github.com/pgaskin/NickelTC):

```sh
docker run --rm -v "$PWD:/src" -w /src ghcr.io/pgaskin/nickeltc:1.0 make all strip
docker run --rm -v "$PWD:/src" -w /src ghcr.io/pgaskin/nickeltc:1.0 make koboroot
```

`tools/remote-build.sh [ssh-host]` does the same on a machine that has Docker and copies
`KoboRoot.tgz` to `build/`. `make test` compiles and runs the gesture tests with the host
compiler. The GitHub workflow runs both.

## Acknowledgements

- [NickelHook](https://github.com/pgaskin/NickelHook) by Patrick Gaskin, included under
  its licence.
- [kobo-btpt](https://github.com/tsowell/kobo-btpt) by Thomas Sowell showed how a mod can
  reach Nickel's reading view and power manager.
- [Kobalt](https://github.com/nicoverbruggen/kobalt) by Nico Verbruggen documents how
  Nickel's Bluetooth heartbeat and D-Bus adapter state work on current firmware.
- [kobo.koplugin](https://github.com/OGKevin/kobo.koplugin) by OGKevin, where the
  gesture translation was first written for KOReader.

## Licence

MIT, see `LICENSE`. NickelHook keeps its own licence in `NickelHook/LICENSE`.
