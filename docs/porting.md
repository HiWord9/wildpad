# Porting Wildpad to another CH552 macropad

Many cheap macropads use a CH552 with different key counts. Wildpad keeps everything
board specific in [firmware/src/config.h](../firmware/src/config.h).

1. **Find the pins.** Open the case and follow the traces from each switch, the
   encoder's A/B/switch pins and the LED data line to the CH552. Datasheet pinouts of
   the CH552G (SOP-16) and CH552T (TSSOP-20) are in the WCH datasheet.
2. **Edit `config.h`:**
   - `BOARD_ID`: pick a new number (the configurator falls back to a generic drawing).
   - `BOARD_BUTTONS`: number of buttons including the knob switch. The settings
     layout adapts: fewer profiles or combo slots fit when there are many buttons
     (all within 128 bytes), and the setup page reads the layout from the pad.
     Up to 8 buttons are supported (the button state is one byte).
   - `BOARD_KNOB_BUTTON`, `BOARD_BOOT_BUTTON`, `BOARD_RESCUE_BUTTON`: button indexes.
     The factory settings in `settings.c` assume three keys plus the knob; adjust
     `DEF_ACTIONS` / `DEF_COMBOS` for other layouts.
   - `PIN_*` and `BTN_READ()`: one bit per button, pressed = 1.
   - `ENC_PORT_STATE()`: must return `(A << 1) | B`. If A and B aren't P3.1/P3.0,
     build the value from two `PIN_read()` calls; it runs in the 4 kHz interrupt, so
     keep it short.
   - `NEO_COUNT`: number of WS2812 LEDs (LED *i* sits under button *i*; the knob
     position effect wants 3).
3. **Build and test:** `python firmware/build.py`, then flash. If turning clockwise
   counts down, turn on *Reverse direction*; if only every second click counts, turn
   on *Half-step knob* (both on the setup page under *Settings → Knob*).

The first flash always needs the board's hardware bootloader entry: on CH552 that's
P3.6 (USB D+) pulled up through ~10 kΩ to 3.3 V while powering up, or whatever pad the
board routes to it (R12 on the 3-key board). Once Wildpad runs, holding
`BOARD_BOOT_BUTTON` while plugging in, or `flash.bat`, enters the bootloader.

Pull requests with new boards are welcome; please include a photo of the PCB and the
pin mapping.
