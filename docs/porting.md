# Running Wildpad on another CH552 macropad

One Wildpad firmware runs every CH552 pad whose keys each have a pin of their own. The
board is a setting, not a build option:

- **Known boards** (3 keys + knob, 6 keys + knob, and the 6-key variant wired in
  reverse) are a table in [firmware/src/settings.c](../firmware/src/settings.c). Pick
  yours on the setup page under *Settings → Board*, or with `wildpad.py board 2`.
- **Any other pad**: on the setup page choose *Another pad: find its keys* and press
  the keys in order, then the knob. The page finds the pins and stores them as a pin
  map (10 bytes of the 128-byte settings). From the command line:
  `wildpad.py board custom P1.1 P1.7 P1.6 knob:P3.3 leds:3`.

What every board must share: the knob's A/B phases on P3.1/P3.0, WS2812 lights on
P3.4, up to 8 buttons (keys plus the knob switch) on P1 or P3, pressed = low. A pad
wired differently needs a change in `config.h` (`ENC_PORT_STATE()`, `PIN_NEO`).

The memory for actions follows the number of keys: every input takes 3 bytes in each
profile, so a 6-key pad fits 2 profiles where the 3-key pad fits 3. Unused profiles
leave their room to combos and multi-step actions.

If turning clockwise counts down, turn on *Reverse direction*; if only every second
click counts, turn on *Half-step knob* (both under *Settings → Knob*). If the wrong
key lights up when pressed, the light chain runs the other way: run the key finder
again and turn on the matching switch.

The first flash always needs the board's hardware bootloader entry: on CH552 that's
P3.6 (USB D+) pulled up through ~10 kΩ to 3.3 V while powering up, or whatever pad the
board routes to it (R12 on the 3-key board). Once Wildpad runs, holding key 1 while
plugging in, or `flash.bat`, enters the bootloader.

To add a board to the known ones, add its map to `BOARDS` in `settings.c` and to
`BOARDS` in `web/js/protocol.js` and `tools/wildpad.py`. Pull requests with new
boards are welcome; please include a photo of the PCB and the pin mapping.
