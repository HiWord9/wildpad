# Configuration protocol (version 3)

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
| 7 | STATUS | – | bytes 0–4 like the live input report; bytes 8–16 the keyboard report being sent (report id, modifiers, reserved, 6 keys) |
| 8 | RECENTER | – | – (knob axis center = current position) |
| 9 | REBOOT | – | – (device reconnects) |
| 10 | BOOTLOADER | – | – (device reappears as the CH552 bootloader `4348:55E0`) |
| 11 | PROFILE | offset = profile (0-based) | – (switch the active profile) |
| 12 | INJECT | offset = buttons held (bit *i* = button *i*), length = knob clicks (int8) | – (acts as if the buttons were held and the knob turned; for automated tests) |

To change settings: READ everything, modify, WRITE all chunks, COMMIT, then SAVE. If
SAVE reports that the USB layout must change (an action now needs the gamepad, or no
longer does), send REBOOT; the device reconnects with the new layout.

INFO payload:

| Byte | Meaning |
|---|---|
| 0–2 | firmware version major, minor, patch |
| 3 | board id |
| 4 | buttons (keys + knob switch) |
| 5 | index of the knob switch |
| 6 | settings size (128) |
| 7 | number of profiles |
| 8 | USB layout: 0 keyboard, 1 keyboard + gamepad, 2 Xbox 360 controller |
| 9 | number of LEDs |
| 10 | number of combo slots |
| 11 | offset of the first profile |
| 12 | size of one profile |
| 13 | offset of the first combo |
| 14 | settings version (3) |
| 15 | active profile |

Older firmware: protocol 2 (Detent 1.0 previews) had a fixed 104-byte layout with
keyboard and wheel modes; protocol 1 used `READ = 1` and `BOOTLOADER = 8`. The flasher
recognises both by the protocol byte of the cmd 1 response.

## Settings (128 bytes)

Inputs are numbered: buttons `0 … B-1` (B = buttons from INFO), then *knob right* (B)
and *knob left* (B + 1). Offsets below are for the 3-key board (B = 4, 3 profiles,
12 combos); other boards use the offsets from INFO.

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | magic `0xA5` |
| 1 | 1 | settings version `3` |
| 2 | 1 | flags: 1 reverse knob, 2 half-step knob, 4 Xbox 360 identity |
| 3 | 1 | LED brightness (0–255) |
| 4 | 1 | hold time for long presses, 10 ms units (10–100) |
| 5 | 1 | debounce in ms (1–30) |
| 6 | 1 | tap length in ms (1–50) |
| 7 | 1 | knob clicks per turn (only for drawing) |
| 8 | 1 | profile after power-up (0-based) |
| 9 | 1 | light animation speed (1–10) |
| 10 | 1 | switcher hold after turning: a knob turn with a *key, modifiers held* action keeps them held this long after the last click, 100 ms units (2–30; 0 = 8) |
| 11 | 1 | reserved |
| 12 | 22 × 3 | profiles: red, green, blue, light effect, then one action per input (bit 0x80 of a button's type: right away, see Combos) |
| 78 | 4 × 12 | combos |
| 126 | 1 | reserved |
| 127 | 1 | checksum: sum of bytes 0–126, modulo 256 |

Light effects: 0 off, 1 solid, 2 breathe, 3 rainbow, 4 light on press, 5 knob position
(gamepad axis; solid otherwise).

### Combos (4 bytes)

`[held << 4 | trigger, profile << 5 | action type, p1, p2]`. While *held* (a button) is
pressed, *trigger* (any input) does the action instead of its own. `held == trigger`
is a long press of that button. Profile 7 means all profiles. An unused slot starts
with `0xFF`.

A button that starts combos or has a long press sends its own action when it is
released (as a tap), unless a combo was used while it was held. If bit `0x80` of the
button's action type in the profile is set ("right away"), the action is pressed at
once and held until release instead, and combos work on top of it; a long press is
then ignored. Example: knob = Alt right away, knob + right = Tab, knob + left =
Shift+Tab gives Alt+Tab window switching.

A combo of the active profile wins over an all-profiles combo with the same held
input and trigger.

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
| 11 | key, modifiers held | like type 1, but Ctrl / Alt / Win (not Shift) stay held: on a button while it is held (the key and Shift are tapped once), on a knob turn until the knob has stopped for the switcher hold time (the key and Shift are tapped on every click). For Alt+Tab-style switchers. |

Keys, media keys, mouse buttons and gamepad buttons stay pressed while their input is
held; knob clicks press and release them once per click. The gamepad interface is
present whenever any profile or combo uses type 6 or 7.

## File format

`detent.py export` and the setup page's *Save settings to a file* write the same JSON:

```json
{
  "format": "detent-settings", "version": 3,
  "options": { "brightness": 128, "holdMs": 300, "debounceMs": 5, "tapMs": 4,
               "clicksPerTurn": 20, "startProfile": 1, "lightSpeed": 5, "turnHoldMs": 800,
               "reverseKnob": false, "halfStep": false, "xbox360": false },
  "profiles": [ { "color": "#00aaff", "light": "solid",
                  "actions": { "key1": "media:previous-track", "knob": "media:mute",
                               "right": "media:volume-up", "left": "media:volume-down" },
                  "rightAway": [] } ],
  "combos": [ { "hold": "knob", "then": "key1", "profile": "all", "action": "profile:1" } ]
}
```

Action texts are listed by `detent.py actions`; `key:alt+tab:hold` is type 11. `rightAway` lists the buttons whose
own action is sent right away; when it is missing, modifier-only keys and gamepad
buttons are sent right away and everything else on release.
