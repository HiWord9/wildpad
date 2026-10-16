# Detent

**Open firmware for the cheap CH552 “3 keys + knob” macropad that never drops a knob step.**


These little macropads are sold under dozens of brand names with the same PCB: three
mechanical keys, a clickable rotary knob, three RGB LEDs and a WCH CH552G
microcontroller (stock USB ID `1189:8890`, configured with an app called
“MINI KeyBoard”). The stock firmware loses most knob steps as soon as you turn it with
any speed. Detent replaces it with a firmware that reads the knob properly, plus a
browser-based setup page.

## Why

Measured on a stock unit, logging every HID event:

| Action | Detents turned | Events received |
|---|---|---|
| Slow full turn | 20 | 20 |
| Fast full turn | 20 | 9, then 7 |
| Fast full turn counter-clockwise | 20 | 10 CCW **and 2 CW** |

Knob events never arrive closer than 47 ms apart: the stock firmware polls the encoder
from its main loop and blocks on USB for every step, so it tops out at ~21 detents per
second and reads the wrong direction beyond that. Every open firmware for this board
we found reads the encoder the same way. Details in [docs/findings.md](docs/findings.md).

With Detent, the same fast turn delivers exactly 20 events, the same counter-clockwise
turn delivers 20 counter-clockwise events in half a second, and the PC-side count
matches the knob's absolute position.

## Features

- **No lost steps.** The knob is decoded in a 4 kHz timer interrupt with a quadrature
  state table, independent of USB traffic, with bounce filtering. Every detent is
  queued and replayed as its own key press.
- **Keyboard mode.** Any key, the knob press and each turn direction can send a key
  combo, a media key, a mouse button or scroll.
  - **Fn layer**: hold a key and every key and the knob do something else
    (by default: hold key 2 and the knob scrolls instead of changing the volume).
  - **Tap or hold**: a key can do one thing on a quick tap and another when held.
- **Wheel mode.** The macropad becomes an Xbox controller (XInput, driver built into
  Windows) *and* keeps its keyboard: the knob is an absolute steering axis with a
  configurable lock and hard stop, and each key can be a gamepad button, a keyboard key
  or “re-center”. Works in Rocket League and any other XInput game.
- **Lighting**: off, react to keys, solid, rainbow or breathing, any color; in wheel
  mode the LEDs show the steering position.
- **Setup in the browser**: a live drawing of the macropad, settings stored on the
  device, export/import to a file. Also a command-line tool.
- **Safe to try**: the CH552 bootloader lives in ROM, so a macropad can always be
  re-flashed.

## Quick start (Windows)

You need [Python 3](https://www.python.org/downloads/) (`winget install Python.Python.3.12`)
and Chrome or Edge. Download the latest release and unzip it.

### 1. Flash

**First time, from the stock firmware** (the case has to be opened once):

1. Remove the four screws on the bottom and lift the bottom plate.
2. Find the **R12** pads next to the WCH chip: two empty pads with no resistor
   ([photo](docs/r12_short.jpeg)).
3. Run **`flash.bat`**. The first time on a PC it opens Zadig to install the WinUSB
   driver for the bootloader; no macropad needs to be connected for that:
   *Device → Create New Device*, name `CH552 bootloader`, USB ID `4348` `55E0`,
   driver **WinUSB**, *Install Driver*. Then press Enter in the flasher window.
4. Short the R12 pads (tweezers or a paper clip) and, **while holding them shorted**,
   plug in the USB cable. The LEDs stay dark in the bootloader; that's normal.
   (Connecting chip pin P1.5 to GND while plugging in also works.)
5. Flashing starts by itself and takes a few seconds.

The stock firmware can't be backed up first: the CH552 bootloader has no read command.

**Updates** need no screwdriver: run `flash.bat` and it switches the running firmware
into the bootloader by itself. If that ever fails, hold **key 1** while plugging in.

### 2. Set up

Run **`configure.bat`**, click *Connect* and pick the macropad. Click a key or the
knob on the drawing and choose what it does; changes apply immediately, *Save to
macropad* keeps them.

## Using it

| Do this | What happens |
|---|---|
| Hold the knob button for 3 s | Toggle keyboard ↔ wheel mode (LEDs fill up yellow) |
| Plug in while holding key 1 | Bootloader, for flashing |
| Plug in while holding key 3 | Start in keyboard mode once (rescue) |

**Defaults.** Keyboard mode: previous track / play-pause / next track, knob for
volume, knob press for mute; hold key 2 and the knob scrolls. Wheel mode: key 1 = RT
(throttle), key 2 = A (jump), key 3 = B (boost), knob press = re-center, half a turn
= full lock.

### Rocket League

Switch to wheel mode, check the stick moves in the setup page's *Wheel test* (or in
`joy.cpl`), then start the game; it sees an Xbox controller. Turn *Controller Deadzone*
down in the game's control settings. The knob has 20 detents per turn, so steering
moves in steps: with full lock at 10 detents each step is 10 %. A larger value gives
finer steps but more turning.

### Command line

```
python tools/detent.py info
python tools/detent.py set key1 ctrl+c
python tools/detent.py set cw wheel:1 --fn
python tools/detent.py set key2 fn --hold
python tools/detent.py set key3 pad:x --wheel
python tools/detent.py option led rainbow
python tools/detent.py watch
python tools/detent.py export backup.json
```

## Building

```
winget install SDCC.SDCC                  # or: apt install sdcc sdcc-ucsim
python firmware/build.py                  # -> firmware/build/detent.bin
python firmware/test/run_tests.py         # tests in the s51 8051 simulator
python firmware/test/check_descriptors.py # USB descriptor checks on the binary
```

The tests run the real firmware sources in a simulator with the USB layer stubbed out:
knob decoding (slow, fast, bounce, reversals), the keyboard pipeline (every detent
becomes exactly one press + release), tap vs. hold, the Fn layer and wheel mode.
`build.py` works around a packaging bug of SDCC 4.5 on Windows.

## Other boards

Everything board specific is in [firmware/src/config.h](firmware/src/config.h). The
settings format has room for up to 6 buttons. See [docs/porting.md](docs/porting.md).

## How it works

| Path | What |
|---|---|
| `firmware/src/input.c` | Timer interrupt: quadrature decoder, key debounce, millisecond clock |
| `firmware/src/main.c` | Action engine: layers, tap/hold, knob step queue, XInput report, LEDs, config protocol |
| `firmware/src/usbdev.c`, `descr.c` | USB device with three personalities (keyboard, composite wheel, legacy Xbox 360) |
| `firmware/src/settings.c` | Settings in the 128-byte Data-Flash |
| `tools/configurator.html` | Setup page (WebHID) |
| `tools/flash.py`, `winusb.py` | Flasher; talks WinUSB directly on Windows |
| `tools/detent.py` | Command-line tool |

The wheel mode is a composite USB device: a HID keyboard/mouse, a vendor HID interface
for configuration and live state, and an XInput interface. A Microsoft OS descriptor
gives that interface the compatible ID `XUSB10`, so Windows loads its built-in Xbox
driver for just that interface; nothing has to be installed and no Microsoft IDs are
borrowed. The configuration protocol is documented in [docs/protocol.md](docs/protocol.md).

## Credits and license

- CH55x support code and the base of the USB handler by
  [Stefan Wagner (wagiminator)](https://github.com/wagiminator).
- Xbox 360 descriptor layout from [GP2040-CE](https://github.com/OpenStickCommunity/GP2040-CE).
- Bootloader protocol from chprog by Stefan Wagner, based on chflasher and wchprog.
- Pinout and bootloader entry cross-checked with
  [eccherda/ch552g_mini_keyboard](https://github.com/eccherda/ch552g_mini_keyboard)
  and [nelsonjchen/c3-touchkey-keyboard](https://github.com/nelsonjchen/c3-touchkey-keyboard).

Wildpad is free software under the [GNU GPL 3.0](LICENSE), except the firmware: it is
under [CC BY-SA 3.0](firmware/LICENSE), because it contains CH55x code by Stefan Wagner
published under that license, which requires the firmware as a whole to stay under it.
