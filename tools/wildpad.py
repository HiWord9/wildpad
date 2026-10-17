#!/usr/bin/env python3
"""Wildpad - command line tool for macropads running the Wildpad firmware.

  python wildpad.py info                         firmware, board and USB layout
  python wildpad.py show                         every profile, combo and option
  python wildpad.py watch                        live knob / keys / profile (Ctrl+C stops)
  python wildpad.py set 1 key1 key:ctrl+c        profile 1, key 1 -> Ctrl+C
  python wildpad.py set 2 right axis:lx:10       profile 2, knob -> gamepad stick X
  python wildpad.py set 1 knob key:alt --now     own action right away (see below)
  python wildpad.py set 1 key1 "seq:tap pad:a; wait 60; tap pad:a; hold pad:rt"
                                                several steps (see 'actions')
  python wildpad.py combo add knob key1 profile:1           hold knob + key 1
  python wildpad.py combo add key3 right scroll:-1 --profile 2
  python wildpad.py combo add key1 key1 key:esc  hold key 1 alone (long press)
  python wildpad.py combo del 3                  remove combo number 3
  python wildpad.py light 1 breathe #ff8800      profile 1 light effect and color
  python wildpad.py option brightness 200        see OPTIONS below
  python wildpad.py profile 2                    switch to profile 2 now
  python wildpad.py export my.json               save everything to a file
  python wildpad.py import my.json               load a file (saved on the device)
  python wildpad.py defaults                     factory settings (saved)
  python wildpad.py board                        which board this is, and the known ones
  python wildpad.py board 2                      it is board 2 (factory settings for it)
  python wildpad.py board custom P1.1 P1.7 P1.6 knob:P3.3 leds:3
                                                a board of your own: key pins in order
  python wildpad.py bootloader                   jump to the USB bootloader
  python wildpad.py actions                      list action and key names

Inputs: key1 key2 key3 ... knob (press), right / left (knob turns).
A key that starts combos (or has a long press) sends its own action when released,
unless a combo was used. --now sends it on press and holds it while combos still
work, e.g. knob = Alt with combos knob+right = Tab, knob+left = Shift+Tab for
window switching. Without --now/--on-release, modifiers and gamepad buttons are
sent right away and everything else on release.
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
SETTINGS_VERSION = 5

(CMD_INFO, CMD_READ, CMD_WRITE, CMD_COMMIT, CMD_SAVE, CMD_DEFAULTS, CMD_STATUS,
 CMD_RECENTER, CMD_REBOOT, CMD_BOOTLOADER, CMD_PROFILE, CMD_INJECT, CMD_BOARD) = range(1, 14)
STATUS_TEXT = {1: "unknown command", 2: "invalid settings", 3: "bad offset/length"}
USB_LAYOUTS = ["keyboard", "keyboard + gamepad"]

# ----------------------------------------------------------------- settings layout
S_MAGIC, S_VERSION, S_FLAGS, S_BRIGHTNESS, S_HOLD, S_DEBOUNCE, S_TAP, S_DETENTS, \
    S_START, S_SPEED, S_TURN_HOLD, S_PROFILES, S_BOARD, S_MAP = range(14)
BOARDS = {1: "3 keys + knob", 2: "6 keys + knob", 3: "6 keys + knob, keys wired in reverse"}
MAP_LEN, MAP_LED_REVERSED, MAP_NO_KNOB = 10, 0x10, 0x20
MAGIC = 0xA5
FLAG_INVERT, FLAG_HALFSTEP = 0x01, 0x02
ALL_PROFILES = 7
REC_END, REC_SEQ = 0xFF, 0x80
ST_TAP, ST_HOLD, ST_WAIT, ST_REL = 0x00, 0x20, 0x40, 0x60
EFFECTS = ["off", "solid", "breathe", "rainbow", "press", "steering"]

(ACT_NONE, ACT_KEY, ACT_MEDIA, ACT_MOUSE_BTN, ACT_SCROLL, ACT_MOUSE_MOVE, ACT_PAD_BTN,
 ACT_AXIS, ACT_CENTER, ACT_PROFILE, ACT_LIGHT, ACT_SEQ) = range(12)
HOLD_MODS = 0xDD                     # what an app switcher keeps held: all modifiers but Shift
ACT_TYPE_MASK, ACT_NOW = 0x1F, 0x80
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


def default_now(a):
    """Modifier-only keys and gamepad buttons are held right away by default."""
    return (a[0] == ACT_KEY and a[1] and not a[2]) or a[0] == ACT_PAD_BTN


def _int8(v):
    v = int(v)
    if not -127 <= v <= 127:
        raise ActionError(f"{v} is out of range -127..127")
    return v & 0xFF


def _s8(b):
    return b - 256 if b > 127 else b


def parse_action(text):
    """Action text -> (type, p1, p2). See `wildpad.py actions`."""
    t = text.strip().lower()
    kind, _, arg = t.partition(":")
    try:
        if kind in ("", "none"):
            return (ACT_NONE, 0, 0)
        if kind == "key":
            combo, _, flag = arg.partition(":")
            mods, code = 0, 0
            for part in combo.split("+"):
                if part in MODIFIERS:
                    mods |= MODIFIERS[part]
                elif part in KEYCODES:
                    code = KEYCODES[part]
                elif re.fullmatch(r"0x[0-9a-f]{1,2}", part):
                    code = int(part, 16)
                else:
                    raise ActionError(f"unknown key '{part}'")
            if flag:
                raise ActionError(f"unknown key option '{flag}'")
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
        self.rec_size, self.prof_off, self.prof_size, self.rec_off = info[10:14]
        self.version, self.current = info[14], info[15]
        self.inputs = info[16] if len(info) > 16 else self.buttons + 2
        names, n = [], 1
        for i in range(self.buttons):
            if i == self.knob:
                names.append("knob")
            else:
                names.append(f"key{n}")
                n += 1
        self.names = names + ["right", "left"][:self.inputs - self.buttons]

    @property
    def board_name(self):
        return BOARDS.get(self.board, "board of your own")

    def input_index(self, name):
        try:
            return self.names.index(name.lower())
        except ValueError:
            raise ActionError(f"unknown input '{name}' (use {', '.join(self.names)})") from None


class Settings:
    def __init__(self, layout, data):
        self.L = layout
        self.b = bytearray(data)
        self._parse_records()

    # profiles
    def _poff(self, p):
        return self.L.prof_off + p * self.L.prof_size

    def action(self, p, i):
        o = self._poff(p) + 4 + 3 * i
        return (self.b[o] & ACT_TYPE_MASK, self.b[o + 1], self.b[o + 2])

    def now(self, p, i):
        """Own action sent right away even when the button starts combos."""
        return bool(self.b[self._poff(p) + 4 + 3 * i] & ACT_NOW)

    def set_action(self, p, i, a, now=None):
        """now=None picks the usual choice: right away for modifiers and gamepad buttons."""
        if now is None:
            now = default_now(a)
        o = self._poff(p) + 4 + 3 * i
        self.b[o:o + 3] = bytes([a[0] | (ACT_NOW if now and i < self.L.buttons else 0), a[1], a[2]])

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

    # profiles in use: the room of the others goes to combos and sequences
    @property
    def np(self):
        n = self.b[S_PROFILES]
        return n if 1 <= n <= self.L.profiles else self.L.profiles

    @property
    def rec_off(self):
        return self.L.prof_off + self.np * self.L.prof_size

    @property
    def rec_size(self):
        return self.L.size - 1 - self.rec_off

    def set_profile_count(self, n):
        if not 1 <= n <= self.L.profiles:
            raise ActionError(f"profiles must be 1..{self.L.profiles}")
        old = self.np
        if n < old:
            self.cs = [c for c in self.cs if (c[2] == ALL_PROFILES or c[2] < n)
                       and not (c[3][0] == ACT_PROFILE and n <= c[3][1] < PROF_NEXT)]
        for q in range(old, n):                             # new profiles start empty
            o = self._poff(q)
            self.b[o:o + self.L.prof_size] = bytes(self.L.prof_size)
            self.b[o:o + 4] = bytes([255, 255, 255, 1])
        self.b[S_PROFILES] = 0 if n == self.L.profiles else n
        if self.b[S_START] >= n:
            self.b[S_START] = 0

    # records: combos and sequences, packed after the profiles
    def _parse_records(self):
        self.cs, self.seqs = [], {}
        i, end, b = self.rec_off, self.L.size - 1, self.b
        while i < end and b[i] != REC_END:
            if b[i] < REC_SEQ:
                self.cs.append((b[i] >> 4, b[i] & 0x0F, b[i + 1] >> 5, (b[i + 1] & 0x1F, b[i + 2], b[i + 3])))
                i += 4
                continue
            press, release, stop = [], [], i + 2 + b[i + 1]
            steps, j = press, i + 2
            while j < stop:
                op = b[j] & 0xE0
                if op == ST_WAIT:
                    ms = (b[j] & 0x1F) * 20
                    if steps and steps[-1][0] == "wait":
                        steps[-1] = ("wait", steps[-1][1] + ms)
                    else:
                        steps.append(("wait", ms))
                    j += 1
                elif op == ST_REL:
                    steps, j = release, j + 1
                else:
                    steps.append(("hold" if op == ST_HOLD else "tap", (b[j] & 0x1F, b[j + 1], b[j + 2])))
                    j += 3
            self.seqs[b[i] & 0x7F] = (press, release)
            i = stop

    def _records(self):
        order, out = [], []

        def ref(a):
            if a[0] != ACT_SEQ:
                return a
            if a[1] not in self.seqs:
                return (ACT_NONE, 0, 0)
            if a[1] not in order:
                order.append(a[1])
            return (ACT_SEQ, order.index(a[1]), 0)

        for p in range(self.np):
            for i in range(self.L.inputs):
                a = self.action(p, i)
                if a[0] == ACT_SEQ:
                    o = self._poff(p) + 4 + 3 * i
                    self.b[o:o + 3] = bytes([(self.b[o] & ACT_NOW) | ACT_SEQ, ref(a)[1], 0])
        for h, t, pr, a in self.cs:
            a = ref(a)
            out += [h << 4 | t, pr << 5 | a[0], a[1], a[2]]
        for k, sid in enumerate(order):
            body = self._seq_bytes(self.seqs[sid])
            out += [REC_SEQ | k, len(body)] + body
        self.seqs = {k: self.seqs[sid] for k, sid in enumerate(order)}
        return out

    @staticmethod
    def _seq_bytes(seq):
        def steps(lst):
            out = []
            for op, x in lst:
                if op == "wait":
                    n = max(1, round(x / 20))
                    while n > 0:
                        out.append(ST_WAIT | min(31, n))
                        n -= 31
                else:
                    out += [(ST_HOLD if op == "hold" else ST_TAP) | x[0], x[1], x[2]]
            return out
        press, release = seq
        return steps(press) + ([ST_REL] + steps(release) if release else [])

    def used_bytes(self):
        saved = bytes(self.b), dict(self.seqs)
        n = len(self._records())
        self.b[:], self.seqs = saved[0], saved[1]
        return n

    # actions as text (sequences need the settings they live in)
    def text(self, a):
        if a[0] != ACT_SEQ:
            return format_action(a)
        press, release = self.seqs.get(a[1], ([], []))
        fmt = lambda lst: [f"wait {x}" if op == "wait" else f"{op} {format_action(x)}" for op, x in lst]
        return "seq:" + "; ".join(fmt(press) + (["on release"] + fmt(release) if release else []))

    def parse(self, text):
        t = text.strip()
        m = re.fullmatch(r"(key:[^:]+):hold", t.lower())
        if m:                        # older files: "keep Alt held, tap Tab" is two steps now
            _, mods, code = parse_action(m[1])
            sid = max(self.seqs, default=-1) + 1
            self.seqs[sid] = ([("hold", (ACT_KEY, mods & HOLD_MODS, 0)),
                               ("tap", (ACT_KEY, mods & ~HOLD_MODS & 0xFF, code))], [])
            return (ACT_SEQ, sid, 0)
        if not t.lower().startswith("seq:"):
            return parse_action(t)
        press, release = [], []
        steps = press
        for part in t[4:].split(";"):
            part = part.strip().lower()
            if not part:
                continue
            if part == "on release":
                steps = release
                continue
            op, _, arg = part.partition(" ")
            if op == "wait":
                ms = int(arg)
                if not 20 <= ms <= 10000:
                    raise ActionError("a wait must be 20..10000 ms")
                steps.append(("wait", ms))
            elif op == "tap" or (op == "hold" and steps is press):
                a = parse_action(arg)
                if a[0] == ACT_SEQ:
                    raise ActionError("a sequence can't contain another sequence")
                steps.append((op, a))
            else:
                raise ActionError(f"cannot understand step '{part}' (tap, hold, wait, on release)")
        sid = max(self.seqs, default=-1) + 1
        self.seqs[sid] = (press, release)
        return (ACT_SEQ, sid, 0)

    # combos
    def combos(self):
        return [(n, h, t, p, a) for n, (h, t, p, a) in enumerate(self.cs)]

    def add_combo(self, held, trig, prof, a):
        if held >= self.L.buttons:
            raise ActionError("the held input must be a key or the knob button")
        for n, (h, t, p, _) in enumerate(self.cs):
            if (h, t, p) == (held, trig, prof):
                self.cs[n] = (held, trig, prof, a)          # replace the same combo
                return
        self.cs.append((held, trig, prof, a))

    def del_combo(self, n):
        if not 0 <= n < len(self.cs):
            raise ActionError(f"there is no combo {n + 1}")
        del self.cs[n]

    def clear_combos(self):
        self.cs = []

    def finalize(self):
        rec, off = self._records(), self.rec_off
        if len(rec) > self.rec_size:
            raise ActionError(f"that doesn't fit: combos and steps need {len(rec)} bytes, "
                              f"the pad has {self.rec_size}" + (" (fewer profiles give more)" if self.np > 1 else ""))
        self.b[off:self.L.size] = bytes([REC_END] * (self.L.size - off))
        self.b[off:off + len(rec)] = bytes(rec)
        self.b[S_MAGIC], self.b[S_VERSION] = MAGIC, SETTINGS_VERSION
        self.b[-1] = sum(self.b[:-1]) & 0xFF
        return bytes(self.b)

    # JSON (same format as the setup page)
    def to_json(self):
        L = self.L
        f = self.b[S_FLAGS]
        return {
            "format": "wildpad-settings", "version": SETTINGS_VERSION,
            "board": dict({"id": L.board, "inputs": L.names},
                          **({"map": list(self.b[S_MAP:S_MAP + MAP_LEN])} if L.board == 0 else {})),
            "options": {
                "brightness": self.b[S_BRIGHTNESS], "holdMs": self.b[S_HOLD] * 10,
                "debounceMs": self.b[S_DEBOUNCE], "tapMs": self.b[S_TAP],
                "clicksPerTurn": self.b[S_DETENTS], "profiles": self.np, "startProfile": self.b[S_START] + 1,
                "lightSpeed": self.b[S_SPEED], "turnHoldMs": (self.b[S_TURN_HOLD] or 8) * 100,
                "reverseKnob": bool(f & FLAG_INVERT),
                "halfStep": bool(f & FLAG_HALFSTEP)},
            "profiles": [{"color": self.color(p), "light": self.effect(p),
                          "actions": {L.names[i]: self.text(self.action(p, i))
                                      for i in range(L.inputs)},
                          "rightAway": [L.names[i] for i in range(L.buttons) if self.now(p, i)]}
                         for p in range(self.np)],
            "combos": [{"hold": L.names[h], "then": L.names[t],
                        "profile": "all" if p == ALL_PROFILES else p + 1,
                        "action": self.text(a)} for _, h, t, p, a in self.combos()],
        }

    def from_json(self, j):
        L, o = self.L, j.get("options", {})
        if j.get("format") not in ("wildpad-settings", "detent-settings"):   # older name
            raise ActionError("not a Wildpad settings file")
        b = self.b
        b[S_BRIGHTNESS] = int(o.get("brightness", b[S_BRIGHTNESS]))
        b[S_HOLD] = max(10, min(100, int(o.get("holdMs", b[S_HOLD] * 10)) // 10))
        b[S_DEBOUNCE] = max(1, min(30, int(o.get("debounceMs", b[S_DEBOUNCE]))))
        b[S_TAP] = max(1, min(50, int(o.get("tapMs", b[S_TAP]))))
        b[S_DETENTS] = max(4, min(100, int(o.get("clicksPerTurn", b[S_DETENTS]))))
        self.set_profile_count(max(1, min(L.profiles, int(o.get("profiles", len(j.get("profiles", [])) or L.profiles)))))
        b[S_START] = max(0, min(self.np - 1, int(o.get("startProfile", b[S_START] + 1)) - 1))
        b[S_SPEED] = max(1, min(10, int(o.get("lightSpeed", b[S_SPEED]))))
        b[S_TURN_HOLD] = max(2, min(30, int(o.get("turnHoldMs", (b[S_TURN_HOLD] or 8) * 100)) // 100))
        flags = 0
        flags |= FLAG_INVERT if o.get("reverseKnob") else 0
        flags |= FLAG_HALFSTEP if o.get("halfStep") else 0
        b[S_FLAGS] = flags
        for p, prof in enumerate(j.get("profiles", [])[:self.np]):
            self.set_color(p, prof.get("color", self.color(p)))
            self.set_effect(p, prof.get("light", self.effect(p)))
            right_away = prof.get("rightAway")             # missing: usual choice per action
            for name, text in prof.get("actions", {}).items():
                if name in L.names:
                    now = None if right_away is None else name in right_away
                    self.set_action(p, L.input_index(name), self.parse(text), now)
        self.clear_combos()
        for c in j.get("combos", []):
            prof = ALL_PROFILES if c.get("profile", "all") == "all" else int(c["profile"]) - 1
            self.add_combo(L.input_index(c["hold"]), L.input_index(c["then"]), prof,
                           self.parse(c["action"]))


# ----------------------------------------------------------------- device access
class Device:
    def __init__(self):
        try:
            import hid
        except ImportError:
            sys.exit("Missing module: pip install hidapi")
        path = next((d["path"] for d in hid.enumerate(VID, PID) if d.get("usage_page") == USAGE_PAGE), None)
        if path is None:
            sys.exit("No Wildpad macropad found. Is it plugged in and running the Wildpad firmware?")
        self.dev = hid.device()
        self.dev.open_path(path)
        self.seq = 0
        self.layout = Layout(self.cmd(CMD_INFO)[:17])

    def close(self):
        self.dev.close()

    def set_board(self, board, pin_map=b""):
        """Tell the pad which board it is; it starts over with factory settings for it."""
        reconnect = self.cmd(CMD_BOARD, board, bytes(pin_map))[0]
        self.cmd(CMD_SAVE)
        self.layout = Layout(self.cmd(CMD_INFO)[:17])
        if reconnect:
            self.cmd(CMD_REBOOT, wait=False)
        return bool(reconnect)

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
    flags = [n for n, f in (("reverse knob", FLAG_INVERT), ("half step", FLAG_HALFSTEP)) if b[S_FLAGS] & f]
    print(f"              {b[S_DETENTS]} clicks per turn, light speed {b[S_SPEED]}, "
          f"starts in profile {b[S_START] + 1}" + (", " + ", ".join(flags) if flags else ""))
    for p in range(s.np):
        print(f"\nprofile {p + 1}     light {s.effect(p)} {s.color(p)}")
        for i in range(L.inputs):
            mark = "   (sent right away)" if i < L.buttons and s.now(p, i) else ""
            print(f"  {L.names[i]:<10}  {s.text(s.action(p, i))}{mark}")
    combos = s.combos()
    print(f"\ncombos ({len(combos)}; combos and steps use {s.used_bytes()} of {s.rec_size} bytes)")
    for n, h, t, p, a in combos:
        where = "all profiles" if p == ALL_PROFILES else f"profile {p + 1}"
        what = f"hold {L.names[h]} (long press)" if h == t else f"hold {L.names[h]} + {L.names[t]}"
        print(f"  {n + 1:>2}. {what:<28} {s.text(a):<24} {where}")


def cmd_info(d):
    L = d.layout
    print(f"Wildpad firmware {L.fw}, board {L.board} ({L.board_name}): {L.buttons} buttons, {L.leds} LEDs")
    print(f"USB layout    {USB_LAYOUTS[L.usb] if L.usb < len(USB_LAYOUTS) else L.usb}")
    print(f"profiles      up to {L.profiles}, now in profile {L.current + 1}")
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
    p, n = int(text) - 1, d.read().np                       # profiles in use
    if not 0 <= p < n:
        raise ActionError(f"profile must be 1..{n}" + (" (see: option profiles)" if n < d.layout.profiles else ""))
    return p


OPTIONS = {
    "brightness": (S_BRIGHTNESS, 0, 255, 1), "long-press-ms": (S_HOLD, 100, 1000, 10),
    "debounce-ms": (S_DEBOUNCE, 1, 30, 1), "tap-ms": (S_TAP, 1, 50, 1),
    "clicks-per-turn": (S_DETENTS, 4, 100, 1), "light-speed": (S_SPEED, 1, 10, 1),
    "turn-hold-ms": (S_TURN_HOLD, 200, 3000, 100),
}
FLAG_OPTIONS = {"reverse-knob": FLAG_INVERT, "half-step": FLAG_HALFSTEP}


def parse_pin_map(args):
    """'P1.1 P1.7 P1.6 knob:P3.3 leds:3 [reversed] [no-knob]' -> 10-byte board map."""
    def pin(text):
        m = re.fullmatch(r"[Pp]([13])\.([0-7])", text)
        if not m:
            raise ActionError(f"'{text}' is not a pin like P1.1 or P3.2")
        return (8 if m[1] == "3" else 0) + int(m[2])
    pins, knob, leds, flags = [], None, 0, 0
    for a in args:
        if a.lower().startswith("knob:"):
            knob = pin(a[5:])
        elif a.lower().startswith("leds:"):
            leds = int(a[5:])
        elif a.lower() == "reversed":
            flags |= MAP_LED_REVERSED
        elif a.lower() == "no-knob":
            flags |= MAP_NO_KNOB
        else:
            pins.append(pin(a))
    if knob is not None:
        pins.append(knob)
    if not 1 <= len(pins) <= 8 or not 0 <= leds <= 8:
        raise ActionError("1 to 8 buttons (keys + knob switch) and 0 to 8 LEDs")
    knob_index = len(pins) - 1 if knob is not None else 15
    return bytes([len(pins) | knob_index << 4, leds | flags] + pins + [0xFF] * (8 - len(pins)))


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
  seq:STEP; STEP; ...   several steps in order. Steps: tap ACTION, hold ACTION (pressed
                        until the control is let go; on a knob turn until it stops),
                        wait MS, and 'on release' - the steps after it run when the
                        control is let go. Examples:
                        seq:tap pad:a; wait 60; tap pad:a; hold pad:rt; on release; tap key:x
                        seq:hold key:alt; tap key:tab   (app switcher, e.g. on a knob turn)
Light effects: """ + " ".join(EFFECTS) + """
Gamepad buttons: """ + " ".join(PAD_NAMES[1:]) + """
Media keys: """ + " ".join(MEDIA) + """
Keys: """ + " ".join(KEYCODES))


def main(argv):
    if not argv or argv[0] in ("-h", "--help", "help"):
        print(__doc__)
        print("OPTIONS: " + " ".join(OPTIONS) + " profiles start-profile " + " ".join(FLAG_OPTIONS) + " (on/off)")
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
        elif c == "set" and len(args) >= 3:
            now = True if "--now" in args else False if "--on-release" in args else None
            args = [a for a in args if a not in ("--now", "--on-release")]
            s = d.read()
            s.set_action(parse_profile(d, args[0]), L.input_index(args[1]), s.parse(args[2]), now)
            apply(d, s)
        elif c == "combo" and args[:1] == ["add"] and len(args) >= 4:
            prof = ALL_PROFILES
            if "--profile" in args:
                k = args.index("--profile")
                prof = parse_profile(d, args[k + 1])
                args = args[:k] + args[k + 2:]
            s = d.read()
            s.add_combo(L.input_index(args[1]), L.input_index(args[2]), prof, s.parse(args[3]))
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
            elif name == "profiles":
                s.set_profile_count(int(val))
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
            want = j.get("board", {})
            if "id" in want and (want["id"] != L.board or want["id"] == 0):
                if d.set_board(want["id"], want.get("map", b"")):
                    sys.exit("The pad is now set up for the file's board and reconnects; run the import again.")
            s = d.read()
            s.from_json(j)
            apply(d, s)
        elif c == "defaults":
            reconnect = d.cmd(CMD_DEFAULTS)[0]
            d.cmd(CMD_SAVE)
            if reconnect:
                d.cmd(CMD_REBOOT, wait=False)
            print("factory settings restored")
        elif c == "board" and not args:
            print(f"this pad: board {L.board} ({L.board_name}), inputs {' '.join(L.names)}")
            print("known boards: " + ", ".join(f"{k} = {v}" for k, v in BOARDS.items()))
        elif c == "board" and args[0] == "custom":
            pin_map = parse_pin_map(args[1:])
            reconnect = d.set_board(0, pin_map)
            print("custom board set, factory settings for it" + (" (reconnecting)" if reconnect else ""))
        elif c == "board" and len(args) == 1:
            reconnect = d.set_board(int(args[0]))
            print(f"board {args[0]} ({BOARDS.get(int(args[0]), '?')}), factory settings for it"
                  + (" (reconnecting)" if reconnect else ""))
        elif c == "recenter":
            d.cmd(CMD_RECENTER)
        elif c == "reboot":
            d.cmd(CMD_REBOOT, wait=False)
        elif c == "bootloader":
            d.cmd(CMD_BOOTLOADER, wait=False)
            print("the macropad is now in its USB bootloader")
        else:
            sys.exit("unknown command - run 'wildpad.py help'")
    except ActionError as e:
        sys.exit(f"error: {e}")
    finally:
        d.close()


if __name__ == "__main__":
    main(sys.argv[1:])
