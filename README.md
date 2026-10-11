# NickelTouchRemote

Turn pages on a Kobo with the cheap "TikTok ring" Bluetooth page turners, inside Kobo's
own software. No KOReader needed: the ring works in Kobo's reader and in
[Kobalt](https://github.com/nicoverbruggen/kobalt), and the home screen and menus are
untouched.

NickelTouchRemote is a [NickelHook](https://github.com/pgaskin/NickelHook) mod, the same
kind of add-on as NickelMenu. It installs like any other Kobo mod and can be removed again
with an empty file. It is the Nickel-side counterpart of the touch-remote support in my
[kobo.koplugin fork](https://github.com/glaucon1984/kobo.koplugin) for KOReader.

## Why these rings do not work on a Kobo

Rings such as the WX02 ("wxlj-02", sold under many names) do not present themselves as a
keyboard. They pretend to be a single-finger touch screen: every button sends a simulated
finger gesture, a swipe for the two page buttons and a tap for the middle one. On a Kobo
that fails twice:

1. The kernel files their HID descriptor under a multitouch driver that the Kobo kernel
   does not include, so the ring connects but no input device is ever created.
2. Even with an input device, the buttons arrive as touch contacts, which nothing turns
   into page turns.

The mod fixes both. It hands such driverless Bluetooth HID devices to the kernel's generic
HID driver, reads the contacts itself, folds each one into a tap or a swipe, and sends the
arrow key that a Bluetooth keyboard would send for the configured action. Kobo's reader
and Kobalt already turn pages on those keys.

## Compatibility

| | Status |
|---|---|
| Kobo Clara BW, firmware 4.45.23792 | Tested, daily use |
| Other Kobo models on firmware 4.x (Qt 5) | Should work, untested. The mod checks nothing device specific |
| Firmware 5.x and 6.x (Qt 6) | Not available. NickelTC builds only the Qt 5 line; the code itself is not Qt 5 specific |
| WX02 ring (three buttons, classic Bluetooth, Apple vendor id `05AC:0220`) | Tested |
| Other touch-style remotes | Should work; the gesture logic is generic. See [Other remotes](#other-remotes) |
| Ordinary keyboard-style remotes | Not needed: Nickel handles those natively. The mod leaves them alone unless you map their keys |

It runs alongside NickelMenu, NickelClock, NickelDissolve, NickelHome, NickelTypeFix and
Kobalt on my device. It hooks no Nickel function, so it cannot conflict with mods that do.

## Install

1. Download `KoboRoot.tgz` from the [latest release](../../releases/latest).
2. Connect the Kobo over USB and copy `KoboRoot.tgz` into the hidden `.kobo` folder of
   the drive.
3. Eject the Kobo safely. It restarts and installs the mod; a folder
   `.adds/touchremote` appears on the drive.
4. Pair the ring in Nickel's Bluetooth settings as you would any page turner: Bluetooth on,
   put the ring in pairing mode, pick it from the list. A ring that was paired before,
   for example from KOReader, reconnects on its own without showing up in a scan.
5. Open a book and press the buttons.

## Using it

The defaults match the WX02 ring:

| Ring button | What the ring sends | Default action |
|---|---|---|
| lower | swipe towards larger Y (`swipe_down`) | next page |
| upper | swipe towards smaller Y (`swipe_up`) | previous page |
| middle | tap | `swap`: exchange next and previous until the next press |

### Why `swap` and `invert` exist

The ring is meant to sit on a finger, but it can also be built into a case so that its
buttons become physical page buttons. I use it in my
[Kobo Clara BW/Colour turn-page buttons case](https://makerworld.com/en/models/3400159-kobo-clara-bw-color-turn-page-buttons-case),
a 3D-printed case with two thumb buttons on one edge that press the ring inside. Buttons
on one edge are right for one hand only. To read with the other hand you turn the reader
around, and then the button that used to turn forward turns back. The middle button is
there to fix that, in one of two ways:

- **`swap`** (default) only exchanges the two page buttons.
- **`invert`** turns the whole screen upside down, the way "invert rotation" works in
  KOReader, and the page buttons follow the screen automatically. The reader can then be
  held either way round and the lower button keeps turning forward. The screen stays
  inverted on Nickel's home screen and in Kobo's reader until the next press. Kobalt
  applies its own saved orientation when a book opens, so press the middle button once
  more there.

Set `tap = invert` in the configuration to get the second behaviour.

### Connecting

Nickel only connects a remote when you pick it on its Bluetooth settings page, and the
ring does not initiate the connection either. The mod takes that over:

- **The first time**, connect a new remote by hand once: Bluetooth on, settings page, pick
  the ring. The mod learns it at that moment and remembers it in
  `.adds/touchremote/remotes`.
- **The last used remote reconnects by itself.** Whenever Bluetooth is on and no remote is
  connected, the mod asks the Bluetooth daemon to connect it: right away when Bluetooth
  comes on or the Kobo wakes from sleep, then every 20 seconds for the first attempts and
  every minute after that. Switch the ring on and it is connected a few seconds later,
  with no tap on the Kobo.
- **Another known remote needs a scan.** The Kobo's Bluetooth stack reaches only the
  device it connected last with a direct request; a different one has to be seen in a
  scan first. Open Nickel's Bluetooth settings page, which makes Nickel scan, and the mod
  connects the remote as soon as it appears, without a tap. From then on that remote is
  the last used one and reconnects by itself.

Nickel's own Bluetooth timeout still switches the radio off when nothing is connected for
a while, as before. `auto_connect = false` turns the automatic connection off.

While the ring is in use, the mod tells Nickel that a Bluetooth remote is active, so
Nickel's Bluetooth inactivity timer does not switch the radio off mid-chapter, and it
reports the presses as activity so the Kobo does not fall asleep while you read with the
ring alone.

## Configuration

Copy `.adds/touchremote/config.sample.ini` to `config.ini` in the same folder and edit it
with any text editor. The file is read when the mod starts and again every time a
Bluetooth device connects, so switching the ring off and on applies a change. Lines
starting with `;` are comments.

```ini
[gestures]
tap = swap            ; next, prev, swap, invert or none
swipe_up = prev
swipe_down = next
swipe_left = none
swipe_right = none

[keys]
; Plain keys of the remote, by Linux key code, for remotes that send keys.
; 115 = next         ; KEY_VOLUMEUP
; 114 = prev         ; KEY_VOLUMEDOWN

[options]
swap_when_inverted = true   ; negate next/prev while the screen is upside down
grab_touch_devices = true   ; keep touch-style remotes away from Nickel (leave on)
grab_other_devices = false  ; also take keyboard-style remotes (only with [keys])
inject = keys               ; keys: arrow keys; view: call Nickel's reader directly
power_activity = true       ; count presses as activity for Nickel's sleep timer
auto_connect = true         ; reconnect the last used remote by ourselves
verbose = false             ; log every raw input event
```

Actions:

| Action | Effect |
|---|---|
| `next`, `prev` | Turn the page. Delivered as the Right and Left arrow keys |
| `swap` | Exchange `next` and `prev` until the next `swap` |
| `invert` | Turn the screen upside down, or back |
| `none` | Ignore the button |

`inject = view` makes the page turn call Kobo's reading view directly instead of sending
keys, and falls back to keys outside it. It exists as a workaround in case a firmware
version ignores the arrow keys; the default has worked everywhere I tried.

## Other remotes

Any Bluetooth device that reports a touch contact (an X/Y position with a touch button)
goes through the gesture translator, so a different touch-style remote should work with
the same five gestures. To see what an unknown remote sends:

1. Set `verbose = true` in `config.ini`.
2. Create an empty file called `dumplog` in `.adds/touchremote`.
3. Connect the remote and press every button a few times. A minute after the remote
   connected, the mod copies the system log to `NickelTouchRemote_<date>.log` in the root
   of the drive and removes the flag.

The log shows every raw event (`type`, `code`, `value`) and the gesture or key the mod
made of it. Keys that arrive outside a touch contact can be mapped in `[keys]` by their
code; set `grab_other_devices = true` if the device is a plain keyboard-style remote that
Nickel would otherwise handle as well.

## Troubleshooting

- **The ring does not appear when scanning.** A ring that is already bonded to the Kobo
  does not advertise; it just reconnects when switched on. Watch Nickel's Bluetooth icon
  instead, or put the ring in pairing mode as its manual describes.
- **The ring connects but nothing happens.** Make the log dump described above and look
  for `touchremote:` lines. `opened /dev/input/eventN 'WX02'` means the mod has the ring;
  without it, look for `binding hid-generic` and what follows.
- **Bluetooth looks dead after installing a mod** (grey toggle, empty device list). That
  was a bug in the first test build of this mod, fixed before the first release. A
  restart of the Kobo brings the radio back.
- **The screen is upside down and I want it back.** Press the ring's middle button
  again, or set `tap = swap` and reconnect the ring.

## Uninstall

Create an empty file called `uninstall` in `.adds/touchremote` and restart the Kobo. The
mod removes its library and the Kobo starts normally. The `.adds/touchremote` folder can
then be deleted.

## How it works

- `src/gesture.cc` is the gesture translator, with no Qt dependency. One contact becomes
  one gesture. Swipes fire as soon as the finger has moved 5% of the coordinate range, not
  when it lifts, which keeps page turns snappy; taps fire on release. Keys that arrive
  inside a contact (the touch tool buttons) are dropped; keys outside one pass through to
  the `[keys]` map.
- `src/touchremote.cc` runs on Nickel's main thread. It watches `/dev/input` for new
  Bluetooth input nodes, grabs touch-style ones exclusively so their simulated touches
  and the ring's stray power key never reach Nickel, and reads them with a socket
  notifier. Bluetooth HID devices without a driver are handed to `hid-generic` through its
  `new_id` file; the ids are remembered in `.adds/touchremote/devices` and registered
  again at every start, so a known remote gets its input node from the kernel on its
  own. Bluetooth connection and power changes are followed over D-Bus (the `org.bluez`
  interfaces, served by Kobo's bluedroid bridge on MediaTek devices) without ever
  activating the daemon, which Nickel must start itself. The remotes seen connected are
  remembered in `.adds/touchremote/remotes`, and the last one is reconnected with
  `Device1.Connect` while Bluetooth is on and nothing is connected; a wall-clock jump
  between two timer ticks detects a wake from sleep. Page turns post Nickel's
  Bluetooth heartbeat event and a power-manager activity event; `invert` uses the same
  platform call as Nickel's and Kobalt's rotation.
- `src/main.cc` is the NickelHook registration. Every Nickel symbol is optional; a missing
  one only disables the feature that needs it, and nothing is hooked.

## Building

The mod builds with [NickelTC](https://github.com/pgaskin/NickelTC):

```sh
docker run --rm -v "$PWD:/src" -w /src ghcr.io/pgaskin/nickeltc:1.0 make all strip
docker run --rm -v "$PWD:/src" -w /src ghcr.io/pgaskin/nickeltc:1.0 make koboroot
```

`tools/remote-build.sh [ssh-host]` does the same on a machine that has Docker and copies
`KoboRoot.tgz` to `build/`. `make test` compiles and runs the gesture tests with the host
compiler. The GitHub workflow runs both and attaches `KoboRoot.tgz` to every build.

## Acknowledgements

- [NickelHook](https://github.com/pgaskin/NickelHook) by Patrick Gaskin, included under
  its licence, and [NickelTC](https://github.com/pgaskin/NickelTC) for the toolchain.
- [kobo-btpt](https://github.com/tsowell/kobo-btpt) by Thomas Sowell showed how a mod can
  reach Nickel's reading view and power manager.
- [Kobalt](https://github.com/nicoverbruggen/kobalt) and
  [NickelRotate](https://github.com/nicoverbruggen/NickelRotate) by Nico Verbruggen
  document how Nickel's Bluetooth heartbeat, D-Bus adapter state and screen rotation work
  on current firmware.
- [kobo.koplugin](https://github.com/OGKevin/kobo.koplugin) by OGKevin, where the gesture
  translation was first written for KOReader.

## Licence

MIT, see `LICENSE`. NickelHook keeps its own licence in `NickelHook/LICENSE`.
