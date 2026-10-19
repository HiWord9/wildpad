// =============================================================================== settings model
// The layout comes from the INFO command. Without a pad (demo), boardInfo() works it out
// the way SET_layout() in the firmware does.
function boardInfo(board, map) {
  map = board ? BOARDS[board].map : map;
  const buttons = map[0] & 15, knob = map[0] >> 4, leds = map[1] & 15, hasKnob = !(map[1] & MAP_NO_KNOB);
  const inputs = buttons + (hasKnob ? 2 : 0), profOff = board ? 13 : 13 + MAP_LEN, profSize = 4 + 3 * inputs;
  let profiles = 3;
  while (profiles > 1 && profOff + profiles * profSize + 24 > 127) profiles--;
  const recOff = profOff + profiles * profSize;
  return [1, 0, 0, board, buttons, knob, 128, profiles, 0, leds, 127 - recOff, profOff, profSize, recOff, SVER, 0, inputs];
}
const DEMO_INFO = boardInfo(1);

class Layout {
  constructor(r) {
    this.fw = `${r[0]}.${r[1]}.${r[2]}`; this.board = r[3]; this.buttons = r[4]; this.knob = r[5];
    this.size = r[6]; this.profiles = r[7]; this.usb = r[8]; this.leds = r[9]; this.recSize = r[10];
    this.profOff = r[11]; this.profSize = r[12]; this.recOff = r[13]; this.version = r[14]; this.current = r[15];
    this.inputs = r[16] ?? this.buttons + 2;
    this.turns = this.inputs > this.buttons;                // has a rotary encoder
    this.right = this.turns ? this.buttons : -1; this.left = this.turns ? this.buttons + 1 : -1;
    this.keys = []; let n = 1;
    for (let i = 0; i < this.buttons; i++) if (i !== this.knob) this.keys.push({ i, n: n++ });
    this.cols = this.keys.length <= 4 ? this.keys.length : Math.ceil(this.keys.length / 2);
  }
  get hasKnob() { return this.knob < this.buttons || this.turns; }
  get boardName() {
    if (BOARDS[this.board]) return BOARDS[this.board].name;
    const k = this.keys.length;
    return `${k} key${k === 1 ? "" : "s"}` + (this.hasKnob ? " and a knob" : "") + " (your own map)";
  }
  name(i) {
    if (i === this.right) return "Turn right";
    if (i === this.left) return "Turn left";
    if (i === this.knob) return "Knob";
    return "Key " + this.keys.find(k => k.i === i).n;
  }
  fileName(i) {
    if (i === this.right) return "right";
    if (i === this.left) return "left";
    if (i === this.knob) return "knob";
    return "key" + this.keys.find(k => k.i === i).n;
  }
  fromFileName(s) {
    for (let i = 0; i < this.inputs; i++) if (this.fileName(i) === s) return i;
    throw new Error(`unknown input "${s}"`);
  }
}

// Profiles in use (header byte, 0 = all) and where the records start after them
const profCount = (L, b) => b[S.PROFILES] >= 1 && b[S.PROFILES] <= L.profiles ? b[S.PROFILES] : L.profiles;
const recordsAt = (L, n) => L.profOff + n * L.profSize;

function parseRecords(L, b) {
  const combos = [], seqs = new Map();
  let i = recordsAt(L, profCount(L, b));
  const end = L.size - 1;
  while (i < end && b[i] !== REC_END) {
    if (b[i] < REC_SEQ) {
      combos.push({ held: b[i] >> 4, trig: b[i] & 15, prof: b[i + 1] >> 5, a: A(b[i + 1] & 31, b[i + 2], b[i + 3]) });
      i += 4;
      continue;
    }
    const sq = { press: [], release: [] }, stop = i + 2 + b[i + 1];
    let list = sq.press;
    for (let j = i + 2; j < stop && j < end;) {
      const op = b[j] & 0xE0;
      if (op === ST_WAIT) {
        const ms = (b[j] & 31) * 20, last = list[list.length - 1];
        if (last && last.op === "wait") last.ms += ms; else list.push({ op: "wait", ms });
        j++;
      }
      else if (op === ST_REL) { list = sq.release; j++; }
      else { list.push({ op: op === ST_HOLD ? "hold" : "tap", a: A(b[j] & 31, b[j + 1], b[j + 2]) }); j += 3; }
    }
    seqs.set(b[i] & 0x7F, sq);
    i = stop;
  }
  return { combos, seqs };
}

function stepBytes(list) {
  const out = [];
  for (const st of list) {
    if (st.op === "wait") {
      for (let n = Math.max(1, Math.round(st.ms / 20)); n > 0; n -= 31) out.push(ST_WAIT | Math.min(31, n));
    }
    else out.push((st.op === "hold" ? ST_HOLD : ST_TAP) | st.a.t, st.a.p1, st.a.p2);
  }
  return out;
}
const seqBytes = sq => [...stepBytes(sq.press), ...(sq.release.length ? [ST_REL, ...stepBytes(sq.release)] : [])];

class MemoryFull extends Error {
  constructor(over) { super(`That doesn't fit: the pad's memory for combos and steps is ${over} byte${over > 1 ? "s" : ""} short. Remove a combo or some steps.`); }
}

class Model {
  constructor(L, bytes) { this.L = L; this.load(bytes); }
  load(bytes) {
    this.b = new Uint8Array(bytes);
    const r = parseRecords(this.L, this.b);
    this.cs = r.combos; this.seqs = r.seqs;
    this.nextSeq = Math.max(-1, ...this.seqs.keys()) + 1;
  }
  clone() { return new Model(this.L, this.bytes()); }
  po(p) { return this.L.profOff + p * this.L.profSize; }
  action(p, i) {
    if (i < 0 || i >= this.L.inputs) return A(T.NONE);
    const o = this.po(p) + 4 + 3 * i; return A(this.b[o] & TYPE_MASK, this.b[o + 1], this.b[o + 2]);
  }
  // now: send a button's own action on press even when it starts combos (undefined = usual choice)
  setAction(p, i, a, now) {
    const o = this.po(p) + 4 + 3 * i, flag = i < this.L.buttons && (now ?? defaultNow(a)) ? NOW : 0;
    this.b[o] = a.t | flag; this.b[o + 1] = a.p1; this.b[o + 2] = a.p2;
  }
  now(p, i) { return i < this.L.buttons && !!(this.b[this.po(p) + 4 + 3 * i] & NOW); }
  setNow(p, i, on) { const o = this.po(p) + 4 + 3 * i; this.b[o] = (this.b[o] & ~NOW) | (on ? NOW : 0); }
  color(p) { const o = this.po(p); return "#" + [0, 1, 2].map(k => this.b[o + k].toString(16).padStart(2, "0")).join(""); }
  setColor(p, hex) { const o = this.po(p); for (let k = 0; k < 3; k++) this.b[o + k] = parseInt(hex.substr(1 + 2 * k, 2), 16); }
  effect(p) { return Math.min(this.b[this.po(p) + 3], EFFECTS.length - 1); }
  setEffect(p, e) { this.b[this.po(p) + 3] = e; }
  // combos: slot = position in the list (the firmware tries them in this order)
  combos() { return this.cs.map((c, slot) => ({ ...c, slot })); }
  setCombo(slot, c) { this.cs[slot] = { held: c.held, trig: c.trig, prof: c.prof, a: c.a }; }
  delCombo(slot) { this.cs.splice(slot, 1); }
  freeSlot() { return this.usedBytes() + 4 <= this.recSize() ? this.cs.length : -1; }
  // profiles in use; the room of the others goes to combos and steps
  get np() { return profCount(this.L, this.b); }
  recOff() { return recordsAt(this.L, this.np); }
  recSize() { return this.L.size - 1 - this.recOff(); }
  setProfileCount(n) {
    const old = this.np;
    if (n < old)                                             // drop combos of / to removed profiles
      this.cs = this.cs.filter(c => (c.prof === ALL || c.prof < n) && !(c.a.t === T.PROFILE && c.a.p1 >= n && c.a.p1 < PROF_NEXT));
    for (let p = old; p < n; p++) {                          // new profiles start empty
      const o = this.po(p);
      this.b.fill(0, o, o + this.L.profSize);
      this.b.set(hexToRgb(["#00aaff", "#ff8c00", "#00e0b0"][p] || "#ffffff"), o);
      this.b[o + 3] = 1;
    }
    this.b[S.PROFILES] = n === this.L.profiles ? 0 : n;
    if (this.b[S.START] >= n) this.b[S.START] = 0;
  }
  // sequences: {press: [step], release: [step]}, step = {op: "tap" | "hold", a} or {op: "wait", ms}
  addSeq(sq) { const id = this.nextSeq++; this.seqs.set(id, sq); return id; }
  seq(id) { return this.seqs.get(id) || { press: [], release: [] }; }
  setSeq(id, sq) { this.seqs.set(id, sq); }
  // Records in the firmware's format. Sequences are numbered in the order they are
  // used; unused ones are dropped. remap collects profile actions that need new ids.
  records(remap) {
    const order = [];
    const ref = a => {
      if (a.t !== T.SEQ) return a;
      if (!this.seqs.has(a.p1)) return A(T.NONE);
      let k = order.indexOf(a.p1);
      if (k < 0) { order.push(a.p1); k = order.length - 1; }
      return A(T.SEQ, k);
    };
    for (let p = 0; p < this.np; p++)
      for (let i = 0; i < this.L.inputs; i++) { const a = this.action(p, i); if (a.t === T.SEQ) { const r = ref(a); if (remap) remap.push([p, i, r]); } }
    const out = [];
    for (const c of this.cs) { const a = ref(c.a); out.push(c.held << 4 | c.trig, c.prof << 5 | a.t, a.p1, a.p2); }
    order.forEach((id, k) => { const body = seqBytes(this.seqs.get(id)); out.push(REC_SEQ | k, body.length, ...body); });
    return out;
  }
  usedBytes() { return this.records().length; }
  // a combo of profile p wins over an all-profiles combo with the same controls (as in the firmware)
  findCombo(held, trig, p) {
    const same = this.combos().filter(c => c.held === held && c.trig === trig);
    return same.find(c => c.prof === p) || same.find(c => c.prof === ALL);
  }
  startsCombos(h, p) { return this.combos().some(c => c.held === h && c.trig !== h && (c.prof === p || c.prof === ALL)); }
  usesGamepad() {
    const pad = a => a && (a.t === T.PAD || a.t === T.AXIS);
    for (let p = 0; p < this.np; p++) for (let i = 0; i < this.L.inputs; i++) if (pad(this.action(p, i))) return true;
    if (this.cs.some(c => pad(c.a))) return true;
    for (const sq of this.seqs.values()) if ([...sq.press, ...sq.release].some(st => pad(st.a))) return true;
    return false;
  }
  bytes() {
    const remap = [], rec = this.records(remap);
    if (rec.length > this.recSize()) throw new MemoryFull(rec.length - this.recSize());
    const b = new Uint8Array(this.b);
    for (const [p, i, a] of remap) { const o = this.po(p) + 4 + 3 * i; b[o] = (b[o] & NOW) | a.t; b[o + 1] = a.p1; b[o + 2] = a.p2; }
    b.fill(REC_END, this.recOff(), this.L.size);
    b.set(rec, this.recOff());
    b[0] = 0xA5; b[1] = SVER;
    let x = 0; for (let i = 0; i < b.length - 1; i++) x = (x + b[i]) & 255; b[b.length - 1] = x; return b;
  }
  toJSON() {
    const L = this.L, b = this.b, f = b[S.FLAGS];
    const names = Array.from({ length: L.inputs }, (_, i) => L.fileName(i));
    return {
      format: "wildpad-settings", version: SVER,
      board: L.board ? { id: L.board, inputs: names } : { id: 0, inputs: names, map: Array.from(b.subarray(S.MAP, S.MAP + MAP_LEN)) },
      options: { brightness: b[S.BRIGHT], holdMs: b[S.HOLD] * 10, debounceMs: b[S.DEBOUNCE], tapMs: b[S.TAP],
        clicksPerTurn: b[S.DETENTS], profiles: this.np, startProfile: b[S.START] + 1, lightSpeed: b[S.SPEED], turnHoldMs: (b[S.TURN_HOLD] || 8) * 100,
        reverseKnob: !!(f & FLAG.INVERT), halfStep: !!(f & FLAG.HALF) },
      profiles: Array.from({ length: this.np }, (_, p) => ({ color: this.color(p), light: EFFECTS[this.effect(p)][0],
        actions: Object.fromEntries(names.map((n, i) => [n, actionToText(this.action(p, i), this)])),
        rightAway: names.filter((n, i) => this.now(p, i)) })),
      combos: this.combos().map(c => ({ hold: L.fileName(c.held), then: L.fileName(c.trig),
        profile: c.prof === ALL ? "all" : c.prof + 1, action: actionToText(c.a, this) })),
    };
  }
  fromJSON(j) {
    if (j.format !== "wildpad-settings" && j.format !== "detent-settings") throw new Error("this is not a Wildpad settings file");
    const L = this.L, b = this.b, o = j.options || {}, clamp = (v, lo, hi, d) => Math.max(lo, Math.min(hi, Number.isFinite(+v) ? +v : d));
    const fb = j.board?.id;
    if (fb !== undefined && (fb !== L.board || (fb === 0 && String(j.board.map) !== String(Array.from(b.subarray(S.MAP, S.MAP + MAP_LEN))))))
      throw new Error(`it is for another board (${BOARDS[fb]?.name || "a board of its own"}). Choose that board under Settings first`);
    b[S.BRIGHT] = clamp(o.brightness, 0, 255, b[S.BRIGHT]);
    b[S.HOLD] = clamp(Math.round((o.holdMs ?? b[S.HOLD] * 10) / 10), 10, 100, 30);
    b[S.DEBOUNCE] = clamp(o.debounceMs, 1, 30, b[S.DEBOUNCE]);
    b[S.TAP] = clamp(o.tapMs, 1, 50, b[S.TAP]);
    b[S.DETENTS] = clamp(o.clicksPerTurn, 4, 100, b[S.DETENTS]);
    this.setProfileCount(clamp(o.profiles ?? (j.profiles || []).length ?? L.profiles, 1, L.profiles, L.profiles));
    b[S.START] = clamp((o.startProfile ?? 1) - 1, 0, this.np - 1, 0);
    b[S.SPEED] = clamp(o.lightSpeed, 1, 10, b[S.SPEED]);
    b[S.TURN_HOLD] = clamp(Math.round((o.turnHoldMs ?? (b[S.TURN_HOLD] || 8) * 100) / 100), 2, 30, 8);
    b[S.FLAGS] = (o.reverseKnob ? FLAG.INVERT : 0) | (o.halfStep ? FLAG.HALF : 0);
    (j.profiles || []).slice(0, this.np).forEach((pr, p) => {
      if (/^#[0-9a-f]{6}$/i.test(pr.color || "")) this.setColor(p, pr.color.toLowerCase());
      const e = EFFECTS.findIndex(x => x[0] === pr.light); if (e >= 0) this.setEffect(p, e);
      for (const [name, text] of Object.entries(pr.actions || {})) {
        let i; try { i = L.fromFileName(name); } catch { continue; }
        this.setAction(p, i, textToAction(text, this), Array.isArray(pr.rightAway) ? pr.rightAway.includes(name) : undefined);
      }
    });
    this.cs = [];
    for (const c of j.combos || []) {
      let held, trig; try { held = L.fromFileName(c.hold); trig = L.fromFileName(c.then); } catch { continue; }
      if (held >= L.buttons) continue;
      if (c.profile !== "all" && (+c.profile < 1 || +c.profile > this.np)) continue;
      this.cs.push({ held, trig, prof: c.profile === "all" ? ALL : +c.profile - 1, a: textToAction(c.action, this) });
    }
    this.bytes();                                           // throws if it doesn't fit
  }
}

// Factory settings, same bytes as SET_defaults() in firmware/src/settings.c. A board of
// its own keeps its pin map from `from`.
function factory(L, from) {
  const b = new Uint8Array(L.size);
  b.fill(REC_END, recordsAt(L, L.profiles));
  b.set([0xA5, SVER, 0, 128, 30, 5, 4, 20, 0, 5, 8], 0);
  b[S.BOARD] = L.board;
  if (!L.board && from) b.set(from.b.subarray(S.MAP, S.MAP + MAP_LEN), S.MAP);
  const m = new Model(L, b);
  const K = L.knob;
  let slot = 0;
  if (K < L.buttons) {
    L.keys.slice(0, 3).forEach((k, n) => { if (n < L.profiles) m.setCombo(slot++, { held: K, trig: k.i, prof: ALL, a: A(T.PROFILE, n, 0) }); });
    if (L.turns) {
      m.setCombo(slot++, { held: K, trig: L.right, prof: ALL, a: A(T.LIGHT, 1) });
      m.setCombo(slot++, { held: K, trig: L.left, prof: ALL, a: A(T.LIGHT, 2) });
    }
  }
  ["media", "editing", "windows"].slice(0, L.profiles).forEach((w, p) => applyPreset(m, p, w));
  return m;
}

const PRESETS = [
  ["media", "Media controls"], ["editing", "Copy, paste and scroll"], ["windows", "Switch windows by turning"],
  ["windows-press", "Switch windows: press and turn"],
  ["mouse", "Mouse buttons"], ["gamepad", "Gamepad with knob axis"], ["empty", "Empty"],
];
function applyPreset(m, p, which) {
  const L = m.L, k = L.keys.map(x => x.i), set = (i, a) => { if (i !== undefined && i >= 0 && i < L.inputs) m.setAction(p, i, a); };
  const colors = { media: "#00aaff", editing: "#ff8c00", windows: "#00e0b0", "windows-press": "#00e0b0", mouse: "#dc00ff", gamepad: "#36d03a", empty: m.color(p) };
  for (let i = 0; i < L.inputs; i++) m.setAction(p, i, A(T.NONE));
  m.cs = m.cs.filter(c => c.prof !== p);                    // a preset replaces the whole profile
  const combo = (held, trig, a) => { const slot = m.freeSlot(); if (slot >= 0) m.setCombo(slot, { held, trig, prof: p, a }); };
  m.setColor(p, colors[which]); m.setEffect(p, 1);
  if (which === "media") {
    set(k[0], A(T.MEDIA, 0xB6)); set(k[1], A(T.MEDIA, 0xCD)); set(k[2], A(T.MEDIA, 0xB5));
    set(k[3], A(T.MEDIA, 0xEA)); set(k[4], A(T.MEDIA, 0xE2)); set(k[5], A(T.MEDIA, 0xE9));
    set(L.knob, A(T.MEDIA, 0xE2)); set(L.right, A(T.MEDIA, 0xE9)); set(L.left, A(T.MEDIA, 0xEA));
  } else if (which === "editing") {
    set(k[0], A(T.KEY, 1, 0x06)); set(k[1], A(T.KEY, 1, 0x19)); set(k[2], A(T.KEY, 1, 0x1D));
    set(k[3], A(T.KEY, 1, 0x1B)); set(k[4], A(T.KEY, 1, 0x04)); set(k[5], A(T.KEY, 1, 0x1C));
    set(L.knob, A(T.KEY, 0, 0x28)); set(L.right, A(T.SCROLL, 0xFF, 0)); set(L.left, A(T.SCROLL, 1, 0));
  } else if (which === "windows" || which === "windows-press") {
    set(k[0], A(T.KEY, 8, 0x07)); set(k[1], A(T.KEY, 8, 0x08)); set(k[2], A(T.KEY, 8 | 2, 0x16));
    if (which === "windows") {
      // turn: Alt+Tab with Alt held until the knob stops; press: task view
      set(L.knob, A(T.KEY, 8, 0x2B));
      if (L.turns) { set(L.right, holdTap(m, 4, 0x2B)); set(L.left, holdTap(m, 4 | 2, 0x2B)); }
    } else {
      // press: Alt+Tab with Alt held (the switcher opens at once); hold + turn: next / previous window
      if (L.knob < L.buttons) m.setAction(p, L.knob, holdTap(m, 4, 0x2B), true);
      set(L.right, A(T.KEY, 1, 0x2B)); set(L.left, A(T.KEY, 1 | 2, 0x2B));
      if (L.turns && L.knob < L.buttons) { combo(L.knob, L.right, A(T.KEY, 0, 0x2B)); combo(L.knob, L.left, A(T.KEY, 2, 0x2B)); }
    }
  } else if (which === "mouse") {
    set(k[0], A(T.MOUSE, 1)); set(k[1], A(T.MOUSE, 4)); set(k[2], A(T.MOUSE, 2));
    set(L.knob, A(T.MOUSE, 8)); set(L.right, A(T.SCROLL, 0, 1)); set(L.left, A(T.SCROLL, 0, 0xFF));
  } else if (which === "gamepad") {
    set(k[0], A(T.PAD, 1)); set(k[1], A(T.PAD, 2)); set(k[2], A(T.PAD, 3));
    set(L.knob, A(T.CENTER)); set(L.right, A(T.AXIS, 0, 10)); m.setEffect(p, 5);
  }
}

