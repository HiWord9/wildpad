# Stock firmware investigation

## Device
- USB `1189:8890`, composite device with 4 HID interfaces: MI_00 keyboard, MI_01 vendor
  (usage page 0xFF00, used for configuration), MI_02 keyboard + consumer control,
  MI_03 mouse.
- Only stock Windows drivers (`kbdhid`, `mouhid`, `HidUsb`); there is no vendor driver.
- MCU: WCH CH552G. Pinout: keys P1.1, P1.7, P1.6; encoder A = P3.1, B = P3.0,
  encoder switch P3.3; 3x WS2812 LEDs on P3.4.

## The "MINI KeyBoard" app
A .NET app (internal name "HIDTester") built on HidLibrary. It sends 8-byte output
reports to MI_01: `[key number, (layer << 4) | type, count, index, modifier, code]`,
followed by `AA AA` to commit to flash. The protocol has no speed, debounce or polling
parameters.

## Measurements (Raw Input, QPC timestamps)
- Slow full turn: 20 events for 20 detents (the encoder has 20 detents per turn).
- Fast full turns: about 40–50 % of the detents arrive as events.
- Across 122 events the shortest gap between knob events was 47.3 ms, and gaps
  cluster on an 8 ms grid (47.5 / 55.8 / 64 ms). Each key press lasts one USB frame.
- Conclusion: the firmware emits at most ~21 knob events per second and samples the
  encoder slower than its phases change, so it drops steps.

## Existing open firmwares
| Project | Encoder handling | Result |
|---|---|---|
| nelsonjchen/c3-touchkey-keyboard | `if(!A) {read B; DLY 10ms; while(!A);}` plus 5 ms per loop; consecutive same-direction steps merge (no release in between) | drops steps when turned fast |
| eccherda/ch552g_mini_keyboard | state table, but polled from a loop with `delay(5)` and blocking USB sends | better, still drops steps |
| wagiminator CH552-USB-Knob | `A low -> read B -> delay` | same |

That's why this firmware decodes the encoder in a timer interrupt and queues the steps.
