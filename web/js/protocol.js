"use strict";
// =============================================================================== protocol
const VID = 0x1189, PID = 0x8890, PAGE = 0xFF54, FLEN = 40, CHUNK = 32, PROTO = 3, SVER = 5;
const CMD = { INFO: 1, READ: 2, WRITE: 3, COMMIT: 4, SAVE: 5, DEFAULTS: 6, STATUS: 7, RECENTER: 8, REBOOT: 9, BOOT: 10, PROFILE: 11, BOARD: 13 };
const STATUS_TEXT = { 1: "unknown command", 2: "invalid settings", 3: "bad offset or length" };
const S = { MAGIC: 0, VERSION: 1, FLAGS: 2, BRIGHT: 3, HOLD: 4, DEBOUNCE: 5, TAP: 6, DETENTS: 7, START: 8, SPEED: 9, TURN_HOLD: 10, PROFILES: 11, BOARD: 12, MAP: 13 };
// Boards: the same table as BOARDS in firmware/src/settings.c. A map is
// [buttons | knob switch index << 4, LEDs | flags, pin of each button (P1.x = x, P3.x = 8 + x)].
const MAP_LEN = 10, MAP_LED_REVERSED = 0x10, MAP_NO_KNOB = 0x20, NO_KNOB = 15;
const BOARDS = {
  1: { name: "3 keys and a knob", map: [0x34, 3, 1, 7, 6, 11] },
  2: { name: "6 keys and a knob", map: [0x67, 0x16, 10, 4, 5, 6, 7, 1, 11] },
  3: { name: "6 keys and a knob, wired in reverse", map: [0x67, 6, 1, 7, 6, 5, 4, 10, 11] },
};
const pinName = k => k < 8 ? `P1.${k}` : `P3.${k - 8}`;
const FLAG = { INVERT: 1, HALF: 2 };
const T = { NONE: 0, KEY: 1, MEDIA: 2, MOUSE: 3, SCROLL: 4, MOVE: 5, PAD: 6, AXIS: 7, CENTER: 8, PROFILE: 9, LIGHT: 10, SEQ: 11 };
// records after the profiles: combos and sequences (see docs/protocol.md)
const REC_END = 0xFF, REC_SEQ = 0x80, ST_TAP = 0x00, ST_HOLD = 0x20, ST_WAIT = 0x40, ST_REL = 0x60;
const HOLD_MODS = 0xDD;                                  // modifiers an app switcher keeps held: all but Shift
// "Keep Alt held, tap Tab" (app switchers) as two steps: hold the modifiers but Shift
// (on a knob turn: until it stops), tap the key with Shift
const holdTap = (m, mods, code) => A(T.SEQ, m.addSeq({ press: [{ op: "hold", a: A(T.KEY, mods & HOLD_MODS, 0) }, { op: "tap", a: A(T.KEY, mods & ~HOLD_MODS, code) }], release: [] }));
const PROF_NEXT = 0xF0, PROF_PREV = 0xF1, ALL = 7, EMPTY = 0xFF, AXIS_FREE = 0x80, TYPE_MASK = 0x1F, NOW = 0x80;
const EFFECTS = [
  ["off", "Off", "No light."],
  ["solid", "Solid", "Steady profile color."],
  ["breathe", "Breathe", "Slowly fades in and out."],
  ["rainbow", "Rainbow", "Cycles through all colors."],
  ["press", "Light on press", "Keys light up when pressed."],
  ["steering", "Knob position", "Shows the gamepad axis; needs the knob as an axis."],
];
const SWATCHES = ["#00aaff", "#00e0b0", "#36d03a", "#ffe000", "#ff8c00", "#ff2a2a", "#ff3fa0", "#dc00ff", "#ffffff"];

// =============================================================================== action catalog
const MEDIA = [
  [0xCD, "Play / pause", "Play/Pause", "play-pause"], [0xB5, "Next track", "Next", "next-track"],
  [0xB6, "Previous track", "Previous", "previous-track"], [0xB7, "Stop", "Stop", "stop"],
  [0xE9, "Volume up", "Vol +", "volume-up"], [0xEA, "Volume down", "Vol −", "volume-down"],
  [0xE2, "Mute", "Mute", "mute"], [0xB3, "Fast forward", "Fwd", "fast-forward"], [0xB4, "Rewind", "Rewind", "rewind"],
  [0x6F, "Screen brighter", "Screen +", "brightness-up"], [0x70, "Screen dimmer", "Screen −", "brightness-down"],
  [0x192, "Calculator", "Calc", "calculator"], [0x18A, "Mail", "Mail", "mail"], [0x196, "Web browser", "Browser", "browser"],
  [0x194, "File explorer", "Files", "file-explorer"], [0x183, "Media player", "Player", "media-player"],
  [0x223, "Browser home", "Home", "browser-home"], [0x224, "Browser back", "Back", "browser-back"],
  [0x225, "Browser forward", "Forward", "browser-forward"], [0x227, "Browser refresh", "Refresh", "browser-refresh"],
  [0x221, "Search", "Search", "browser-search"], [0xB8, "Eject", "Eject", "eject"],
];
const MOUSE = [[1, "Left click", "left"], [2, "Right click", "right"], [4, "Middle click", "middle"], [8, "Back button", "back"], [16, "Forward button", "forward"]];
const PAD = [null, ["A", "a"], ["B", "b"], ["X", "x"], ["Y", "y"], ["LB", "lb"], ["RB", "rb"], ["LT", "lt"], ["RT", "rt"],
  ["Back", "back"], ["Start", "start"], ["Left stick press", "ls"], ["Right stick press", "rs"], ["D-pad up", "up"],
  ["D-pad down", "down"], ["D-pad left", "left"], ["D-pad right", "right"], ["Guide", "guide"]];
const PAD_SHORT = { 11: "LS", 12: "RS", 13: "D-pad ↑", 14: "D-pad ↓", 15: "D-pad ←", 16: "D-pad →" };
const AXES = [["lx", "Left stick, left / right"], ["ly", "Left stick, up / down"], ["rx", "Right stick, left / right"],
  ["ry", "Right stick, up / down"], ["triggers", "Triggers: left = LT, right = RT"]];
const AXIS_SHORT = ["Stick X", "Stick Y", "Right stick X", "Right stick Y", "Triggers"];
const LIGHTS = [["toggle", "Lights on / off"], ["brighter", "Brighter"], ["dimmer", "Dimmer"]];
const MODS = [[1, "Ctrl", "ctrl"], [2, "Shift", "shift"], [4, "Alt", "alt"], [8, "Win", "win"],
  [16, "Right Ctrl", "rctrl"], [32, "Right Shift", "rshift"], [64, "Right Alt", "ralt"], [128, "Right Win", "rwin"]];
const MOD_CODES = { ControlLeft: 1, ShiftLeft: 2, AltLeft: 4, MetaLeft: 8, ControlRight: 16, ShiftRight: 32, AltRight: 64, MetaRight: 128, OSLeft: 8, OSRight: 128 };

// keyboard keys: usage -> {label, name (file format), code (KeyboardEvent.code), group}
const KEYS = new Map();
function addKey(u, label, name, code, group) { KEYS.set(u, { u, label, name, code, group }); }
"abcdefghijklmnopqrstuvwxyz".split("").forEach((c, i) => addKey(4 + i, c.toUpperCase(), c, "Key" + c.toUpperCase(), "Letters"));
"1234567890".split("").forEach((c, i) => addKey(0x1E + i, c, c, "Digit" + c, "Numbers"));
[[0x28, "Enter", "enter", "Enter"], [0x29, "Esc", "esc", "Escape"], [0x2A, "Backspace", "backspace", "Backspace"],
 [0x2B, "Tab", "tab", "Tab"], [0x2C, "Space", "space", "Space"], [0x39, "Caps Lock", "capslock", "CapsLock"],
 [0x65, "Menu", "menu", "ContextMenu"]].forEach(k => addKey(...k, "Editing"));
for (let i = 1; i <= 24; i++) addKey(i <= 12 ? 0x39 + i : 0x68 + i - 13, "F" + i, "f" + i, "F" + i, "Function keys");
[[0x4F, "→", "right", "ArrowRight"], [0x50, "←", "left", "ArrowLeft"], [0x51, "↓", "down", "ArrowDown"], [0x52, "↑", "up", "ArrowUp"],
 [0x4A, "Home", "home", "Home"], [0x4D, "End", "end", "End"], [0x4B, "Page Up", "pageup", "PageUp"], [0x4E, "Page Down", "pagedown", "PageDown"],
 [0x49, "Insert", "insert", "Insert"], [0x4C, "Delete", "delete", "Delete"]].forEach(k => addKey(...k, "Navigation"));
[[0x2D, "-", "minus", "Minus"], [0x2E, "=", "equal", "Equal"], [0x2F, "[", "lbracket", "BracketLeft"], [0x30, "]", "rbracket", "BracketRight"],
 [0x31, "\\", "backslash", "Backslash"], [0x33, ";", "semicolon", "Semicolon"], [0x34, "'", "quote", "Quote"], [0x35, "`", "grave", "Backquote"],
 [0x36, ",", "comma", "Comma"], [0x37, ".", "period", "Period"], [0x38, "/", "slash", "Slash"], [0x64, "\\ (ISO)", "iso-backslash", "IntlBackslash"]
].forEach(k => addKey(...k, "Symbols"));
[[0x46, "Print Screen", "printscreen", "PrintScreen"], [0x47, "Scroll Lock", "scrolllock", "ScrollLock"], [0x48, "Pause", "pause", "Pause"],
 [0x53, "Num Lock", "numlock", "NumLock"]].forEach(k => addKey(...k, "System"));
for (let i = 1; i <= 9; i++) addKey(0x58 + i, "Num " + i, "kp" + i, "Numpad" + i, "Numpad");
[[0x62, "Num 0", "kp0", "Numpad0"], [0x54, "Num /", "kp-slash", "NumpadDivide"], [0x55, "Num *", "kp-star", "NumpadMultiply"],
 [0x56, "Num −", "kp-minus", "NumpadSubtract"], [0x57, "Num +", "kp-plus", "NumpadAdd"], [0x58, "Num Enter", "kp-enter", "NumpadEnter"],
 [0x63, "Num .", "kp-period", "NumpadDecimal"]].forEach(k => addKey(...k, "Numpad"));
const KEY_BY_CODE = new Map([...KEYS.values()].map(k => [k.code, k]));
const KEY_BY_NAME = new Map([...KEYS.values()].map(k => [k.name, k]));

const s8 = b => b > 127 ? b - 256 : b;
const u8 = v => (Math.max(-127, Math.min(127, v | 0)) + 256) & 255;

const MOD_ORDER = [8, 1, 4, 2, 128, 16, 64, 32];        // display order: Win+Ctrl+Alt+Shift
function keyText(mods, code, short) {
  const parts = MOD_ORDER.filter(bit => mods & bit).map(bit => MODS.find(m => m[0] === bit)[1]).map(n => short ? n.replace("Right ", "R") : n);
  if (code) parts.push(KEYS.get(code)?.label ?? `Key 0x${code.toString(16)}`);
  return parts.join(short ? "+" : " + ");
}

// Short label (keycaps) and long label (inspector) for an action
function describe(a, short) {
  switch (a.t) {
    case T.NONE: return short ? "—" : "Nothing";
    case T.KEY: return keyText(a.p1, a.p2, short) || (short ? "—" : "Nothing");
    case T.MEDIA: { const u = a.p1 | a.p2 << 8; const m = MEDIA.find(x => x[0] === u); return m ? m[short ? 2 : 1] : `Media 0x${u.toString(16)}`; }
    case T.MOUSE: { const m = MOUSE.find(x => x[0] === a.p1); return m ? m[1] : "Mouse button"; }
    case T.SCROLL: {
      const v = s8(a.p1), h = s8(a.p2), out = [];
      if (v) out.push(`${short ? "Scroll" : "Scroll"} ${v > 0 ? "up" : "down"}${Math.abs(v) > 1 ? " ×" + Math.abs(v) : ""}`);
      if (h) out.push(`${out.length ? "" : "Scroll "}${h > 0 ? "right" : "left"}${Math.abs(h) > 1 ? " ×" + Math.abs(h) : ""}`);
      return out.join(", ") || "Scroll";
    }
    case T.MOVE: return `Move pointer ${s8(a.p1)}, ${s8(a.p2)}`;
    case T.PAD: return a.p1 < PAD.length && PAD[a.p1] ? (short ? (PAD_SHORT[a.p1] || PAD[a.p1][0]) : "Gamepad " + PAD[a.p1][0]) : "Gamepad";
    case T.AXIS: return short ? AXIS_SHORT[a.p1 & 15] || "Axis" : `Gamepad axis: ${AXES[a.p1 & 15]?.[1] ?? "?"}`;
    case T.CENTER: return short ? "Center" : "Center the knob axis";
    case T.PROFILE: {
      const name = a.p1 === PROF_NEXT ? "Next profile" : a.p1 === PROF_PREV ? "Previous profile" : `Profile ${a.p1 + 1}`;
      return a.p2 === 1 ? name + (short ? " (hold)" : " while held") : name;
    }
    case T.LIGHT: return LIGHTS[a.p1]?.[1] ?? "Lights";
    case T.SEQ: {
      const sq = model.seqs.get(a.p1);
      if (!sq) return short ? "Steps" : "Steps (missing)";
      const txt = describeSteps(sq.press) + (sq.release.length ? " · let go: " + describeSteps(sq.release) : "");
      if (!txt) return short ? "Steps" : "Steps (none yet)";
      return short && txt.length > 24 ? txt.slice(0, 23) + "…" : txt;
    }
  }
  return "Unknown";
}

function describeSteps(list) {
  return list.map(st => st.op === "wait" ? `${st.ms} ms` : (st.op === "hold" ? "hold " : "") + describe(st.a, true)).join(", ");
}

// File format action text, identical to tools/wildpad.py
function actionToText(a, m = model) {
  switch (a.t) {
    case T.SEQ: return "seq:" + seqToText(m.seqs.get(a.p1) || { press: [], release: [] });
    case T.NONE: return "none";
    case T.KEY: {
      const parts = MODS.filter(m => a.p1 & m[0]).map(m => m[2]);
      if (a.p2) parts.push(KEYS.get(a.p2)?.name ?? "0x" + a.p2.toString(16).padStart(2, "0"));
      return "key:" + parts.join("+");
    }
    case T.MEDIA: { const u = a.p1 | a.p2 << 8; const m = MEDIA.find(x => x[0] === u); return "media:" + (m ? m[3] : "0x" + u.toString(16).padStart(3, "0")); }
    case T.MOUSE: return "mouse:" + (MOUSE.find(x => x[0] === a.p1)?.[2] ?? a.p1);
    case T.SCROLL: return `scroll:${s8(a.p1)}` + (a.p2 ? `,${s8(a.p2)}` : "");
    case T.MOVE: return `move:${s8(a.p1)},${s8(a.p2)}`;
    case T.PAD: return "pad:" + (PAD[a.p1]?.[1] ?? a.p1);
    case T.AXIS: return `axis:${AXES[a.p1 & 15]?.[0] ?? "lx"}:${a.p2}` + (a.p1 & AXIS_FREE ? ":free" : "");
    case T.CENTER: return "center";
    case T.PROFILE: return "profile:" + (a.p1 === PROF_NEXT ? "next" : a.p1 === PROF_PREV ? "prev" : a.p1 + 1) + (a.p2 === 1 ? ":hold" : "");
    case T.LIGHT: return "light:" + (LIGHTS[a.p1]?.[0] ?? a.p1);
  }
  return "none";
}

function seqToText(sq) {
  const list = l => l.map(st => st.op === "wait" ? `wait ${st.ms}` : `${st.op} ${actionToText(st.a)}`);
  return [...list(sq.press), ...(sq.release.length ? ["on release", ...list(sq.release)] : [])].join("; ");
}

function textToSeq(text) {
  const sq = { press: [], release: [] };
  let list = sq.press;
  for (const raw of text.split(";")) {
    const t = raw.trim().toLowerCase();
    if (!t) continue;
    if (t === "on release") { list = sq.release; continue; }
    const [op, ...rest] = t.split(/\s+/), arg = rest.join(" ");
    if (op === "wait") {
      if (!/^\d+$/.test(arg) || +arg < 20 || +arg > 10000) throw new Error("a wait must be 20..10000 ms");
      list.push({ op: "wait", ms: Math.round(+arg / 20) * 20 });
    }
    else if (op === "tap" || (op === "hold" && list === sq.press)) {
      const a = textToAction(arg);
      if (a.t === T.SEQ) throw new Error("a sequence can't contain another sequence");
      list.push({ op, a });
    }
    else throw new Error(`cannot understand step "${raw.trim()}"`);
  }
  return sq;
}

function textToAction(text, m = model) {
  const t = String(text).trim().toLowerCase();
  if (t.startsWith("seq:")) return A(T.SEQ, m.addSeq(textToSeq(t.slice(4))));
  const i = t.indexOf(":"), kind = i < 0 ? t : t.slice(0, i), arg = i < 0 ? "" : t.slice(i + 1);
  const bad = () => { throw new Error(`cannot understand action "${text}"`); };
  const int8 = v => v === undefined || v === "" ? 0 : /^[+-]?\d+$/.test(v) && Math.abs(+v) <= 127 ? u8(+v) : bad();
  switch (kind) {
    case "": case "none": return A(T.NONE);
    case "key": {
      let mods = 0, code = 0;
      const [combo, flag] = arg.split(":");
      if (flag && flag !== "hold") bad();
      for (const part of combo.split("+")) {
        const m = MODS.find(x => x[2] === part);
        if (m) mods |= m[0];
        else if (KEY_BY_NAME.has(part)) code = KEY_BY_NAME.get(part).u;
        else if (/^0x[0-9a-f]{1,2}$/.test(part)) code = parseInt(part, 16);
        else bad();
      }
      return flag ? holdTap(m, mods, code) : A(T.KEY, mods, code);   // ":hold": older files
    }
    case "media": { const m = MEDIA.find(x => x[3] === arg); const u = m ? m[0] : parseInt(arg); if (isNaN(u)) bad(); return A(T.MEDIA, u & 255, u >> 8); }
    case "mouse": { const m = MOUSE.find(x => x[2] === arg); if (!m) bad(); return A(T.MOUSE, m[0]); }
    case "scroll": { const [v, h] = arg.split(","); return A(T.SCROLL, int8(v), int8(h)); }
    case "move": { const [x, y] = arg.split(","); return A(T.MOVE, int8(x), int8(y)); }
    case "pad": { const n = PAD.findIndex(x => x && x[1] === arg); if (n < 1) bad(); return A(T.PAD, n); }
    case "axis": {
      const p = arg.split(":"), ax = AXES.findIndex(x => x[0] === p[0]), clicks = p.length > 1 ? +p[1] : 10;
      if (ax < 0 || !Number.isInteger(clicks) || clicks < 1 || clicks > 100) bad();
      return A(T.AXIS, ax | (p.includes("free") ? AXIS_FREE : 0), clicks);
    }
    case "center": return A(T.CENTER);
    case "profile": { const [target, mode] = arg.split(":"); const p = target === "next" ? PROF_NEXT : target === "prev" ? PROF_PREV : (+target - 1); if (p !== PROF_NEXT && p !== PROF_PREV && !(Number.isInteger(p) && p >= 0 && p < 3)) bad(); return A(T.PROFILE, p, mode === "hold" ? 1 : 0); }
    case "light": { const n = LIGHTS.findIndex(x => x[0] === arg); if (n < 0) bad(); return A(T.LIGHT, n); }
  }
  bad();
}

function A(t, p1 = 0, p2 = 0) { return { t, p1, p2 }; }
const sameAction = (a, b) => a.t === b.t && a.p1 === b.p1 && a.p2 === b.p2;
const heldType = t => t === T.KEY || t === T.MEDIA || t === T.MOUSE || t === T.PAD;
// usual choice for a key that starts combos: hold modifiers and gamepad buttons right away
const defaultNow = a => (a.t === T.KEY && a.p1 && !a.p2) || a.t === T.PAD;

