# TISHRIC Knob: open firmware for the "3 keys + knob" CH552 macropad

Replacement firmware for the cheap WCH CH552G macropad with three keys and a rotary
knob (stock USB ID `1189:8890`, configured by the "MINI KeyBoard" app). Sold under
many names, this one as TISHRIC.


## Why the stock firmware drops knob steps

Measured on a real unit by logging every HID event with Raw Input timestamps:

| Action | Detents turned | Events received |
|---|---|---|
| Slow full turn | 20 | 20 |
| Fast full turn | 20 | 9, and 7 on a second try |
| Fast full turn counter-clockwise | 20 | 10 CCW **plus 2 CW** |

Knob events never arrive closer than **47 ms** apart, and the gaps fall on an 8 ms grid.
The stock firmware polls the encoder from its main loop and blocks on USB for each
step, so it can't see more than ~21 detents per second. Faster turns lose steps or read
the wrong direction. The encoder itself is fine, and the vendor app's protocol has no
speed or debounce settings at all. Details: [docs/findings.md](docs/findings.md).

Every public firmware for this board (c3-touchkey, eccherda, wagiminator's
USB-Knob) reads the encoder the same naive way, so they all still drop steps.

## What this firmware does

- **The knob is decoded in a 4 kHz timer interrupt** with a quadrature state table,
  independent of USB. No lost or reversed steps, and contact bounce is filtered out.
  Verified in the 8051 simulator (`firmware/test`).
- **Keyboard mode.** Each key, the knob press and each turn direction can send a key
  combo (Ctrl+C), a media key (volume, tracks), a mouse button or the scroll wheel.
  Every detent becomes its own key tap, and fast turns are queued instead of dropped.
  Keys stay held for as long as you hold them.
- **Wheel mode.** The macropad becomes an Xbox 360 controller (XInput, driver built into
  Windows). The knob position drives the left stick X axis, and the keys become pad
  buttons. Rocket League and other games see a normal controller.
- **Absolute position.** In keyboard mode the device also reports the live knob position
  over a vendor HID interface. It shows on the setup page and in
  `tools/tishric.py watch`, and any program can read it.
- Settings live on the device, so they follow it to any computer.

## Flashing

You need Python 3 for the scripts (`winget install Python.Python.3.12`).

### First time, from the stock firmware (case must be opened once)

1. Remove the 4 screws on the bottom and lift the bottom plate.
2. Find the **R12** pads next to the WCH chip: two empty pads with no resistor
   ([photo](docs/r12_short.jpeg)).
3. Run **`flash.bat`**. **The first time on a PC** it opens Zadig (bundled in `tools/`)
   to install the WinUSB driver for the bootloader. No macropad needs to be connected:
   *Device → Create New Device*, name `CH552 bootloader`, USB ID `4348` `55E0`,
   driver **WinUSB**, *Install Driver*. Then press Enter in the flasher window.
4. With the flasher waiting, short the R12 pads (tweezers, screw head or a paper clip)
   and, **while holding them shorted**, plug in the USB cable. You can let go after a
   second. The LEDs stay off in the bootloader, and that's normal.
   Alternative known to work: connect chip pin P1.5 to GND while plugging in.
5. The bootloader only stays for ~10 seconds, but flashing starts right away: the script
   erases, writes and verifies the firmware, and the macropad restarts on its own.

The stock firmware can't be backed up first, because the CH552 bootloader has no read
command. Bricking is very unlikely, though: the bootloader lives in ROM and is always
reachable.

### Updates (no need to open the case)

Run `flash.bat`. It tells the running firmware to jump into the bootloader by itself.
If that ever fails, hold **key 1** while plugging in USB.

## Setup

Run **`configure.bat`**. A page opens in Chrome. Click *Connect*, then click a key or
the knob on the drawing and choose what it does. Changes apply immediately, and
*Save to macropad* stores them on the device.

Command line alternative (`python tools/tishric.py help`):

```
python tools/tishric.py info
python tools/tishric.py set key1 ctrl+c
python tools/tishric.py set cw wheel:1
python tools/tishric.py watch
python tools/tishric.py mode gamepad
```

## Modes and shortcuts

| Do this | What happens |
|---|---|
| Hold the knob button for 3 s | Toggle keyboard ↔ wheel mode (LEDs fill up yellow) |
| Plug in USB while holding key 1 | Bootloader, for flashing |
| Plug in USB while holding key 3 | Start in keyboard mode once (rescue) |

**Defaults.** Keyboard mode: previous track / play-pause / next track on the keys, knob
for volume, knob press for mute. Wheel mode: key 1 = RT (throttle), key 2 = A (jump),
key 3 = B (boost), knob press = re-center, half a turn = full lock.

**LEDs in wheel mode:** green at center, and the left or right LED turns red in
proportion to how far you steer.

## Rocket League

1. Switch to wheel mode (hold the knob button for 3 seconds).
2. Check that the stick moves in `configure.bat → Wheel test` or in `joy.cpl`.
3. In game, *Settings → Controls* shows an Xbox controller. Lower *Steering
   Sensitivity* and the *Controller Deadzone* for finer control.
4. The knob has 20 detents per turn, so steering moves in steps. With "full lock" at 10,
   each detent is 10% of full lock. A larger value gives finer steps but more turning.

## Building from source

```
winget install SDCC.SDCC
python firmware/build.py                  # -> firmware/build/tishric_knob.bin
python firmware/test/run_tests.py         # simulator tests (s51)
python firmware/test/check_descriptors.py # USB descriptor checks on the binary
```

`build.py` works around a bug in the SDCC 4.5 Windows installer (`cc1` shipped
without `.exe`).

## Project layout

| Path | Contents |
|---|---|
| `firmware/src` | Firmware: `input.c` (knob ISR), `main.c` (modes), `usbdev.c` + `descr.c` (USB), `settings.c` |
| `firmware/include` | CH55x support headers by wagiminator |
| `firmware/test` | Simulator tests and descriptor checks |
| `release/tishric_knob.bin` | Prebuilt firmware |
| `tools/flash.py` | Flasher (CH55x bootloader protocol) |
| `tools/configurator.html` | Setup page (WebHID) |
| `tools/tishric.py` | Command line tool |

## Credits and licenses

- Based on the CH55x code by [Stefan Wagner (wagiminator)](https://github.com/wagiminator),
  CC BY-SA 3.0.
  Modified and new files are shared under the same terms.
- Xbox 360 descriptor from [GP2040-CE](https://github.com/OpenStickCommunity/GP2040-CE) (MIT).
- Bootloader protocol: chprog by Stefan Wagner (MIT), based on chflasher and wchprog.
- Pinout and bootloader entry cross-checked with
  [eccherda/ch552g_mini_keyboard](https://github.com/eccherda/ch552g_mini_keyboard) and
  [nelsonjchen/c3-touchkey-keyboard](https://github.com/nelsonjchen/c3-touchkey-keyboard).
- Zadig: [zadig.akeo.ie](https://zadig.akeo.ie/), GPLv3.
