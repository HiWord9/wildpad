#!/usr/bin/env python3
"""TISHRIC Knob - command line tool for the open firmware.

  python tishric.py info                 show firmware version, mode and settings
  python tishric.py watch                live knob position / buttons (Ctrl+C to stop)
  python tishric.py set key1 ctrl+c      set an action (key1 key2 key3 knob cw ccw)
  python tishric.py set cw wheel:1       ... see `python tishric.py keys` for names
  python tishric.py pad key1 RT          gamepad button for key1/key2/key3/knob
  python tishric.py option range 10      range | brightness | tap | invert | halfstep | clamp
  python tishric.py mode gamepad         switch mode (saved, device re-connects)
  python tishric.py defaults             restore factory defaults (saved)
  python tishric.py bootloader           jump to the USB bootloader (for flashing)

Requires:  pip install hidapi
"""
import struct
import sys
import time

VID, PID = 0x1189, 0x8890
USAGE_PAGE = 0xFF54
FEATURE_LEN = 40
SETTINGS_SIZE = 30

CMD_READ, CMD_WRITE, CMD_SAVE, CMD_DEFAULTS, CMD_STATUS, CMD_RECENTER, CMD_REBOOT, CMD_BOOTLOADER = range(1, 9)

INPUTS = ["key1", "key2", "key3", "knob", "cw", "ccw"]
PAD_INPUTS = INPUTS[:4]
PAD_NAMES = ["none", "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Back", "Start", "LS", "RS",
             "Up", "Down", "Left", "Right", "Guide", "Recenter"]
ACT_NONE, ACT_KEY, ACT_CONSUMER, ACT_MOUSE_BTN, ACT_WHEEL, ACT_HWHEEL = range(6)
FLAG_INVERT, FLAG_HALFSTEP, FLAG_CLAMP = 1, 2, 4

MODIFIERS = {"ctrl": 0x01, "shift": 0x02, "alt": 0x04, "win": 0x08, "gui": 0x08,
             "rctrl": 0x10, "rshift": 0x20, "ralt": 0x40, "altgr": 0x40, "rwin": 0x80}


def _keycodes():
    k = {}
    for i, c in enumerate("abcdefghijklmnopqrstuvwxyz"):
        k[c] = 0x04 + i
    for i, c in enumerate("1234567890"):
        k[c] = 0x1E + i
    k.update({"enter": 0x28, "esc": 0x29, "escape": 0x29, "backspace": 0x2A, "tab": 0x2B,
              "space": 0x2C, "minus": 0x2D, "equal": 0x2E, "lbracket": 0x2F, "rbracket": 0x30,
              "backslash": 0x31, "semicolon": 0x33, "quote": 0x34, "grave": 0x35, "comma": 0x36,
              "period": 0x37, "slash": 0x38, "capslock": 0x39, "printscreen": 0x46,
              "scrolllock": 0x47, "pause": 0x48, "insert": 0x49, "home": 0x4A, "pageup": 0x4B,
              "delete": 0x4C, "end": 0x4D, "pagedown": 0x4E, "right": 0x4F, "left": 0x50,
              "down": 0x51, "up": 0x52, "numlock": 0x53, "kp/": 0x54, "kp*": 0x55, "kp-": 0x56,
              "kp+": 0x57, "kpenter": 0x58, "menu": 0x65})
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


def parse_action(text):
    """'ctrl+shift+a' | 'volup' | 'lclick' | 'wheel:1' | 'hwheel:-1' | 'none' -> (type, p1, p2)"""
    t = text.strip().lower()
    if t in ("", "none", "off"):
        return (ACT_NONE, 0, 0)
    if t.startswith("wheel:") or t.startswith("hwheel:"):
        kind, val = t.split(":")
        v = int(val) & 0xFF
        return (ACT_WHEEL if kind == "wheel" else ACT_HWHEEL, v, 0)
    if t in CONSUMER:
        u = CONSUMER[t]
        return (ACT_CONSUMER, u & 0xFF, u >> 8)
    if t.startswith("consumer:"):
        u = int(t.split(":")[1], 0)
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
            raise ValueError(f"unknown key '{part}' (see: python tishric.py keys)")
    return (ACT_KEY, mods, code)


def format_action(a):
    typ, p1, p2 = a
    if typ == ACT_NONE:
        return "none"
    if typ == ACT_KEY:
        parts = [n for n, b in MODIFIERS.items() if p1 & b and n not in ("gui", "altgr")]
        name = next((n for n, c in KEYCODES.items() if c == p2), f"0x{p2:02x}") if p2 else None
        return "+".join(parts + ([name] if name else []))
    if typ == ACT_CONSUMER:
        u = p1 | p2 << 8
        return next((n for n, c in CONSUMER.items() if c == u), f"consumer:0x{u:x}")
    if typ == ACT_MOUSE_BTN:
        return next((n for n, c in MOUSE.items() if c == p1), f"mouse:{p1}")
    v = p1 - 256 if p1 > 127 else p1
    return f"{'wheel' if typ == ACT_WHEEL else 'hwheel'}:{v}"


class Settings:
    def __init__(self, raw: bytes):
        assert len(raw) >= SETTINGS_SIZE
        self.raw = bytearray(raw[:SETTINGS_SIZE])

    mode = property(lambda s: s.raw[2], lambda s, v: s.raw.__setitem__(2, v))
    flags = property(lambda s: s.raw[3], lambda s, v: s.raw.__setitem__(3, v))
    steer_range = property(lambda s: s.raw[4], lambda s, v: s.raw.__setitem__(4, v))
    brightness = property(lambda s: s.raw[5], lambda s, v: s.raw.__setitem__(5, v))
    tap_ms = property(lambda s: s.raw[6], lambda s, v: s.raw.__setitem__(6, v))

    def pad(self, i):
        return self.raw[7 + i]

    def set_pad(self, i, v):
        self.raw[7 + i] = v

    def action(self, i):
        o = 11 + 3 * i
        return tuple(self.raw[o:o + 3])

    def set_action(self, i, a):
        o = 11 + 3 * i
        self.raw[o:o + 3] = bytes(a)

    def finalize(self):
        self.raw[29] = sum(self.raw[:29]) & 0xFF
        return bytes(self.raw)

    def describe(self):
        lines = [f"mode        : {'gamepad (Xbox)' if self.mode else 'keyboard'}",
                 f"steer range : {self.steer_range} detents to full lock",
                 f"brightness  : {self.brightness}",
                 f"tap length  : {self.tap_ms} ms",
                 f"invert      : {bool(self.flags & FLAG_INVERT)}   halfstep: {bool(self.flags & FLAG_HALFSTEP)}"
                 f"   clamp: {bool(self.flags & FLAG_CLAMP)}",
                 "keyboard mode actions:"]
        for i, n in enumerate(INPUTS):
            lines.append(f"  {n:5s} -> {format_action(self.action(i))}")
        lines.append("gamepad mode buttons:")
        for i, n in enumerate(PAD_INPUTS):
            p = self.pad(i)
            lines.append(f"  {n:5s} -> {PAD_NAMES[p] if p < len(PAD_NAMES) else p}")
        return "\n".join(lines)


class Knob:
    def __init__(self):
        import hid
        self.hid = hid
        path = None
        for d in hid.enumerate(VID, PID):
            if d.get("usage_page") == USAGE_PAGE:
                path = d["path"]
                break
        if path is None:
            raise RuntimeError("TISHRIC Knob (open firmware, keyboard mode) not found")
        self.dev = hid.device()
        self.dev.open_path(path)
        self.seq = 0

    def close(self):
        self.dev.close()

    def command(self, cmd, payload=b"", wait=True):
        self.seq = self.seq % 250 + 1
        req = bytes([cmd, self.seq]) + payload
        req = req + bytes(FEATURE_LEN - len(req))
        self.dev.send_feature_report(b"\x00" + req)
        if not wait:
            return None
        for _ in range(50):
            time.sleep(0.005)
            r = bytes(self.dev.get_feature_report(0, FEATURE_LEN + 1))
            if r and r[0] == 0 and len(r) == FEATURE_LEN + 1:      # Windows prepends report ID
                r = r[1:]
            if len(r) >= 4 and r[0] == cmd and r[1] == self.seq:
                if r[2] != 0:
                    raise RuntimeError(f"device rejected command {cmd} (status {r[2]})")
                return r
        raise RuntimeError("no response from device")

    def read_settings(self):
        r = self.command(CMD_READ)
        return Settings(r[4:4 + SETTINGS_SIZE]), r[3]

    def write_settings(self, s: Settings, save=True):
        self.command(CMD_WRITE, s.finalize())
        if save:
            self.command(CMD_SAVE)

    def status(self):
        r = self.command(CMD_STATUS)
        pos, buttons, mode = struct.unpack_from("<hBB", r, 4)
        return pos, buttons, mode

    def read_live(self, timeout_ms=100):
        d = self.dev.read(STATUS_LEN + 1, timeout_ms)
        if not d:
            return None
        d = bytes(d)
        if len(d) == STATUS_LEN + 1:
            d = d[1:]
        pos, buttons, mode = struct.unpack_from("<hBB", d, 0)
        return pos, buttons, mode


STATUS_LEN = 8


def main(argv):
    if len(argv) < 2 or argv[1] in ("-h", "--help", "help"):
        print(__doc__)
        return
    cmd = argv[1]
    if cmd == "keys":
        print("keys     :", " ".join(sorted(KEYCODES)))
        print("modifiers:", " ".join(MODIFIERS), "(combine with +, e.g. ctrl+shift+s)")
        print("media    :", " ".join(CONSUMER))
        print("mouse    :", " ".join(MOUSE), " wheel:N  hwheel:N")
        print("pad      :", " ".join(PAD_NAMES))
        return
    k = Knob()
    try:
        if cmd == "info":
            s, fw = k.read_settings()
            print(f"firmware v{fw}")
            print(s.describe())
        elif cmd == "watch":
            print("turn the knob / press keys, Ctrl+C to stop")
            last = None
            while True:
                st = k.read_live(200)
                if st and st != last:
                    pos, b, _ = st
                    keys = " ".join(n for i, n in enumerate(PAD_INPUTS) if b & (1 << i))
                    print(f"position {pos:+5d}   pressed: {keys}")
                    last = st
        elif cmd == "set":
            s, _ = k.read_settings()
            s.set_action(INPUTS.index(argv[2].lower()), parse_action(argv[3]))
            k.write_settings(s)
            print(s.describe())
        elif cmd == "pad":
            s, _ = k.read_settings()
            names = [n.lower() for n in PAD_NAMES]
            s.set_pad(PAD_INPUTS.index(argv[2].lower()), names.index(argv[3].lower()))
            k.write_settings(s)
            print(s.describe())
        elif cmd == "option":
            s, _ = k.read_settings()
            name, val = argv[2].lower(), argv[3].lower()
            flag = {"invert": FLAG_INVERT, "halfstep": FLAG_HALFSTEP, "clamp": FLAG_CLAMP}.get(name)
            if flag:
                on = val in ("1", "on", "yes", "true")
                s.flags = (s.flags | flag) if on else (s.flags & ~flag)
            elif name == "range":
                s.steer_range = max(1, min(100, int(val)))
            elif name == "brightness":
                s.brightness = max(0, min(255, int(val)))
            elif name == "tap":
                s.tap_ms = max(1, min(50, int(val)))
            else:
                raise SystemExit(f"unknown option {name}")
            k.write_settings(s)
            print(s.describe())
        elif cmd == "mode":
            s, _ = k.read_settings()
            s.mode = 1 if argv[2].lower() in ("gamepad", "pad", "xbox", "1") else 0
            k.write_settings(s)
            k.command(CMD_REBOOT, wait=False)
            print("saved, device is re-connecting in", "gamepad" if s.mode else "keyboard", "mode")
        elif cmd == "defaults":
            k.command(CMD_DEFAULTS)
            k.command(CMD_SAVE)
            print("defaults restored")
        elif cmd == "recenter":
            k.command(CMD_RECENTER)
        elif cmd == "bootloader":
            k.command(CMD_BOOTLOADER, wait=False)
            print("device is switching to the bootloader")
        else:
            print(__doc__)
    finally:
        k.close()


if __name__ == "__main__":
    try:
        main(sys.argv)
    except KeyboardInterrupt:
        pass
