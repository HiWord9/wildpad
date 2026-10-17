# Wildpad

**The wildcard for your macropad.** Open firmware and setup app for the cheap CH552
“keys + knob” macropads: every knob click counts, every control does what you want.


These little macropads are sold under dozens of brand names: three or six mechanical
keys, a clickable rotary knob, RGB LEDs and a WCH CH552 microcontroller (stock USB ID
`1189:8890`, configured with an app called “MINI KeyBoard”). One Wildpad firmware runs
all of them, and a pad it doesn't know yet is set up by pressing its keys once. The stock firmware loses most knob clicks as soon as you turn it with
any speed, and its app can only assign a key per button. Wildpad replaces both.

## What you get

- **No lost clicks.** The knob is read in a 4 kHz timer interrupt, independent of USB,
  with bounce filtering. Turn it as fast as you like: every click arrives, in the right
  direction.
- **Any control, any action.** Each key, the knob press and each turning direction can
  send a keyboard shortcut, a media key, a mouse click, scrolling, pointer movement or
  a gamepad button, switch profiles or control the lights.
- **Combos.** Hold any control and press or turn another to do something else, for
  example *hold the knob and turn it* for brightness, *hold key 1 and press key 2* for
  a shortcut. Holding a key on its own (long press) can do something else too. A key's
  own action can wait until you let go (so it doesn't fire when you use a combo) or be
  held right away, so combos add to it.
- **Several steps.** Any action can be a short sequence: tap things, hold something
  for as long as you hold the control, wait, and do more when you let go. For example
  *tap A, tap A again, then hold RT* for a double jump and throttle in a game.
- **App switchers.** A shortcut can keep Ctrl / Alt / Win held while the key is tapped:
  on a key for as long as you hold it, on the knob for as long as you keep turning.
  *Turn the knob* and windows switch like Alt+Tab, stop and the window is picked; or
  *press the knob* to open the switcher and turn to move through it.
- **Three profiles**, each with its own light color and effect; switch with a combo,
  a key or the app.
- **Gamepad axis.** The knob can be an absolute gamepad axis (stick or triggers) with a
  configurable range and hard stop, for racing and steering games. The pad adds an Xbox
  controller (XInput, built into Windows) automatically when an action needs it, while
  the keys keep working as a keyboard. Nothing to install, no modes to switch.
- **Lights.** Off, solid, breathe, rainbow, light on press, or knob position, per profile.
- **Setup in the browser.** Click a key on the drawing, pick what it does, done: every
  change takes effect on the pad immediately and is saved on it. Undo, presets, backup
  to a file. A command-line tool does the same.
- **Safe to try.** The CH552 bootloader lives in ROM, so a macropad can always be
  re-flashed.

## Why

Measured on a stock unit, logging every HID event:

| Action | Clicks turned | Events received |
|---|---|---|
| Slow full turn | 20 | 20 |
| Fast full turn | 20 | 9, then 7 |
| Fast full turn counter-clockwise | 20 | 10 counter-clockwise **and 2 clockwise** |

Knob events never arrive closer than 47 ms apart: the stock firmware polls the encoder
from its main loop and blocks on USB for every step, so it tops out at about 21 clicks
per second and reads the wrong direction beyond that. Every open firmware for this
board we found reads the encoder the same way. Details in [docs/findings.md](docs/findings.md).
With Wildpad, the same fast turns deliver exactly 20 events each.

## Quick start (Windows)

You need [Python 3](https://www.python.org/downloads/) (`winget install Python.Python.3.12`)
and Chrome, Edge or Opera. Download the latest release and unzip it.

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
into the bootloader by itself. Windows sometimes fails to start the bootloader driver
after such a switch; the flasher then simply tries again. If it gives up, unplug the
pad and plug it back in while holding **key 1**.

### 2. Set up

Run **`configure.bat`**. It opens the setup page; click *Connect* and pick the
macropad (only needed the first time). Click a key or the knob on the drawing and
choose what it does.

## Factory settings

| Profile | Key 1 | Key 2 | Key 3 | Knob press | Knob turn |
|---|---|---|---|---|---|
| 1, cyan | previous track | play / pause | next track | mute | volume |
| 2, amber | Ctrl+C | Ctrl+V | Ctrl+Z | Enter | scroll |
| 3, teal | Win+D (desktop) | Win+E (files) | Win+Shift+S (screenshot) | Win+Tab (task view) | switch windows (Alt+Tab) |

In every profile: **hold the knob and press key 1, 2 or 3** to switch profiles,
**hold the knob and turn it** to change the brightness. Because the knob starts combos,
its own action (mute) is sent when you let go of it.

Profile 3 switches windows: **turn the knob** and Alt stays held while you turn, so the
window switcher moves along; stop for 0.8 s and the window is picked. The preset *Switch
windows: press and turn* does it the other way round: pressing the knob opens the
switcher (Alt+Tab with Alt held) and turning moves through it until you let go.

Plug the pad in while holding **key 1** to start the bootloader for flashing.

## Gamepad axis

Select the knob, set *Turning sends* to *Gamepad axis*, pick the axis and how many
clicks it takes from center to full. The pad reconnects once and Windows shows an Xbox
controller. Set the knob press to *Center the knob axis*, and turn down the game's
controller deadzone. The knob has 20 clicks per turn, so the axis moves in steps:
with 10 clicks to full, each click is 10 %. More clicks give finer steps but more
turning. Combos still work while the knob is an axis: for example *hold key 3 and
turn* can scroll without moving the axis.

## Command line

```
python tools/wildpad.py show                       # everything on the pad
python tools/wildpad.py set 1 key1 key:ctrl+c      # profile 1, key 1
python tools/wildpad.py set 2 right axis:lx:10     # profile 2, knob = stick X
python tools/wildpad.py combo add key1 key2 media:mute
python tools/wildpad.py set 1 key3 "seq:tap pad:a; wait 60; tap pad:a; hold pad:rt"
python tools/wildpad.py option profiles 1                  # more memory for combos and steps
python tools/wildpad.py light 1 breathe #ff8800
python tools/wildpad.py export backup.json         # same format as the setup page
python tools/wildpad.py actions                    # all action and key names
```

## Building

```
winget install SDCC.SDCC                  # or: apt install sdcc sdcc-ucsim
python firmware/build.py                  # -> firmware/build/wildpad.bin
python firmware/test/run_tests.py         # tests in the s51 8051 simulator
python firmware/test/check_descriptors.py # USB descriptor checks on the binary
```

The tests run the real firmware sources in a simulator with the USB layer stubbed
out: knob decoding (slow, fast, bounce, reversals), every click becoming exactly one
press and release, combos with keys and the knob, long press, profile switching and
the gamepad axis. `build.py` works around a packaging bug of SDCC 4.5 on Windows.

## Boards

Known boards: 3 keys + knob, 6 keys + knob, and a 6-key variant wired in reverse. The
board is a setting (*Settings → Board* on the setup page, or `wildpad.py board`). For
any other CH552 pad, *Another pad: find its keys* asks you to press the keys in order
and stores their pins. The memory for actions follows the number of keys: the 6-key
pad fits 2 profiles, the 3-key pad 3. See [docs/porting.md](docs/porting.md).

## How it works

| Path | What |
|---|---|
| `firmware/src/input.c` | Timer interrupt: quadrature decoder, key debounce, millisecond clock |
| `firmware/src/main.c` | Action engine: profiles, combos, long press, knob step queue, gamepad report, lights, configuration protocol |
| `firmware/src/usbdev.c`, `descr.c` | USB device with two layouts (keyboard; keyboard + gamepad) |
| `firmware/src/settings.c` | Settings in the 128-byte Data-Flash, factory settings |
| `tools/configurator.html` | Setup page (WebHID) |
| `tools/flash.py`, `winusb.py` | Flasher; talks WinUSB directly on Windows |
| `tools/wildpad.py` | Command-line tool |

With a gamepad action the pad is a composite USB device: a HID keyboard/mouse, a vendor
HID interface for configuration and live state, and an XInput interface. A Microsoft
OS descriptor gives that interface the compatible ID `XUSB10`, so Windows loads its
built-in Xbox driver for just that interface; nothing has to be installed and no
Microsoft IDs are borrowed. The configuration protocol and the settings layout are
documented in [docs/protocol.md](docs/protocol.md).

Settings live in the chip's 128-byte Data-Flash, which limits how much fits. Combos
and multi-step actions share what the profiles leave: 49 bytes with 3 profiles (a
combo takes 4, a step 3, a wait 1), 93 bytes with a single profile. The setup page
shows how much is used. Long text macros don't fit.

## Credits and license

- CH55x support code and the base of the USB handler by
  [Stefan Wagner (wagiminator)](https://github.com/wagiminator).
- XInput interface layout from [GP2040-CE](https://github.com/OpenStickCommunity/GP2040-CE).
- Bootloader protocol from chprog by Stefan Wagner, based on chflasher and wchprog.
- Pinout and bootloader entry cross-checked with
  [eccherda/ch552g_mini_keyboard](https://github.com/eccherda/ch552g_mini_keyboard)
  and [nelsonjchen/c3-touchkey-keyboard](https://github.com/nelsonjchen/c3-touchkey-keyboard).

Wildpad is free software under the [GNU GPL 3.0](LICENSE), except the firmware: it is
under [CC BY-SA 3.0](firmware/LICENSE), because it contains CH55x code by Stefan Wagner
published under that license, which requires the firmware as a whole to stay under it.
