# Configuration protocol (version 2)

The firmware exposes a vendor HID interface (usage page `0xFF54`, usage `0x01`) on
interface 1 in keyboard and wheel mode. It has two reports, neither uses report IDs:

| Report | Size | Direction | Use |
|---|---|---|---|
| Input | 8 bytes | device → host, interrupt EP3 | live state, sent on every change |
| Feature | 40 bytes | both, control transfers | commands |

## Live state (input report)

| Byte | Meaning |
|---|---|
| 0–1 | knob position relative to the current center, int16 little endian |
| 2 | buttons, bit *i* = button *i* pressed |
| 3 | running mode (0 keyboard, 1 wheel) |
| 4 | 1 while the Fn layer is active |
| 5–7 | reserved |

## Commands (feature report)

The host writes a feature report `[cmd, seq, payload...]` and then reads the feature
report until it returns `[cmd, seq, status, protocol, payload...]` with matching `cmd`
and `seq` (poll every few milliseconds; `seq` should cycle through 1…250, 0xFF marks a
response that is still being prepared). `protocol` is 2. `status` 0 means success;
1 unknown command, 2 invalid settings, 3 bad offset/length.

| cmd | Name | Request payload | Response payload |
|---|---|---|---|
| 1 | INFO | – | fw major, minor, patch, board id, button count, knob button index, settings size, running mode, USB personality, LED count |
| 2 | READ | offset, length (≤ 32) | bytes of the live settings |
| 3 | WRITE | offset, length (≤ 32), bytes | – (writes a staging buffer) |
| 4 | COMMIT | – | – (validates staging and makes it live; status 2 if invalid) |
| 5 | SAVE | – | – (stores live settings in Data-Flash) |
| 6 | DEFAULTS | – | – (live settings = factory defaults, not saved) |
| 7 | STATUS | – | like the live input report |
| 8 | RECENTER | – | – |
| 9 | REBOOT | – | – (device reconnects; use after changing the mode) |
| 10 | BOOTLOADER | – | – (device reappears as the CH552 bootloader `4348:55E0`) |

To change settings: READ everything, modify, WRITE all chunks, COMMIT, then SAVE.
A mode change takes effect after REBOOT.

Protocol 1 (the first, unnamed release) used a single 30-byte settings block, `READ = 1`
and `BOOTLOADER = 8`; the flasher detects it by the protocol byte of the cmd 1 response.

## Settings (104 bytes)

Mirrors `settings_t` in `firmware/src/settings.h`.

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | magic `0xA5` |
| 1 | 1 | settings version `2` |
| 2 | 1 | mode: 0 keyboard, 1 wheel |
| 3 | 1 | flags: 1 reverse knob, 2 half steps, 4 hard stop, 8 legacy Xbox 360 identity, 16 disable the 3 s mode gesture |
| 4 | 1 | steering: detents from center to full lock (1–100) |
| 5 | 1 | LED brightness (0–255) |
| 6 | 1 | key tap length in ms (1–50) |
| 7 | 1 | debounce in ms (1–30) |
| 8 | 1 | hold threshold in 10 ms units (10–100) |
| 9 | 1 | detents per knob revolution (informational) |
| 10 | 1 | LED effect: 0 off, 1 react to keys, 2 solid, 3 rainbow, 4 breathe |
| 11 | 1 | LED hue: 0–191 color wheel (0 red, 64 green, 128 blue), ≥ 192 white |
| 12 | 1 | steering axis: 0 LX, 1 LY, 2 RX, 3 RY, 4 triggers |
| 13 | 3 | reserved |
| 16 | 24 | keyboard base layer: 8 actions (buttons 0–5, then knob CW, CCW) |
| 40 | 24 | keyboard Fn layer, same layout |
| 64 | 18 | “when held” actions for buttons 0–5 (type 0 = disabled) |
| 82 | 18 | wheel mode actions for buttons 0–5 |
| 100 | 3 | reserved |
| 103 | 1 | checksum: sum of bytes 0–102, modulo 256 |

### Actions (3 bytes: type, p1, p2)

| Type | Name | p1 / p2 |
|---|---|---|
| 0 | none | |
| 1 | key | p1 modifier bits (Ctrl, Shift, Alt, Win, then right-hand ones), p2 HID keycode |
| 2 | media | consumer usage, p1 low byte, p2 high byte |
| 3 | mouse button | p1 bits: 1 left, 2 right, 4 middle, 8 back, 16 forward |
| 4 | scroll | p1 notches, signed |
| 5 | horizontal scroll | p1 notches, signed |
| 6 | gamepad button (wheel mode) | p1: 1 A, 2 B, 3 X, 4 Y, 5 LB, 6 RB, 7 LT, 8 RT, 9 Back, 10 Start, 11 LS, 12 RS, 13–16 D-pad up/down/left/right, 17 Guide |
| 7 | re-center (wheel mode) | |
| 8 | Fn layer while held (keyboard mode) | |
| 9 | switch keyboard / wheel mode | |
