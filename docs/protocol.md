# Configuration protocol (version 3, settings version 5)

The firmware exposes a vendor HID interface (usage page `0xFF54`, usage `0x01`) on
interface 1 whenever it runs as a keyboard (with or without the gamepad). It has two
reports, neither uses report IDs:

| Report | Size | Direction | Use |
|---|---|---|---|
| Input | 8 bytes | device → host, interrupt EP3 | live state, sent on every change |
| Feature | 40 bytes | both, control transfers | commands |

## Live state (input report)

| Byte | Meaning |
|---|---|
| 0–1 | knob position, int16 little endian: relative to the axis center, limited to the axis range, when the knob is a gamepad axis; otherwise relative to the last center |
| 2 | buttons, bit *i* = button *i* pressed |
| 3 | active profile (0-based) |
| 4 | flags: 1 gamepad interface present, 2 knob is a gamepad axis, 4 lights on |
| 5–7 | reserved |

## Commands (feature report)

The host writes a feature report `[cmd, seq, offset, length, data...]` and then reads
the feature report until it returns `[cmd, seq, status, protocol, payload...]` with
matching `cmd` and `seq` (poll every few milliseconds; `seq` should cycle through
1…127, 0xFF marks a response that is still being prepared). `protocol` is 3.
`status` 0 means success; 1 unknown command, 2 invalid settings, 3 bad offset/length.

| cmd | Name | Request | Response payload |
|---|---|---|---|
| 1 | INFO | – | see below |
| 2 | READ | offset, length (≤ 32) | bytes of the live settings |
| 3 | WRITE | offset, length (≤ 32), bytes | – (writes a staging buffer) |
| 4 | COMMIT | – | byte 0: 1 if the USB layout must change (validates staging and makes it live; status 2 if invalid) |
| 5 | SAVE | – | byte 0: as COMMIT (stores the live settings in Data-Flash) |
| 6 | DEFAULTS | – | byte 0: as COMMIT (live settings = factory settings, not saved) |
| 7 | STATUS | – | bytes 0–4 like the live input report; bytes 5, 6 the raw P1 and P3 port pins (to find a board's keys); bytes 8–16 the keyboard report being sent (report id, modifiers, reserved, 6 keys) |
| 8 | RECENTER | – | – (knob axis center = current position) |
| 9 | REBOOT | – | – (device reconnects) |
| 10 | BOOTLOADER | offset 1: update layout | – (offset 0: device reappears as the CH552 bootloader `4348:55E0`, which leaves again after ~10 s without a command. Offset 1: it reappears as a bulk device with serial `WILDPADU`, see below) |
| 11 | PROFILE | offset = profile (0-based) | – (switch the active profile) |
| 13 | BOARD | offset = board (1 … 3), or 0 with the 10-byte pin map as data | byte 0: as COMMIT (live settings = factory settings for that board, not saved; status 2 if the map is invalid) |

To change settings: READ everything, modify, WRITE all chunks, COMMIT, then SAVE. If
SAVE reports that the USB layout must change (an action now needs the gamepad, or no
longer does), send REBOOT; the device reconnects with the new layout.

INFO payload:

| Byte | Meaning |
|---|---|
| 0–2 | firmware version major, minor, patch |
| 3 | board: 1 … 3 known board, 0 own pin map (see Boards) |
| 4 | buttons (keys + knob switch) |
| 5 | index of the knob switch |
| 6 | settings size (128) |
| 7 | maximum number of profiles |
| 8 | USB layout: 0 keyboard, 1 keyboard + gamepad |
| 9 | number of LEDs |
| 10 | bytes for records (combos, sequences) with all profiles in use |
| 11 | offset of the first profile |
| 12 | size of one profile |
| 13 | offset of the records with all profiles in use (each unused profile moves it back by the profile size) |
| 14 | settings version (5) |
| 15 | active profile |
| 16 | inputs (buttons, plus 2 when the board has a knob) |

Older firmware: protocol 2 (early previews) had a fixed 104-byte layout with
keyboard and wheel modes; protocol 1 used `READ = 1` and `BOOTLOADER = 8`. The flasher
recognises both by the protocol byte of the cmd 1 response.

## Update layout

After BOOTLOADER with offset 1 the pad reconnects as `1189:8890`, serial `WILDPADU`,
one vendor interface with bulk endpoints `0x82` / `0x02` (64 bytes), like the CH552's
ROM bootloader. Microsoft OS 1.0 descriptors (compatible ID `WINUSB`) make Windows bind
WinUSB to it without any driver install. The host opens it and sends the vendor
request `bmRequestType 0x40, bRequest 0x5B` (no data); 5 ms later the firmware hands
the live USB connection to the ROM bootloader's main loop, which then takes the usual
CH55x flashing commands on endpoint 2 (as `tools/flash.py` sends them). Without that
request the firmware restarts normally after 15 s. Only the v2.50 ROM is handed over
to; on other ROMs the pad starts its bootloader the usual way.

Why: when the ROM bootloader connects by itself, Windows' WinUSB driver asks it
GET_STATUS, which it stalls, and the CH552 then often stalls the next request too, so
Windows fails to start the driver (Device Manager: code 10). In the update layout
the firmware answers every request Windows sends; the one stall (device qualifier,
which a full-speed device must stall) is cleared after 5 ms, before the next request.

## Settings (128 bytes)

Inputs are numbered: buttons `0 … B-1` (B = buttons from INFO), then *knob right* (B)
and *knob left* (B + 1) on boards with a knob. A profile is 4 + 3 × inputs bytes. The
profiles start at offset 13 (23 with an own pin map); INFO reports all of these for
the live settings. Sizes below are for the 3-key board (6 inputs, profiles of 22 bytes).

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | magic `0xA5` |
| 1 | 1 | settings version `5` |
| 2 | 1 | flags: 1 reverse knob, 2 half-step knob (other bits reserved, keep 0) |
| 3 | 1 | LED brightness (0–255) |
| 4 | 1 | hold time for long presses, 10 ms units (10–100) |
| 5 | 1 | debounce in ms (1–30) |
| 6 | 1 | tap length in ms (1–50); gamepad buttons are tapped for at least 40 ms |
| 7 | 1 | knob clicks per turn (only for drawing) |
| 8 | 1 | profile after power-up (0-based) |
| 9 | 1 | light animation speed (1–10) |
| 10 | 1 | switcher hold after turning: what a knob turn holds (type 11 modifiers, *hold* steps) is let go this long after the last click, 100 ms units (2–30; 0 = 8) |
| 11 | 1 | profiles in use, 1 … max (0 = all) |
| 12 | 1 | board: 1 … 3 known board, 0 own pin map (see Boards) |
| 13 | 10 | pin map, only when byte 12 is 0 |
| 13 / 23 | 22 × n | the n profiles in use: red, green, blue, light effect, then one action per input (bit 0x80 of a button's type: right away, see below) |
| after | rest | records: combos and sequences, packed, then `0xFF` |
| 127 | 1 | checksum: sum of bytes 0–126, modulo 256 |

Every profile that is not in use leaves its bytes to the records: on the 3-key board
3 profiles leave 48 bytes for combos and sequences, 1 profile 92. The 6-key board has
9 inputs, profiles of 31 bytes and room for 2 of them (52 bytes of records).

### Boards

| Board | Keys (in order) | Knob switch | LEDs |
|---|---|---|---|
| 1 | P1.1, P1.7, P1.6 | P3.3 | 3 |
| 2 | P3.2, P1.4, P1.5, P1.6, P1.7, P1.1 | P3.3 | 6, chain runs from the last key |
| 3 | P1.1, P1.7, P1.6, P1.5, P1.4, P3.2 | P3.3 | 6 |

All of them have the knob phases on P3.1 (A) and P3.0 (B) and the WS2812 chain on P3.4.
An own pin map (board 0) is 10 bytes: byte 0 = buttons (keys + knob switch, 1–8) |
knob switch index << 4 (15: no switch); byte 1 = LEDs (0–8) | 0x10 chain runs from the
last key | 0x20 no knob; bytes 2–9 = pin of each button, P1.*x* = *x*, P3.*x* = 8 + *x*,
unused `0xFF`. LED *i* sits under button *i*.

Light effects: 0 off, 1 solid, 2 breathe, 3 rainbow, 4 light on press, 5 knob position
(gamepad axis; solid otherwise).

### Records

Records follow each other without gaps; the first byte tells the kind:

| First byte | Record | Size |
|---|---|---|
| `0x00`–`0x7F` | combo `[held << 4 \| trigger, profile << 5 \| action type, p1, p2]` | 4 |
| `0x80`–`0xFE` | sequence `[0x80 \| id, length, steps…]` | 2 + length |
| `0xFF` | end of the list | |

**Combos.** While *held* (a button) is pressed, *trigger* (any input) does the action
instead of its own. `held == trigger` is a long press of that button. Profile 7 means
all profiles; a combo of the active profile wins over an all-profiles combo with the
same controls.

A button that starts combos or has a long press sends its own action when it is
released (as a tap), unless a combo was used while it was held. If bit `0x80` of the
button's action type in the profile is set ("right away"), the action is pressed at
once and held until release instead, and combos work on top of it; a long press is
then ignored.

**Sequences.** An action of type 12 runs sequence *p1*. Its steps run one after the
other; each step's first byte is `op | x`:

| op | Step | Size |
|---|---|---|
| `0x00` | tap action of type *x*, then p1, p2: pressed and released (gamepad buttons for at least 40 ms, with a 40 ms gap after) | 3 |
| `0x20` | hold action of type *x*, then p1, p2: stays pressed until the input is let go; on a knob turn until the knob stops | 3 |
| `0x40` | wait *x* × 20 ms (x = 1–31; longer waits are several steps) | 1 |
| `0x60` | the steps after this one run when the input is let go | 1 |

A sequence can't contain another one. On a knob turn the steps run for every click;
steps after `0x60` don't apply there. Example, "tap A twice, then hold RT while the
key is held, and tap X when it is let go":
`80 0E 06 01 00 43 06 01 00 26 08 00 60 01 00 1B`.

### Actions (3 bytes: type, p1, p2)

| Type | Name | p1 / p2 |
|---|---|---|
| 0 | none | |
| 1 | key | p1 modifier bits (1 Ctrl, 2 Shift, 4 Alt, 8 Win, then the right-hand ones), p2 HID keycode (0 = modifiers only) |
| 2 | media | consumer usage, p1 low byte, p2 high byte |
| 3 | mouse button | p1 bits: 1 left, 2 right, 4 middle, 8 back, 16 forward |
| 4 | scroll | p1 vertical notches, p2 horizontal notches, signed (positive = up / right) |
| 5 | move pointer | p1 x, p2 y, signed |
| 6 | gamepad button | p1: 1 A, 2 B, 3 X, 4 Y, 5 LB, 6 RB, 7 LT, 8 RT, 9 Back, 10 Start, 11 LS, 12 RS, 13–16 D-pad up/down/left/right, 17 Guide |
| 7 | gamepad axis | knob right only: p1 axis (0 LX, 1 LY, 2 RX, 3 RY, 4 triggers) + 0x80 for no hard stop, p2 clicks from center to full (1–100) |
| 8 | center the axis | |
| 9 | profile | p1 profile (0-based), 0xF0 next, 0xF1 previous; p2 0 switch, 1 only while held |
| 10 | lights | p1 0 on/off, 1 brighter, 2 dimmer (brightness changes are stored after 3 s) |
| 11 | key, modifiers held | like type 1, but Ctrl / Alt / Win (not Shift) stay held: on a button while it is held (the key and Shift are tapped once), on a knob turn until the knob has stopped (the key and Shift are tapped on every click). For Alt+Tab-style switchers. |
| 12 | sequence | p1 sequence id |

Keys, media keys, mouse buttons and gamepad buttons stay pressed while their input is
held; knob clicks press and release them once per click. The gamepad interface is
present whenever any action, combo or step uses type 6 or 7.

## File format

`wildpad.py export` and the setup page's *Save settings to a file* write the same JSON:

```json
{
  "format": "wildpad-settings", "version": 5,
  "board": { "id": 1, "inputs": ["key1", "key2", "key3", "knob", "right", "left"] },
  "options": { "brightness": 128, "holdMs": 300, "debounceMs": 5, "tapMs": 4,
               "clicksPerTurn": 20, "profiles": 3, "startProfile": 1, "lightSpeed": 5,
               "turnHoldMs": 800, "reverseKnob": false, "halfStep": false },
  "profiles": [ { "color": "#00aaff", "light": "solid",
                  "actions": { "key1": "seq:tap pad:a; wait 60; tap pad:a; hold pad:rt; on release; tap key:x",
                               "knob": "media:mute",
                               "right": "media:volume-up", "left": "media:volume-down" },
                  "rightAway": [] } ],
  "combos": [ { "hold": "knob", "then": "key1", "profile": "all", "action": "profile:1" } ]
}
```

Action texts are listed by `wildpad.py actions`; `key:alt+tab:hold` is type 11 and
`seq:…` a sequence (steps `tap ACTION`, `hold ACTION`, `wait MS`, `on release`,
separated by `;`). `rightAway` lists the buttons whose own action is sent right away;
when it is missing, modifier-only keys, gamepad buttons and type 11 are sent right
away and everything else on release.
