#!/usr/bin/env python3
"""Detent - command line tool for macropads running the Detent firmware.

  python detent.py info                         firmware, board and USB layout
  python detent.py show                         every profile, combo and option
  python detent.py watch                        live knob / keys / profile (Ctrl+C stops)
  python detent.py set 1 key1 key:ctrl+c        profile 1, key 1 -> Ctrl+C
  python detent.py set 2 right axis:lx:10       profile 2, knob -> gamepad stick X
  python detent.py combo add knob key1 profile:1           hold knob + key 1
  python detent.py combo add key3 right scroll:-1 --profile 2
  python detent.py combo add key1 key1 key:esc  hold key 1 alone (long press)
  python detent.py combo del 3                  remove combo number 3
  python detent.py light 1 breathe #ff8800      profile 1 light effect and color
  python detent.py option brightness 200        see OPTIONS below
  python detent.py profile 2                    switch to profile 2 now
  python detent.py export my.json               save everything to a file
  python detent.py import my.json               load a file (saved on the device)
  python detent.py defaults                     factory settings (saved)
  python detent.py bootloader                   jump to the USB bootloader
  python detent.py actions                      list action and key names

Inputs: key1 key2 key3 ... knob (press), right / left (knob turns).
Changes are saved on the device right away. The device reconnects by itself when
the gamepad has to be added or removed.

Requires:  pip install hidapi
"""
import json
import re
import sys
import time

VID, PID = 0x1189, 0x8890
USAGE_PAGE = 0xFF54
FEATURE_LEN = 40
CHUNK = 32
PROTOCOL = 3
SETTINGS_VERSION = 3

(CMD_INFO, CMD_READ, CMD_WRITE, CMD_COMMIT, CMD_SAVE, CMD_DEFAULTS, CMD_STATUS,
 CMD_RECENTER, CMD_REBOOT, CMD_BOOTLOADER, CMD_PROFILE) = range(1, 12)
STATUS_TEXT = {1: "unknown command", 2: "invalid settings", 3: "bad offset/length"}
USB_LAYOUTS = ["keyboard", "keyboard + gamepad", "Xbox 360 controller"]

# ----------------------------------------------------------------- settings layout
S_MAGIC, S_VERSION, S_FLAGS, S_BRIGHTNESS, S_HOLD, S_DEBOUNCE, S_TAP, S_DETENTS, \
    S_START, S_SPEED = range(10)
MAGIC = 0xA5
FLAG_INVERT, FLAG_HALFSTEP, FLAG_XBOX360 = 0x01, 0x02, 0x04
COMBO_EMPTY, ALL_PROFILES = 0xFF, 7
EFFECTS = ["off", "solid", "breathe", "rainbow", "press", "steering"]

(ACT_NONE, ACT_KEY, ACT_MEDIA, ACT_MOUSE_BTN, ACT_SCROLL, ACT_MOUSE_MOVE, ACT_PAD_BTN,
 ACT_AXIS, ACT_CENTER, ACT_PROFILE, ACT_LIGHT) = range(11)
PROF_NEXT, PROF_PREV = 0xF0, 0xF1
AXES = ["lx", "ly", "rx", "ry", "triggers"]
AXIS_FREE = 0x80
PAD_NAMES = ["", "a", "b", "x", "y", "lb", "rb", "lt", "rt", "back", "start", "ls", "rs",
             "up", "down", "left", "right", "guide"]
LIGHT_NAMES = ["toggle", "brighter", "dimmer"]
MOUSE_NAMES = {"left": 1, "right": 2, "middle": 4, "back": 8, "forward": 16}
MODIFIERS = {"ctrl": 0x01, "shift": 0x02, "alt": 0x04, "win": 0x08,
             "rctrl": 0x10, "rshift": 0x20, "ralt": 0x40, "rwin": 0x80}
MEDIA = {"volume-up": 0xE9, "volume-down": 0xEA, "mute": 0xE2, "play-pause": 0xCD,
         "next-track": 0xB5, "previous-track": 0xB6, "stop": 0xB7, "fast-forward": 0xB3,
         "rewind": 0xB4, "eject": 0xB8, "brightness-up": 0x6F, "brightness-down": 0x70,
         "calculator": 0x192, "mail": 0x18A, "browser": 0x196, "file-explorer": 0x194,
         "media-player": 0x183, "browser-home": 0x223, "browser-back": 0x224,
         "browser-forward": 0x225, "browser-refresh": 0x227, "browser-search": 0x221}


def _keycodes():
    k = {c: 0x04 + i for i, c in enumerate("abcdefghijklmnopqrstuvwxyz")}
    k.update({c: 0x1E + i for i, c in enumerate("1234567890")})
    k.update({"enter": 0x28, "esc": 0x29, "backspace": 0x2A, "tab": 0x2B, "space": 0x2C,
              "minus": 0x2D, "equal": 0x2E, "lbracket": 0x2F, "rbracket": 0x30,
              "backslash": 0x31, "semicolon": 0x33, "quote": 0x34, "grave": 0x35,
              "comma": 0x36, "period": 0x37, "slash": 0x38, "capslock": 0x39,
              "printscreen": 0x46, "scrolllock": 0x47, "pause": 0x48, "insert": 0x49,
              "home": 0x4A, "pageup": 0x4B, "delete": 0x4C, "end": 0x4D, "pagedown": 0x4E,
              "right": 0x4F, "left": 0x50, "down": 0x51, "up": 0x52, "numlock": 0x53,
              "kp-slash": 0x54, "kp-star": 0x55, "kp-minus": 0x56, "kp-plus": 0x57,
              "kp-enter": 0x58, "kp-period": 0x63, "iso-backslash": 0x64, "menu": 0x65})
    k.update({f"f{i}": 0x39 + i for i in range(1, 13)})
    k.update({f"f{i}": 0x68 + i - 13 for i in range(13, 25)})
    k.update({f"kp{i}": 0x58 + i for i in range(1, 10)})
    k["kp0"] = 0x62
    return k


KEYCODES = _keycodes()
KEYNAMES = {v: k for k, v in KEYCODES.items()}


class ActionError(ValueError):
    pass


def _int8(v):
    v = int(v)
    if not -127 <= v <= 127:
        raise ActionError(f"{v} is out of range -127..127")
    return v & 0xFF


def _s8(b):
    return b - 256 if b > 127 else b


def parse_action(text):
    """Action text -> (type, p1, p2). See `detent.py actions`."""
    t = text.strip().lower()
    kind, _, arg = t.partition(":")
    try:
        if kind in ("", "none"):
            return (ACT_NONE, 0, 0)
        if kind == "key":
            mods, code = 0, 0
            for part in arg.split("+"):
                if part in MODIFIERS:
                    mods |= MODIFIERS[part]
                elif part in KEYCODES:
                    code = KEYCODES[part]
                elif re.fullmatch(r"0x[0-9a-f]{1,2}", part):
                    code = int(part, 16)
                else:
                    raise ActionError(f"unknown key '{part}'")
            return (ACT_KEY, mods, code)
        if kind == "media":
            u = MEDIA[arg] if arg in MEDIA else int(arg, 0)
            return (ACT_MEDIA, u & 0xFF, u >> 8)
        if kind == "mouse":
            return (ACT_MOUSE_BTN, MOUSE_NAMES[arg], 0)
        if kind == "scroll":
            v, _, h = arg.partition(",")
            return (ACT_SCROLL, _int8(v or 0), _int8(h or 0))
        if kind == "move":
            x, _, y = arg.partition(",")
            return (ACT_MOUSE_MOVE, _int8(x or 0), _int8(y or 0))
        if kind == "pad":
            return (ACT_PAD_BTN, PAD_NAMES.index(arg), 0)
        if kind == "axis":
            parts = arg.split(":")
            clicks = int(parts[1]) if len(parts) > 1 else 10
            if not 1 <= clicks <= 100:
                raise ActionError("clicks to full lock must be 1..100")
            return (ACT_AXIS, AXES.index(parts[0]) | (AXIS_FREE if "free" in parts[2:] else 0), clicks)
        if kind == "center":
            return (ACT_CENTER, 0, 0)
        if kind == "profile":
            target, _, mode = arg.partition(":")
            p = {"next": PROF_NEXT, "prev": PROF_PREV}.get(target)
            if p is None:
                p = int(target) - 1
            return (ACT_PROFILE, p, 1 if mode == "hold" else 0)
        if kind == "light":
            return (ACT_LIGHT, LIGHT_NAMES.index(arg), 0)
    except (KeyError, IndexError, ValueError) as e:
        if isinstance(e, ActionError):
            raise
        raise ActionError(f"cannot understand action '{text}'") from None
    raise ActionError(f"unknown action type '{kind}'")


def format_action(a):
    """(type, p1, p2) -> action text"""
    t, p1, p2 = a
    if t == ACT_NONE:
        return "none"
    if t == ACT_KEY:
        parts = [n for n, b in MODIFIERS.items() if p1 & b]
        if p2:
            parts.append(KEYNAMES.get(p2, f"0x{p2:02x}"))
        return "key:" + "+".join(parts)
    if t == ACT_MEDIA:
        u = p1 | p2 << 8
        name = next((n for n, v in MEDIA.items() if v == u), None)
        return "media:" + (name or f"0x{u:03x}")
    if t == ACT_MOUSE_BTN:
        return "mouse:" + next((n for n, v in MOUSE_NAMES.items() if v == p1), str(p1))
    if t == ACT_SCROLL:
        return f"scroll:{_s8(p1)}" + (f",{_s8(p2)}" if p2 else "")
    if t == ACT_MOUSE_MOVE:
        return f"move:{_s8(p1)},{_s8(p2)}"
    if t == ACT_PAD_BTN:
        return "pad:" + (PAD_NAMES[p1] if p1 < len(PAD_NAMES) else str(p1))
    if t == ACT_AXIS:
        axis = AXES[p1 & 0x0F] if (p1 & 0x0F) < len(AXES) else "lx"
        return f"axis:{axis}:{p2}" + (":free" if p1 & AXIS_FREE else "")
    if t == ACT_CENTER:
        return "center"
    if t == ACT_PROFILE:
        target = {PROF_NEXT: "next", PROF_PREV: "prev"}.get(p1, str(p1 + 1))
        return f"profile:{target}" + (":hold" if p2 == 1 else "")
    if t == ACT_LIGHT:
        return "light:" + (LIGHT_NAMES[p1] if p1 < len(LIGHT_NAMES) else str(p1))
    return f"raw:{t},{p1},{p2}"


# ----------------------------------------------------------------- board layout
class Layout:
    """Where things live in the settings block; reported by the firmware (INFO)."""

    def __init__(self, info):
        self.fw = f"{info[0]}.{info[1]}.{info[2]}"
        self.board, self.buttons, self.knob = info[3], info[4], info[5]
        self.size, self.profiles, self.usb, self.leds = info[6], info[7], info[8], info[9]
        self.combos, self.prof_off, self.prof_size, self.combo_off = info[10:14]
        self.version, self.current = info[14], info[15]
        self.inputs = self.buttons + 2
        names, n = [], 1
        for i in range(self.buttons):
            if i == self.knob:
                names.append("knob")
            else:
                names.append(f"key{n}")
                n += 1
        self.names = names + ["right", "left"]

    def input_index(self, name):
        try:
            return self.names.index(name.lower())
        except ValueError:
            raise ActionError(f"unknown input '{name}' (use {', '.join(self.names)})") from None


class Settings:
    def __init__(self, layout, data):
        self.L = layout
        self.b = bytearray(data)

    # profiles
    def _poff(self, p):
        return self.L.prof_off + p * self.L.prof_size

    def action(self, p, i):
        o = self._poff(p) + 4 + 3 * i
        return tuple(self.b[o:o + 3])

    def set_action(self, p, i, a):
        o = self._poff(p) + 4 + 3 * i
        self.b[o:o + 3] = bytes(a)

    def color(self, p):
        o = self._poff(p)
        return "#%02x%02x%02x" % tuple(self.b[o:o + 3])

    def set_color(self, p, text):
        m = re.fullmatch(r"#?([0-9a-fA-F]{6})", text.strip())
        if not m:
            raise ActionError(f"color must look like #00aaff, got '{text}'")
        o = self._poff(p)
        self.b[o:o + 3] = bytes.fromhex(m.group(1))

    def effect(self, p):
        e = self.b[self._poff(p) + 3]
        return EFFECTS[e] if e < len(EFFECTS) else "solid"

    def set_effect(self, p, name):
        if name not in EFFECTS:
            raise ActionError(f"light effect must be one of {', '.join(EFFECTS)}")
        self.b[self._poff(p) + 3] = EFFECTS.index(name)

    # combos
    def combos(self):
        out = []
        for n in range(self.L.combos):
            o = self.L.combo_off + 4 * n
            c = self.b[o:o + 4]
            if c[0] != COMBO_EMPTY:
                out.append((n, c[0] >> 4, c[0] & 0x0F, c[1] >> 5, (c[1] & 0x1F, c[2], c[3])))
        return out

    def add_combo(self, held, trig, prof, a):
        if held >= self.L.buttons:
            raise ActionError("the held input must be a key or the knob button")
        for n in range(self.L.combos):
            o = self.L.combo_off + 4 * n
            c = self.b[o:o + 4]
            if c[0] != COMBO_EMPTY and c[0] == (held << 4 | trig) and c[1] >> 5 == prof:
                break                                   # replace the same combo
        else:
            free = [n for n in range(self.L.combos) if self.b[self.L.combo_off + 4 * n] == COMBO_EMPTY]
            if not free:
                raise ActionError(f"all {self.L.combos} combo slots are used")
            o = self.L.combo_off + 4 * free[0]
        self.b[o:o + 4] = bytes([held << 4 | trig, prof << 5 | a[0], a[1], a[2]])

    def del_combo(self, n):
        o = self.L.combo_off + 4 * n
        self.b[o:o + 4] = bytes([COMBO_EMPTY] * 4)

    def clear_combos(self):
        for n in range(self.L.combos):
            self.del_combo(n)

    def finalize(self):
        self.b[S_MAGIC], self.b[S_VERSION] = MAGIC, SETTINGS_VERSION
        self.b[-1] = sum(self.b[:-1]) & 0xFF
        return bytes(self.b)

    # JSON (same format as the setup page)
    def to_json(self):
        L = self.L
        f = self.b[S_FLAGS]
        return {
            "format": "detent-settings", "version": SETTINGS_VERSION,
            "board": {"inputs": L.names},
            "options": {
                "brightness": self.b[S_BRIGHTNESS], "holdMs": self.b[S_HOLD] * 10,
                "debounceMs": self.b[S_DEBOUNCE], "tapMs": self.b[S_TAP],
                "clicksPerTurn": self.b[S_DETENTS], "startProfile": self.b[S_START] + 1,
                "lightSpeed": self.b[S_SPEED], "reverseKnob": bool(f & FLAG_INVERT),
                "halfStep": bool(f & FLAG_HALFSTEP), "xbox360": bool(f & FLAG_XBOX360)},
            "profiles": [{"color": self.color(p), "light": self.effect(p),
                          "actions": {L.names[i]: format_action(self.action(p, i))
                                      for i in range(L.inputs)}}
                         for p in range(L.profiles)],
            "combos": [{"hold": L.names[h], "then": L.names[t],
                        "profile": "all" if p == ALL_PROFILES else p + 1,
                        "action": format_action(a)} for _, h, t, p, a in self.combos()],
        }

    def from_json(self, j):
        L, o = self.L, j.get("options", {})
        if j.get("format") != "detent-settings":
            raise ActionError("not a Detent settings file")
        b = self.b
        b[S_BRIGHTNESS] = int(o.get("brightness", b[S_BRIGHTNESS]))
        b[S_HOLD] = max(10, min(100, int(o.get("holdMs", b[S_HOLD] * 10)) // 10))
        b[S_DEBOUNCE] = max(1, min(30, int(o.get("debounceMs", b[S_DEBOUNCE]))))
        b[S_TAP] = max(1, min(50, int(o.get("tapMs", b[S_TAP]))))
        b[S_DETENTS] = max(4, min(100, int(o.get("clicksPerTurn", b[S_DETENTS]))))
        b[S_START] = max(0, min(L.profiles - 1, int(o.get("startProfile", b[S_START] + 1)) - 1))
        b[S_SPEED] = max(1, min(10, int(o.get("lightSpeed", b[S_SPEED]))))
        flags = 0
        flags |= FLAG_INVERT if o.get("reverseKnob") else 0
        flags |= FLAG_HALFSTEP if o.get("halfStep") else 0
        flags |= FLAG_XBOX360 if o.get("xbox360") else 0
        b[S_FLAGS] = flags
        for p, prof in enumerate(j.get("profiles", [])[:L.profiles]):
            self.set_color(p, prof.get("color", self.color(p)))
            self.set_effect(p, prof.get("light", self.effect(p)))
            for name, text in prof.get("actions", {}).items():
                if name in L.names:
                    self.set_action(p, L.input_index(name), parse_action(text))
        self.clear_combos()
        for c in j.get("combos", []):
            prof = ALL_PROFILES if c.get("profile", "all") == "all" else int(c["profile"]) - 1
            self.add_combo(L.input_index(c["hold"]), L.input_index(c["then"]), prof,
                           parse_action(c["action"]))


# ----------------------------------------------------------------- device access
class Device:
    def __init__(self):
        try:
            import hid
        except ImportError:
            sys.exit("Missing module: pip install hidapi")
        path = next((d["path"] for d in hid.enumerate(VID, PID) if d.get("usage_page") == USAGE_PAGE), None)
        if path is None:
            sys.exit("No Detent macropad found. Is it plugged in and running the Detent firmware?")
        self.dev = hid.device()
        self.dev.open_path(path)
        self.seq = 0
        self.layout = Layout(self.cmd(CMD_INFO)[:16])

    def close(self):
        self.dev.close()

    def cmd(self, cmd, off=0, data=b"", wait=True):
        self.seq = (self.seq + 1) & 0x7F
        n = len(data)
        pkt = bytes([cmd, self.seq, off, n]) + bytes(data)
        self.dev.send_feature_report(b"\x00" + pkt.ljust(FEATURE_LEN, b"\x00"))
        if not wait:
            return b""
        for _ in range(200):
            r = bytes(self.dev.get_feature_report(0, FEATURE_LEN + 1))
            r = r[1:] if len(r) == FEATURE_LEN + 1 else r
            if len(r) >= 4 and r[0] == cmd and r[1] == self.seq:
                if r[2]:
                    raise RuntimeError(f"device refused the command: {STATUS_TEXT.get(r[2], r[2])}")
                if r[3] != PROTOCOL:
                    sys.exit(f"Firmware speaks protocol {r[3]}, this tool needs {PROTOCOL}. "
                             "Flash the matching firmware with flash.bat.")
                return r[4:]
            time.sleep(0.002)
        raise RuntimeError("no answer from the device")

    def read(self):
        data = bytearray()
        while len(data) < self.layout.size:
            n = min(CHUNK, self.layout.size - len(data))
            data += self.cmd(CMD_READ, len(data), bytes(n))[:n]
        return Settings(self.layout, data)

    def write(self, settings, save=True):
        """Send settings, make them live, store them. Returns True if the device restarts."""
        data = settings.finalize()
        for off in range(0, len(data), CHUNK):
            self.cmd(CMD_WRITE, off, data[off:off + CHUNK])
        reconnect = self.cmd(CMD_COMMIT)[0]
        if save:
            self.cmd(CMD_SAVE)
            if reconnect:
                self.cmd(CMD_REBOOT, wait=False)
        return bool(reconnect)

    def status(self):
        r = self.cmd(CMD_STATUS)
        return int.from_bytes(r[0:2], "little", signed=True), r[2], r[3], r[4]


# ----------------------------------------------------------------- commands
def print_settings(s):
    L, b = s.L, s.b
    print(f"options       brightness {b[S_BRIGHTNESS]}, long press {b[S_HOLD] * 10} ms, "
          f"debounce {b[S_DEBOUNCE]} ms, tap {b[S_TAP]} ms")
    flags = [n for n, f in (("reverse knob", FLAG_INVERT), ("half step", FLAG_HALFSTEP),
                            ("Xbox 360 identity", FLAG_XBOX360)) if b[S_FLAGS] & f]
    print(f"              {b[S_DETENTS]} clicks per turn, light speed {b[S_SPEED]}, "
          f"starts in profile {b[S_START] + 1}" + (", " + ", ".join(flags) if flags else ""))
    for p in range(L.profiles):
        print(f"\nprofile {p + 1}     light {s.effect(p)} {s.color(p)}")
        for i in range(L.inputs):
            print(f"  {L.names[i]:<10}  {format_action(s.action(p, i))}")
    combos = s.combos()
    print(f"\ncombos ({len(combos)} of {L.combos} used)")
    for n, h, t, p, a in combos:
        where = "all profiles" if p == ALL_PROFILES else f"profile {p + 1}"
        what = f"hold {L.names[h]} (long press)" if h == t else f"hold {L.names[h]} + {L.names[t]}"
        print(f"  {n + 1:>2}. {what:<28} {format_action(a):<24} {where}")


def cmd_info(d):
    L = d.layout
    print(f"Detent firmware {L.fw}, board {L.board}: {L.buttons} buttons, {L.leds} LEDs")
    print(f"USB layout    {USB_LAYOUTS[L.usb] if L.usb < 3 else L.usb}")
    print(f"profiles      {L.profiles} (now using {L.current + 1}), {L.combos} combo slots")
    print(f"inputs        {' '.join(L.names)}")


def cmd_watch(d):
    print("Turn the knob / press keys. Ctrl+C stops.")
    last = None
    try:
        while True:
            st = d.status()
            if st != last:
                pos, btn, prof, flags = st
                keys = " ".join(d.layout.names[i] for i in range(d.layout.buttons) if btn & (1 << i))
                axis = "axis" if flags & 2 else "knob"
                print(f"profile {prof + 1}  {axis} {pos:+4d}  keys: {keys or '-'}")
                last = st
            time.sleep(0.02)
    except KeyboardInterrupt:
        pass


def apply(d, s):
    if d.write(s):
        print("saved - the macropad reconnects to add or remove the gamepad")
    else:
        print("saved")


def parse_profile(d, text):
    if text == "all":
        return ALL_PROFILES
    p = int(text) - 1
    if not 0 <= p < d.layout.profiles:
        raise ActionError(f"profile must be 1..{d.layout.profiles}")
    return p


OPTIONS = {
    "brightness": (S_BRIGHTNESS, 0, 255, 1), "long-press-ms": (S_HOLD, 100, 1000, 10),
    "debounce-ms": (S_DEBOUNCE, 1, 30, 1), "tap-ms": (S_TAP, 1, 50, 1),
    "clicks-per-turn": (S_DETENTS, 4, 100, 1), "light-speed": (S_SPEED, 1, 10, 1),
}
FLAG_OPTIONS = {"reverse-knob": FLAG_INVERT, "half-step": FLAG_HALFSTEP, "xbox360": FLAG_XBOX360}


def cmd_actions():
    print("""Actions
  none
  key:ctrl+shift+a      keyboard key with modifiers (ctrl shift alt win, rctrl ...)
  media:volume-up       media key
  mouse:left            mouse button: left right middle back forward
  scroll:-1             scroll down one notch (scroll:0,1 = one notch right)
  move:10,0             move the mouse pointer (x, y)
  pad:a                 gamepad button
  axis:lx:10[:free]     knob only: gamepad axis, clicks from center to full lock;
                        free = no hard stop. Axes: """ + " ".join(AXES) + """
  center                make the current knob position the axis center
  profile:2             switch profile; profile:next, profile:prev; profile:2:hold
                        = only while held
  light:toggle          lights on/off; light:brighter, light:dimmer
Light effects: """ + " ".join(EFFECTS) + """
Gamepad buttons: """ + " ".join(PAD_NAMES[1:]) + """
Media keys: """ + " ".join(MEDIA) + """
Keys: """ + " ".join(KEYCODES))


def main(argv):
    if not argv or argv[0] in ("-h", "--help", "help"):
        print(__doc__)
        print("OPTIONS: " + " ".join(OPTIONS) + " start-profile " + " ".join(FLAG_OPTIONS) + " (on/off)")
        return
    c, args = argv[0], argv[1:]
    if c == "actions":
        cmd_actions()
        return
    d = Device()
    try:
        L = d.layout
        if c == "info":
            cmd_info(d)
        elif c == "show":
            cmd_info(d)
            print()
            print_settings(d.read())
        elif c == "watch":
            cmd_watch(d)
        elif c == "set" and len(args) == 3:
            s = d.read()
            s.set_action(parse_profile(d, args[0]), L.input_index(args[1]), parse_action(args[2]))
            apply(d, s)
        elif c == "combo" and args[:1] == ["add"] and len(args) >= 4:
            prof = ALL_PROFILES
            if "--profile" in args:
                k = args.index("--profile")
                prof = parse_profile(d, args[k + 1])
                args = args[:k] + args[k + 2:]
            s = d.read()
            s.add_combo(L.input_index(args[1]), L.input_index(args[2]), prof, parse_action(args[3]))
            apply(d, s)
        elif c == "combo" and args[:1] == ["del"] and len(args) == 2:
            s = d.read()
            s.del_combo(int(args[1]) - 1)
            apply(d, s)
        elif c == "light" and len(args) >= 2:
            s = d.read()
            p = parse_profile(d, args[0])
            s.set_effect(p, args[1])
            if len(args) > 2:
                s.set_color(p, args[2])
            apply(d, s)
        elif c == "option" and len(args) == 2:
            s = d.read()
            name, val = args[0], args[1]
            if name in OPTIONS:
                off, lo, hi, unit = OPTIONS[name]
                v = int(val)
                if not lo <= v <= hi:
                    raise ActionError(f"{name} must be {lo}..{hi}")
                s.b[off] = v // unit
            elif name == "start-profile":
                s.b[S_START] = parse_profile(d, val)
            elif name in FLAG_OPTIONS:
                on = val.lower() in ("on", "1", "yes", "true")
                s.b[S_FLAGS] = (s.b[S_FLAGS] & ~FLAG_OPTIONS[name]) | (FLAG_OPTIONS[name] if on else 0)
            else:
                raise ActionError(f"unknown option '{name}'")
            apply(d, s)
        elif c == "profile" and len(args) == 1:
            d.cmd(CMD_PROFILE, parse_profile(d, args[0]))
        elif c == "export" and len(args) == 1:
            with open(args[0], "w", encoding="utf-8") as f:
                json.dump(d.read().to_json(), f, indent=2)
            print(f"saved to {args[0]}")
        elif c == "import" and len(args) == 1:
            with open(args[0], encoding="utf-8") as f:
                j = json.load(f)
            s = d.read()
            s.from_json(j)
            apply(d, s)
        elif c == "defaults":
            reconnect = d.cmd(CMD_DEFAULTS)[0]
            d.cmd(CMD_SAVE)
            if reconnect:
                d.cmd(CMD_REBOOT, wait=False)
            print("factory settings restored")
        elif c == "recenter":
            d.cmd(CMD_RECENTER)
        elif c == "reboot":
            d.cmd(CMD_REBOOT, wait=False)
        elif c == "bootloader":
            d.cmd(CMD_BOOTLOADER, wait=False)
            print("the macropad is now in its USB bootloader")
        else:
            sys.exit("unknown command - run 'detent.py help'")
    except ActionError as e:
        sys.exit(f"error: {e}")
    finally:
        d.close()


if __name__ == "__main__":
    main(sys.argv[1:])
