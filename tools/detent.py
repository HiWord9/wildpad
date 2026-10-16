#!/usr/bin/env python3
"""Detent - command line tool for the Detent macropad firmware.

  python detent.py info                      firmware, board, mode and all settings
  python detent.py watch                     live knob position / keys (Ctrl+C stops)
  python detent.py set key1 ctrl+c           keyboard action (base layer)
  python detent.py set cw wheel:1 --fn       ... in the Fn layer
  python detent.py set key2 fn --hold        ... when the key is held
  python detent.py set key1 pad:rt --wheel   ... in wheel mode
  python detent.py option range 10           see `options` below
  python detent.py mode wheel                switch mode (saved, device reconnects)
  python detent.py export my.json            save all settings to a file
  python detent.py import my.json            load settings from a file (saved)
  python detent.py defaults                  factory defaults (saved)
  python detent.py bootloader                jump to the USB bootloader
  python detent.py actions                   list action names

Inputs: key1 key2 key3 knob (press) cw ccw (turns).
Options: range brightness tap debounce hold detents led color axis
         invert halfstep clamp legacy gesture

Requires:  pip install hidapi
"""
import json
import struct
import sys
import time

VID, PID = 0x1189, 0x8890
USAGE_PAGE = 0xFF54
FEATURE_LEN = 40
CHUNK = 32
PROTOCOL = 2

CMD_INFO, CMD_READ, CMD_WRITE, CMD_COMMIT, CMD_SAVE, CMD_DEFAULTS, CMD_STATUS, \
    CMD_RECENTER, CMD_REBOOT, CMD_BOOTLOADER = range(1, 11)
STATUS_TEXT = {1: "unknown command", 2: "invalid settings", 3: "bad offset/length"}

# ----------------------------------------------------------------- settings layout
SETTINGS_SIZE = 104
MAX_BUTTONS = 6
SLOT_CW, SLOT_CCW = MAX_BUTTONS, MAX_BUTTONS + 1
OFF_BASE, OFF_FN, OFF_HOLD, OFF_WHEEL = 16, 40, 64, 82

FLAGS = {"invert": 0x01, "halfstep": 0x02, "clamp": 0x04, "legacy": 0x08, "nogesture": 0x10}
LED_MODES = ["off", "reactive", "static", "rainbow", "breathe"]
AXES = ["lx", "ly", "rx", "ry", "triggers"]

ACT_NONE, ACT_KEY, ACT_CONSUMER, ACT_MOUSE_BTN, ACT_WHEEL, ACT_HWHEEL, ACT_PAD, \
    ACT_RECENTER, ACT_FN, ACT_MODE = range(10)
PAD_NAMES = ["", "a", "b", "x", "y", "lb", "rb", "lt", "rt", "back", "start", "ls", "rs",
             "up", "down", "left", "right", "guide"]

MODIFIERS = {"ctrl": 0x01, "shift": 0x02, "alt": 0x04, "win": 0x08,
             "rctrl": 0x10, "rshift": 0x20, "ralt": 0x40, "rwin": 0x80}


def _keycodes():
    k = {}
    for i, c in enumerate("abcdefghijklmnopqrstuvwxyz"):
        k[c] = 0x04 + i
    for i, c in enumerate("1234567890"):
        k[c] = 0x1E + i
    k.update({"enter": 0x28, "esc": 0x29, "backspace": 0x2A, "tab": 0x2B, "space": 0x2C,
              "minus": 0x2D, "equal": 0x2E, "lbracket": 0x2F, "rbracket": 0x30, "backslash": 0x31,
              "semicolon": 0x33, "quote": 0x34, "grave": 0x35, "comma": 0x36, "period": 0x37,
              "slash": 0x38, "capslock": 0x39, "printscreen": 0x46, "scrolllock": 0x47,
              "pause": 0x48, "insert": 0x49, "home": 0x4A, "pageup": 0x4B, "delete": 0x4C,
              "end": 0x4D, "pagedown": 0x4E, "right": 0x4F, "left": 0x50, "down": 0x51,
              "up": 0x52, "numlock": 0x53, "menu": 0x65})
    for i in range(1, 13):
        k[f"f{i}"] = 0x39 + i
    for i in range(13, 25):
        k[f"f{i}"] = 0x68 + i - 13
    for i in range(1, 10):
        k[f"kp{i}"] = 0x58 + i
    k["kp0"] = 0x62
    return k


KEYCODES = _keycodes()
CONSUMER = {"volup": 0xE9, "voldown": 0xEA, "mute": 0xE2, "play": 0xCD, "next": 0xB5,
            "prev": 0xB6, "stop": 0xB7, "calc": 0x192, "browser": 0x196, "mail": 0x18A,
            "brightup": 0x6F, "brightdown": 0x70}
MOUSE = {"lclick": 1, "rclick": 2, "mclick": 4, "back": 8, "forward": 16}
INPUTS = ["key1", "key2", "key3", "knob", "key5", "key6", "cw", "ccw"]
BOARD_INPUTS = ["key1", "key2", "key3", "knob", "cw", "ccw"]


def parse_action(text):
    """'ctrl+shift+a' | 'volup' | 'lclick' | 'wheel:1' | 'hwheel:-1' | 'pad:a' | 'fn' |
    'recenter' | 'mode' | 'none' -> (type, p1, p2)"""
    t = text.strip().lower()
    if t in ("", "none", "off"):
        return (ACT_NONE, 0, 0)
    if t == "fn":
        return (ACT_FN, 0, 0)
    if t == "recenter":
        return (ACT_RECENTER, 0, 0)
    if t == "mode":
        return (ACT_MODE, 0, 0)
    if t.startswith("pad:"):
        return (ACT_PAD, PAD_NAMES.index(t[4:]), 0)
    if t.startswith(("wheel:", "hwheel:")):
        kind, val = t.split(":")
        return (ACT_WHEEL if kind == "wheel" else ACT_HWHEEL, int(val) & 0xFF, 0)
    if t in CONSUMER or t.startswith("consumer:"):
        u = CONSUMER[t] if t in CONSUMER else int(t.split(":")[1], 0)
        return (ACT_CONSUMER, u & 0xFF, u >> 8)
    if t in MOUSE:
        return (ACT_MOUSE_BTN, MOUSE[t], 0)
    mods, code = 0, 0
    for part in t.split("+"):
        if part in MODIFIERS:
            mods |= MODIFIERS[part]
        elif part in KEYCODES:
            code = KEYCODES[part]
        elif part.startswith("0x"):
            code = int(part, 16)
        else:
            raise ValueError(f"unknown key '{part}' (see: python detent.py actions)")
    return (ACT_KEY, mods, code)


def format_action(a):
    typ, p1, p2 = a
    if typ == ACT_NONE:
        return "none"
    if typ == ACT_KEY:
        parts = [n for n, b in MODIFIERS.items() if p1 & b]
        if p2:
            parts.append(next((n for n, c in KEYCODES.items() if c == p2), f"0x{p2:02x}"))
        return "+".join(parts)
    if typ == ACT_CONSUMER:
        u = p1 | p2 << 8
        return next((n for n, c in CONSUMER.items() if c == u), f"consumer:0x{u:x}")
    if typ == ACT_MOUSE_BTN:
        return next((n for n, c in MOUSE.items() if c == p1), f"mouse:{p1}")
    if typ in (ACT_WHEEL, ACT_HWHEEL):
        return f"{'wheel' if typ == ACT_WHEEL else 'hwheel'}:{p1 - 256 if p1 > 127 else p1}"
    if typ == ACT_PAD:
        return f"pad:{PAD_NAMES[p1] if p1 < len(PAD_NAMES) else p1}"
    return {ACT_RECENTER: "recenter", ACT_FN: "fn", ACT_MODE: "mode"}.get(typ, f"?{typ}")


class Settings:
    """Byte-exact mirror of settings_t in firmware/src/settings.h."""

    SCALARS = {"mode": 2, "flags": 3, "steer_range": 4, "brightness": 5, "tap_ms": 6,
               "debounce_ms": 7, "hold_10ms": 8, "detents_per_rev": 9, "led_mode": 10,
               "led_hue": 11, "axis": 12}

    def __init__(self, raw):
        self.raw = bytearray(raw[:SETTINGS_SIZE])

    def __getattr__(self, name):
        if name in Settings.SCALARS:
            return self.raw[Settings.SCALARS[name]]
        raise AttributeError(name)

    def __setattr__(self, name, value):
        if name in Settings.SCALARS:
            self.raw[Settings.SCALARS[name]] = value & 0xFF
        else:
            object.__setattr__(self, name, value)

    @staticmethod
    def offset(table, slot):
        return {"base": OFF_BASE, "fn": OFF_FN, "hold": OFF_HOLD, "wheel": OFF_WHEEL}[table] + 3 * slot

    def action(self, table, slot):
        o = self.offset(table, slot)
        return tuple(self.raw[o:o + 3])

    def set_action(self, table, slot, a):
        o = self.offset(table, slot)
        self.raw[o:o + 3] = bytes(a)

    def finalize(self):
        self.raw[SETTINGS_SIZE - 1] = sum(self.raw[:SETTINGS_SIZE - 1]) & 0xFF
        return bytes(self.raw)

    # ---- JSON (human readable, stable across firmware versions)
    def to_json(self, n_buttons=4):
        slots = list(range(n_buttons))
        name = lambda s: INPUTS[s] if s < MAX_BUTTONS else ("cw" if s == SLOT_CW else "ccw")
        doc = {"format": "detent-settings", "version": 2,
               "mode": "wheel" if self.mode else "keyboard",
               "flags": [k for k, b in FLAGS.items() if self.flags & b],
               "steer_range": self.steer_range, "brightness": self.brightness,
               "tap_ms": self.tap_ms, "debounce_ms": self.debounce_ms,
               "hold_ms": self.hold_10ms * 10, "detents_per_rev": self.detents_per_rev,
               "led_mode": LED_MODES[self.led_mode], "led_hue": self.led_hue,
               "axis": AXES[self.axis]}
        for table in ("base", "fn"):
            doc[table] = {name(s): format_action(self.action(table, s)) for s in slots + [SLOT_CW, SLOT_CCW]}
        for table in ("hold", "wheel"):
            doc[table] = {name(s): format_action(self.action(table, s)) for s in slots}
        return doc

    def from_json(self, doc):
        if doc.get("format") != "detent-settings":
            raise ValueError("not a Detent settings file")
        self.mode = 1 if doc["mode"] == "wheel" else 0
        self.flags = sum(FLAGS[f] for f in doc.get("flags", []))
        for key in ("steer_range", "brightness", "tap_ms", "debounce_ms", "detents_per_rev", "led_hue"):
            setattr(self, key, int(doc[key]))
        self.hold_10ms = int(doc["hold_ms"]) // 10
        self.led_mode = LED_MODES.index(doc["led_mode"])
        self.axis = AXES.index(doc["axis"])
        for table in ("base", "fn", "hold", "wheel"):
            for name, text in doc.get(table, {}).items():
                slot = SLOT_CW if name == "cw" else SLOT_CCW if name == "ccw" else INPUTS.index(name)
                self.set_action(table, slot, parse_action(text))


class Device:
    def __init__(self):
        import hid
        path = next((d["path"] for d in hid.enumerate(VID, PID) if d.get("usage_page") == USAGE_PAGE), None)
        if path is None:
            raise RuntimeError("Detent macropad not found (is the open firmware installed? "
                               "In legacy wheel mode it can't be configured)")
        self.dev = hid.device()
        self.dev.open_path(path)
        self.seq = 0
        self.info = None

    def close(self):
        self.dev.close()

    def command(self, cmd, payload=b"", wait=True):
        self.seq = self.seq % 250 + 1
        req = bytes([cmd, self.seq]) + bytes(payload)
        self.dev.send_feature_report(b"\x00" + req + bytes(FEATURE_LEN - len(req)))
        if not wait:
            return None
        for _ in range(60):
            time.sleep(0.004)
            r = bytes(self.dev.get_feature_report(0, FEATURE_LEN + 1))
            if len(r) == FEATURE_LEN + 1:
                r = r[1:]                                  # Windows prepends the report ID
            if len(r) >= 4 and r[0] == cmd and r[1] == self.seq:
                if r[2]:
                    raise RuntimeError(f"device: {STATUS_TEXT.get(r[2], r[2])}")
                if r[3] != PROTOCOL:
                    raise RuntimeError(f"device speaks protocol {r[3]}, this tool needs {PROTOCOL}: "
                                       "update the firmware with flash.bat")
                return r[4:]
        raise RuntimeError("no answer from the device")

    def get_info(self):
        r = self.command(CMD_INFO)
        self.info = {"firmware": f"{r[0]}.{r[1]}.{r[2]}", "board": r[3], "buttons": r[4],
                     "knob_button": r[5], "settings_size": r[6], "mode": r[7],
                     "usb": r[8], "leds": r[9]}
        return self.info

    def read_settings(self):
        data = b""
        for off in range(0, SETTINGS_SIZE, CHUNK):
            n = min(CHUNK, SETTINGS_SIZE - off)
            data += self.command(CMD_READ, bytes([off, n]))[:n]
        return Settings(data)

    def write_settings(self, s, save=True):
        raw = s.finalize()
        for off in range(0, SETTINGS_SIZE, CHUNK):
            chunk = raw[off:off + CHUNK]
            self.command(CMD_WRITE, bytes([off, len(chunk)]) + chunk)
        self.command(CMD_COMMIT)
        if save:
            self.command(CMD_SAVE)

    def status(self):
        r = self.command(CMD_STATUS)
        pos, buttons, mode, fn = struct.unpack_from("<hBBB", r, 0)
        return pos, buttons, mode, fn

    def read_live(self, timeout_ms=200):
        d = self.dev.read(9, timeout_ms)
        if not d:
            return None
        d = bytes(d)
        if len(d) == 9:
            d = d[1:]
        return struct.unpack_from("<hBBB", d, 0)


def describe(s, info):
    nb = info["buttons"] if info else 4
    names = BOARD_INPUTS
    slots = list(range(nb)) + [SLOT_CW, SLOT_CCW]
    label = lambda i, s_: names[i] if i < len(names) else str(s_)
    lines = [f"mode         : {'wheel' if s.mode else 'keyboard'}",
             f"flags        : {', '.join(k for k, b in FLAGS.items() if s.flags & b) or '-'}",
             f"steer range  : {s.steer_range} detents to full lock, axis {AXES[s.axis]}",
             f"timings      : tap {s.tap_ms} ms, debounce {s.debounce_ms} ms, hold {s.hold_10ms * 10} ms",
             f"LEDs         : {LED_MODES[s.led_mode]}, hue {s.led_hue}, brightness {s.brightness}",
             f"{'':12s}   {'base':16s} {'fn layer':16s} {'held':16s} {'wheel mode':16s}"]
    for i, slot in enumerate(slots):
        held = format_action(s.action("hold", slot)) if slot < MAX_BUTTONS else ""
        wheel = format_action(s.action("wheel", slot)) if slot < MAX_BUTTONS else "(steering)"
        lines.append(f"{label(i, slot):12s}   {format_action(s.action('base', slot)):16s} "
                     f"{format_action(s.action('fn', slot)):16s} {held:16s} {wheel:16s}")
    return "\n".join(lines)


def slot_of(name):
    name = name.lower()
    if name == "cw":
        return SLOT_CW
    if name == "ccw":
        return SLOT_CCW
    return BOARD_INPUTS.index(name) if name in BOARD_INPUTS[:4] else INPUTS.index(name)


def main(argv):
    if len(argv) < 2 or argv[1] in ("-h", "--help", "help"):
        print(__doc__)
        return
    cmd, args = argv[1], argv[2:]
    if cmd == "actions":
        print("keys     :", " ".join(sorted(KEYCODES)))
        print("modifiers:", " ".join(MODIFIERS), "(combine with +, e.g. ctrl+shift+s)")
        print("media    :", " ".join(CONSUMER))
        print("mouse    :", " ".join(MOUSE), "  wheel:N  hwheel:N")
        print("gamepad  :", " ".join("pad:" + p for p in PAD_NAMES[1:]), "  recenter")
        print("special  : fn (layer while held)  mode (switch keyboard/wheel)  none")
        return
    d = Device()
    try:
        info = d.get_info()
        if cmd == "info":
            print(f"firmware {info['firmware']}, board {info['board']}, {info['buttons']} buttons, "
                  f"running {'wheel' if info['mode'] else 'keyboard'} mode")
            print(describe(d.read_settings(), info))
        elif cmd == "watch":
            print("turn the knob / press keys, Ctrl+C to stop")
            last = None
            while True:
                st = d.read_live()
                if st and st != last:
                    pos, b, _, fn = st
                    keys = " ".join(BOARD_INPUTS[i] for i in range(info["buttons"]) if b & (1 << i))
                    print(f"position {pos:+5d}   pressed: {keys:20s} {'[Fn]' if fn else ''}")
                    last = st
        elif cmd == "set":
            table = "fn" if "--fn" in args else "hold" if "--hold" in args else "wheel" if "--wheel" in args else "base"
            pos_args = [a for a in args if not a.startswith("--")]
            s = d.read_settings()
            s.set_action(table, slot_of(pos_args[0]), parse_action(pos_args[1]))
            d.write_settings(s)
            print(describe(s, info))
        elif cmd == "option":
            s = d.read_settings()
            name, val = args[0].lower(), args[1].lower()
            on = val in ("1", "on", "yes", "true")
            if name in FLAGS:
                s.flags = (s.flags | FLAGS[name]) if on else (s.flags & ~FLAGS[name])
            elif name == "gesture":
                s.flags = (s.flags & ~FLAGS["nogesture"]) if on else (s.flags | FLAGS["nogesture"])
            elif name == "range":
                s.steer_range = max(1, min(100, int(val)))
            elif name == "brightness":
                s.brightness = max(0, min(255, int(val)))
            elif name == "tap":
                s.tap_ms = max(1, min(50, int(val)))
            elif name == "debounce":
                s.debounce_ms = max(1, min(30, int(val)))
            elif name == "hold":
                s.hold_10ms = max(10, min(100, int(val) // 10))
            elif name == "detents":
                s.detents_per_rev = max(8, min(100, int(val)))
            elif name == "led":
                s.led_mode = LED_MODES.index(val)
            elif name == "color":
                s.led_hue = max(0, min(255, int(val)))
            elif name == "axis":
                s.axis = AXES.index(val)
            else:
                raise SystemExit(f"unknown option {name}")
            d.write_settings(s)
            print(describe(s, info))
        elif cmd == "mode":
            s = d.read_settings()
            s.mode = 1 if args[0].lower() in ("wheel", "gamepad", "pad", "1") else 0
            d.write_settings(s)
            d.command(CMD_REBOOT, wait=False)
            print("saved, the macropad reconnects in", "wheel" if s.mode else "keyboard", "mode")
        elif cmd == "export":
            with open(args[0], "w", encoding="utf-8") as f:
                json.dump(d.read_settings().to_json(info["buttons"]), f, indent=2)
            print("saved to", args[0])
        elif cmd == "import":
            s = d.read_settings()
            with open(args[0], encoding="utf-8") as f:
                s.from_json(json.load(f))
            d.write_settings(s)
            print(describe(s, info))
        elif cmd == "defaults":
            d.command(CMD_DEFAULTS)
            d.command(CMD_SAVE)
            print("defaults restored")
        elif cmd == "recenter":
            d.command(CMD_RECENTER)
        elif cmd == "bootloader":
            d.command(CMD_BOOTLOADER, wait=False)
            print("switching to the bootloader")
        else:
            print(__doc__)
    finally:
        d.close()


if __name__ == "__main__":
    try:
        main(sys.argv)
    except KeyboardInterrupt:
        pass
    except (RuntimeError, ValueError) as e:
        sys.exit(f"error: {e}")
