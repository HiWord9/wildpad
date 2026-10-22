// =============================================================================== editing
function change(fn, key) {
  const before = model.bytes(), now = Date.now();
  fn(model);
  let after;
  try { after = model.bytes(); }
  catch (e) { model.load(before); toast(e.message, true); render(); return; }
  model.load(after);                                      // renumbers sequences, drops unused ones
  if (key !== lastUndoKey || now - lastUndoTime > 1500) {
    undoStack.push(before); if (undoStack.length > 100) undoStack.shift();
    redoStack = [];
  }
  lastUndoKey = key; lastUndoTime = now;
  schedulePush();
  render();
}
function undo() { if (!undoStack.length) return; redoStack.push(model.bytes()); model.load(undoStack.pop()); lastUndoKey = null; schedulePush(); render(); }
function redo() { if (!redoStack.length) return; undoStack.push(model.bytes()); model.load(redoStack.pop()); lastUndoKey = null; schedulePush(); render(); }

function selectProfile(p) {
  prof = p;
  if (dev) command(CMD.PROFILE, p).catch(fail);
  render();
}

// =============================================================================== rendering helpers
function h(tag, attrs = {}, ...kids) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v === undefined || v === null || v === false) continue;
    if (k === "class") el.className = v;
    else if (k === "style") el.style.cssText = v;
    else if (k.startsWith("on")) el.addEventListener(k.slice(2), v);
    else if (k === "text") el.textContent = v;
    else el.setAttribute(k, v === true ? "" : v);
  }
  for (const kid of kids.flat()) if (kid !== null && kid !== undefined && kid !== false) el.append(kid);
  return el;
}
const ICON_CHEVRON = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="m6 9 6 6 6-6"/></svg>';
const ICON_TRASH = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3"/></svg>';
function svg(markup) { const t = document.createElement("template"); t.innerHTML = markup; return t.content.firstChild; }

function hexToRgb(hex) { return [1, 3, 5].map(k => parseInt(hex.substr(k, 2), 16)); }
function setAccent() {
  const c = model.color(prof), [r, g, b] = hexToRgb(c);
  const lum = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255;
  const root = document.documentElement.style;
  // very light colors (white, yellow) need a darker accent to stay visible on the page
  root.setProperty("--accent", lum > 0.8 ? `rgb(${r * .55 | 0}, ${g * .55 | 0}, ${b * .55 | 0})` : c);
  root.setProperty("--accent-soft", `rgba(${r}, ${g}, ${b}, .16)`);
}

function initTheme() {
  try {
    const saved = localStorage.getItem("wildpad-theme");
    if (saved) document.documentElement.setAttribute("data-theme", saved);
  } catch {}
  updateThemeIcon();
}
function updateThemeIcon() {
  const isDark = document.documentElement.getAttribute("data-theme") === "dark" ||
    (!document.documentElement.getAttribute("data-theme") && window.matchMedia("(prefers-color-scheme: dark)").matches);
  const icon = $("themeIcon");
  if (!icon) return;
  if (isDark) {
    icon.innerHTML = '<circle cx="12" cy="12" r="5"/><path d="M12 1v2M12 21v2M4.22 4.22l1.42 1.42M18.36 18.36l1.42 1.42M1 12h2M21 12h2M4.22 19.78l1.42-1.42M18.36 5.64l1.42-1.42"/>';
  } else {
    icon.innerHTML = '<path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"/>';
  }
}
function toggleTheme() {
  const current = document.documentElement.getAttribute("data-theme");
  const isDark = current === "dark" || (!current && window.matchMedia("(prefers-color-scheme: dark)").matches);
  const next = isDark ? "light" : "dark";
  document.documentElement.setAttribute("data-theme", next);
  try { localStorage.setItem("wildpad-theme", next); } catch {}
  updateThemeIcon();
  setAccent();
}

// =============================================================================== render
function render() {
  setAccent();
  renderBanner();
  renderProfiles();
  $("undo").disabled = !undoStack.length; $("redo").disabled = !redoStack.length;
  $("connect").classList.add("hidden");                // the banner carries the Connect button
  $("disconnectBtn").classList.toggle("hidden", !dev);
  if (tab === "controls") { renderDevice(); renderCombos(); renderInspector(); }
  if (tab === "lighting") { renderDevice("l-"); renderLighting(); }
  if (tab === "settings") renderSettings();
  renderLive();
}

function renderBanner() {
  const b = $("banner"), t = $("bannerText"), btn = $("bannerBtn");
  if (dev) { b.classList.add("hidden"); return; }
  b.classList.remove("hidden");
  if (!("hid" in navigator)) {
    t.textContent = location.protocol === "file:"
      ? "The page was opened as a file. WebHID needs HTTPS or localhost: run configure.bat, or open the page from the project's website. You can still look around: this is a demo."
      : "This browser can't talk to USB devices. Open this page in Chrome, Edge or Opera on a computer. You can still look around: this is a demo.";
    btn.classList.add("hidden");
  } else if (reconnecting) {
    t.textContent = "The pad is reconnecting…"; btn.classList.add("hidden");
  } else {
    t.textContent = "You're looking at a demo. Plug in your macropad and connect to change it; every change is saved on the pad right away.";
    btn.classList.remove("hidden");
  }
}

function renderProfiles() {
  const box = $("profiles"); box.replaceChildren();
  box.append(h("span", { class: "profiles-label", text: "Profile" }));
  for (let p = 0; p < model.np; p++) {
    box.append(h("button", { class: "prof", "aria-pressed": p === prof ? "true" : "false", onclick: () => selectProfile(p) },
      h("span", { class: "sw", style: `background:${model.color(p)}` }), `Profile ${p + 1}`,
      dev && live.prof === p ? h("span", { class: "live", text: "active" }) : null));
  }
  const presetSel = h("select", { class: "plain", "aria-label": "Fill this profile with a preset", onchange: e => {
      const v = e.target.value; e.target.value = "";
      if (v) change(m => applyPreset(m, prof, v), "preset");
      toast(`Profile ${prof + 1}: ${PRESETS.find(x => x[0] === v)[1].toLowerCase()}`);
    } },
    h("option", { value: "", text: "Fill with a preset…" }), PRESETS.map(([k, n]) => h("option", { value: k, text: n })));
  const copySel = h("select", { class: "plain", "aria-label": "Copy another profile into this one", onchange: e => {
      const v = +e.target.value; e.target.value = "";
      change(m => { for (let i = 0; i < L.inputs; i++) m.setAction(prof, i, m.action(v, i), m.now(v, i)); m.setEffect(prof, m.effect(v)); }, "copy");
      toast(`Copied profile ${v + 1} into profile ${prof + 1}`);
    } },
    h("option", { value: "", text: "Copy from…" }),
    Array.from({ length: model.np }, (_, p) => p).filter(p => p !== prof).map(p => h("option", { value: p, text: `Profile ${p + 1}` })));
  box.append(h("div", { class: "prof-tools" }, tab === "settings" ? null : [copySel, presetSel]));
}

// ---------------------------------------------------------------- device drawing
function renderDevice(px = "") {
  const d = $(px + "device"); d.replaceChildren();
  const keys = h("div", { class: "keys", style: `grid-template-columns: repeat(${L.cols}, auto)` });
  for (const k of L.keys) {
    const a = model.action(prof, k.i), long = model.findCombo(k.i, k.i, prof);
    const selected = !px && sel.kind === "input" && sel.i === k.i;
    keys.append(h("div", { class: "cap-wrap", "data-selected": selected ? "true" : "false" },
      h("div", { class: "glow", id: px + "glow" + k.i }),
      h("span", { class: "sel-ring" }),
      h("button", { class: "cap", id: px + "cap" + k.i, tabindex: px ? -1 : null, "aria-label": `Key ${k.n}: ${describe(a)}`, onclick: () => select({ kind: "input", i: k.i }) },
        h("span", { class: "legend" + (a.t === T.NONE ? " none" : ""), text: describe(a, true) }),
        long ? h("span", { class: "sub", text: "hold: " + describe(long.a, true) }) : null),
      h("span", { class: "ctl-name", text: `Key ${k.n}` })));
  }
  d.append(keys);
  if (L.hasKnob) {
    const axis = model.action(prof, L.right).t === T.AXIS;
    const selected = !px && sel.kind === "input" && (sel.i === L.knob || sel.i === L.right || sel.i === L.left);
    const legend = h("div", { class: "knob-legend" },
      !L.turns ? null
        : axis ? [h("b", { text: "Turn" }), h("span", { text: describe(model.action(prof, L.right), true) })]
        : [h("b", { text: "Right" }), h("span", { text: describe(model.action(prof, L.right), true) }),
           h("b", { text: "Left" }), h("span", { text: describe(model.action(prof, L.left), true) })],
      L.knob < L.buttons ? [h("b", { text: "Press" }), h("span", { text: describe(model.action(prof, L.knob), true) })] : null);
    d.append(h("div", { class: "knob-wrap", "data-selected": selected ? "true" : "false" },
      h("div", { class: "knob-holder" },                      // the ring follows the knob, not the legend
        h("span", { class: "sel-ring" }),
        h("button", { class: "knob", id: px + "knob", tabindex: px ? -1 : null, "aria-label": "Knob", onclick: () => select({ kind: "input", i: L.knob < L.buttons ? L.knob : L.right }) },
          h("span", { class: "knob-top" }))),
      legend));
  }
}

function select(s) {
  sel = s; openPicker = null;
  render();
  if (window.innerWidth <= 980) $("inspector").scrollIntoView({ behavior: "smooth", block: "start" });
}

// ---------------------------------------------------------------- live state
let ledTimer = null;
function renderLive() {
  if (tab === "settings") return;
  const px = tab === "lighting" ? "l-" : "";
  for (const k of L.keys) $(px + "cap" + k.i)?.classList.toggle("down", !!(live.btn & 1 << k.i));
  $(px + "knob")?.classList.toggle("down", !!(live.btn & 1 << L.knob));
  const det = Math.max(4, model.b[S.DETENTS]);
  // no wrapping to 0..360: from 342 to 360 degrees must not animate back the long way
  $(px + "knob")?.style.setProperty("--angle", `${live.pos * 360 / det}deg`);
  if (tab === "controls") {
    const tag = $("pressedTag");
    if (tag && sel.kind === "input") tag.classList.toggle("on", sel.i < L.buttons && !!(live.btn & 1 << sel.i));
    const ax = model.action(prof, L.right);
    const meter = $("axisMeter");
    meter.classList.toggle("hidden", ax.t !== T.AXIS);
    if (ax.t === T.AXIS) {
      const range = Math.max(1, ax.p2), v = dev ? Math.max(-range, Math.min(range, live.pos)) : 0, f = v / range;
      const fill = $("axisFill");
      fill.style.left = f < 0 ? `${50 + f * 50}%` : "50%"; fill.style.width = `${Math.abs(f) * 50}%`;
      $("axisText").textContent = v === 0 ? "Center" : `${v > 0 ? "Right" : "Left"} ${Math.round(Math.abs(f) * 100)} %`;
    }
  }
  paintLeds();
}

// Approximate the light effect under the keycaps
const pressGlow = {};
function paintLeds() {
  const e = model.effect(prof), c = model.color(prof), bright = model.b[S.BRIGHT] / 255;
  const t = performance.now() / 1000, speed = model.b[S.SPEED];
  const ax = model.action(prof, L.right);
  L.keys.forEach(k => {
    const g = $((tab === "lighting" ? "l-" : "") + "glow" + k.i); if (!g) return;
    const n = k.i, side = 2 * n + 1 - L.leds;              // LED n is under button n; <0 left half
    let color = c, a = 0;
    if (n >= L.leds) a = 0;
    else if (e === 1) a = 1;
    else if (e === 2) a = 0.15 + 0.85 * (0.5 - 0.5 * Math.cos(t * speed * 0.6));
    else if (e === 3) { color = `hsl(${(t * speed * 9 + n * 60) % 360} 100% 50%)`; a = 1; }
    else if (e === 4) { if (live.btn & 1 << k.i) pressGlow[k.i] = 1; a = pressGlow[k.i] || 0; pressGlow[k.i] = Math.max(0, a - 0.04); }
    else if (e === 5) {
      if (ax.t === T.AXIS && L.leds >= 3) {
        const range = Math.max(1, ax.p2), v = dev ? Math.max(-range, Math.min(range, live.pos)) : 0;
        a = side === 0 ? (v === 0 ? 1 : 0.16) : side < 0 ? (v < 0 ? -v / range : 0) : (v > 0 ? v / range : 0);
      } else a = 1;
    }
    g.style.setProperty("--led", color);
    g.style.setProperty("--led-a", (a * (0.25 + 0.75 * bright)).toFixed(3));
  });
  const animated = tab !== "settings" && (e === 2 || e === 3 || e === 4);
  if (animated && !ledTimer) ledTimer = setInterval(paintLeds, 50);
  if (!animated && ledTimer) { clearInterval(ledTimer); ledTimer = null; }
}

// ---------------------------------------------------------------- inspector
function renderInspector() {
  const box = $("inspector"); box.replaceChildren();
  if (sel.kind === "combo") return renderComboInspector(box);
  const i = sel.i;
  if (i === L.knob || i === L.right || i === L.left) return renderKnobInspector(box);
  const k = L.keys.find(x => x.i === i);
  const body = h("div", { class: "panel-b" });
  box.append(h("div", { class: "panel-h insp-title" }, h("h2", { text: `Key ${k.n}` }),
    h("span", { class: "pressed-tag", id: "pressedTag", text: "pressed" })), body);

  body.append(field("When pressed", null, actionEditor(`press${i}`, model.action(prof, i), a => change(m => m.setAction(prof, i, a), `press${i}`), { input: i })));
  const timing = pressTiming(i, `Key ${k.n}`);
  if (timing) body.lastChild.append(timing);

  const long = model.findCombo(i, i, prof);
  const holdMs = model.b[S.HOLD] * 10;
  const longOff = model.now(prof, i) && isCombiner(i);
  body.append(field("When held", `${holdMs / 1000} s or longer`, longOff
    ? h("p", { class: "note", style: "margin:0", text: long ? `${describe(long.a)}, but not while the action above is sent right away.` : "Not available while the action above is sent right away." })
    : actionEditor(`long${i}`, long ? long.a : A(T.NONE), a => change(m => setLongPress(m, i, a, long), `long${i}`), { input: i, allowNone: "Same as pressed" })));

  const mine = model.combos().filter(c => c.held === i && c.trig !== i && (c.prof === prof || c.prof === ALL));
  const list = h("div", { class: "combo-list" }, mine.map(c => comboRow(c, true)));
  body.append(field(`While key ${k.n} is held`, null, mine.length ? list : h("p", { class: "note", style: "margin:0", text: "No combos yet." }),
    h("div", { class: "row", style: "margin-top:8px" }, h("button", { class: "btn", onclick: () => addCombo(i) }, "Add combo with this key"))));
}

// Does button i start combos or have a long press (so its own action has to wait)?
const isCombiner = i => model.startsCombos(i, prof) || !!model.findCombo(i, i, prof);

// "Right away" / "When released" choice for a button that starts combos
function pressTiming(i, name) {
  if (!isCombiner(i)) return null;
  const now = model.now(prof, i), a = model.action(prof, i);
  if (a.t === T.NONE) return null;
  const seg = h("div", { class: "seg two", style: "margin-top:10px" },
    h("button", { "aria-pressed": now ? "true" : "false", onclick: () => change(m => m.setNow(prof, i, true), `now${i}`) }, "Right away"),
    h("button", { "aria-pressed": now ? "false" : "true", onclick: () => change(m => m.setNow(prof, i, false), `now${i}`) }, "When released"));
  const text = now
    ? `${name} sends this on press and holds it while you hold ${name === "The knob" ? "it" : "the key"}; combos work on top of it. Right for Alt, Ctrl, Shift and gamepad buttons, e.g. Alt here with Tab on a combo switches windows.`
    : `${name} starts combos, so this is sent when you let go, and only if you didn't use a combo.`;
  return h("div", {}, seg, h("p", { class: "note", text }));
}

function setLongPress(m, i, a, existing) {
  if (a.t === T.NONE) { if (existing) m.delCombo(existing.slot); return; }
  if (existing && existing.prof === prof) { m.setCombo(existing.slot, { ...existing, a }); return; }
  const slot = m.freeSlot();
  if (slot < 0) { toast("The pad's memory for combos and steps is full. Remove a combo or some steps first.", true); return; }
  m.setCombo(slot, { held: i, trig: i, prof, a });
}

function renderKnobInspector(box) {
  const body = h("div", { class: "panel-b" });
  box.append(h("div", { class: "panel-h insp-title" }, h("h2", { text: "Knob" }),
    h("span", { class: "pressed-tag", id: "pressedTag", text: "pressed" })), body);
  if (L.turns) {
    const r = model.action(prof, L.right), axis = r.t === T.AXIS;

    const mode = h("div", { class: "seg two" },
      h("button", { "aria-pressed": !axis ? "true" : "false", onclick: () => { if (axis) change(m => { m.setAction(prof, L.right, A(T.MEDIA, 0xE9)); m.setAction(prof, L.left, A(T.MEDIA, 0xEA)); }, "kmode"); } }, "An action per click"),
      h("button", { "aria-pressed": axis ? "true" : "false", onclick: () => { if (!axis) change(m => { m.setAction(prof, L.right, A(T.AXIS, 0, 10)); m.setAction(prof, L.left, A(T.NONE)); }, "kmode"); } }, "Gamepad axis"));
    body.append(field("Turning sends", null, mode));

    if (!axis) {
      body.append(field("Turn right", "each click", actionEditor("right", r, a => change(m => m.setAction(prof, L.right, a), "right"), { input: L.right, turn: true })));
      body.append(field("Turn left", "each click", actionEditor("left", model.action(prof, L.left), a => change(m => m.setAction(prof, L.left, a), "left"), { input: L.left, turn: true })));
    } else {
      const range = r.p2, det = Math.max(4, model.b[S.DETENTS]);
      const axSel = h("select", { class: "plain", style: "width:100%", onchange: e => change(m => m.setAction(prof, L.right, A(T.AXIS, (+e.target.value) | (r.p1 & AXIS_FREE), range)), "axis") },
        AXES.map(([k, n], idx) => h("option", { value: idx, selected: (r.p1 & 15) === idx, text: n })));
      body.append(field("Axis", null, axSel));
      const val = h("span", { class: "val", text: range });
      const slider = h("input", { type: "range", min: 1, max: 50, value: range, oninput: e => {   // applied on release
          val.textContent = e.target.value;
          deg.textContent = `${e.target.value} clicks = ${Math.round(e.target.value * 360 / det)}° of turning`;
        }, onchange: e => change(m => m.setAction(prof, L.right, A(T.AXIS, r.p1, +e.target.value)), "range") });
      const deg = h("p", { class: "note", text: `${range} clicks = ${Math.round(range * 360 / det)}° of turning` });
      body.append(field("Clicks from center to full", null, h("div", { class: "slider-row" }, slider, val), deg));
      const stop = h("label", { class: "switch" }, h("input", { type: "checkbox", checked: !(r.p1 & AXIS_FREE),
        onchange: e => change(m => m.setAction(prof, L.right, A(T.AXIS, (r.p1 & 15) | (e.target.checked ? 0 : AXIS_FREE), range)), "stop") }), h("span"));
      body.append(field("Hard stop", null, h("div", { class: "row" }, h("span", { class: "muted small", style: "flex:1", text: "Turning past full doesn't wind up, so turning back responds at once." }), stop)));
      body.append(h("div", { class: "row", style: "margin:4px 0 6px" },
        h("button", { class: "btn", disabled: !dev, onclick: () => command(CMD.RECENTER).catch(fail) }, "Center now"),
        h("span", { class: "muted small", text: "Tip: set the knob press to “Center the knob axis”." })));
    }
  }
  if (L.knob >= L.buttons) return;                       // the knob doesn't press
  body.append(field("When pressed", null, actionEditor(`press${L.knob}`, model.action(prof, L.knob), a => change(m => m.setAction(prof, L.knob, a), `press${L.knob}`), { input: L.knob })));
  const timing = pressTiming(L.knob, "The knob");
  if (timing) body.lastChild.append(timing);

  const mine = model.combos().filter(c => c.held === L.knob && c.trig !== L.knob && (c.prof === prof || c.prof === ALL));
  body.append(field("While the knob is held", null,
    mine.length ? h("div", { class: "combo-list" }, mine.map(c => comboRow(c, true))) : h("p", { class: "note", style: "margin:0", text: "No combos yet." }),
    h("div", { class: "row", style: "margin-top:8px" }, h("button", { class: "btn", onclick: () => addCombo(L.knob) }, "Add combo with the knob"))));
}

function field(title, hint, ...content) {
  return h("div", { class: "field" }, h("div", { class: "field-h" }, h("h3", { text: title }), hint ? h("span", { class: "hint", text: hint }) : null), ...content);
}

// ---------------------------------------------------------------- action editor
const CATS = [["key", "Keyboard"], ["media", "Media"], ["mouse", "Mouse"], ["pad", "Gamepad"], ["device", "Profile, lights"], ["steps", "Several steps"], ["none", "Nothing"]];
const catOf = a => ({ [T.KEY]: "key", [T.SEQ]: "steps", [T.MEDIA]: "media", [T.MOUSE]: "mouse", [T.SCROLL]: "mouse", [T.MOVE]: "mouse", [T.PAD]: "pad",
  [T.CENTER]: "device", [T.PROFILE]: "device", [T.LIGHT]: "device", [T.AXIS]: "pad" })[a.t] || "none";
const pickerCat = {};

const pickerSetters = new Map(), pickerActions = new Map();   // filled while rendering, used by key recording

function actionEditor(id, a, onSet, opts = {}) {
  const expanded = openPicker === id || (openPicker || "").startsWith(id + "/");
  pickerSetters.set(id, onSet); pickerActions.set(id, a);
  const none = a.t === T.NONE || (a.t === T.KEY && !a.p1 && !a.p2);
  const btn = h("button", { class: "act-btn", "aria-expanded": expanded ? "true" : "false",
      onclick: () => { openPicker = expanded ? (id.includes("/") ? id.slice(0, id.lastIndexOf("/")) : null) : id; recording = false; render(); } },
    h("span", { class: "v" + (none ? " none" : ""), text: none && opts.allowNone ? opts.allowNone : describe(a) }), svg(ICON_CHEVRON));
  const wrap = h("div", {}, btn);
  if (!expanded) return wrap;

  const cats = CATS.filter(([k]) => k !== "steps" || !opts.inStep);
  const cat = pickerCat[id] || (catOf(a) === "none" ? "key" : catOf(a));
  const seg = h("div", { class: "seg cats", role: "tablist" }, cats.map(([k, n]) => h("button", { "aria-pressed": k === cat ? "true" : "false",
    onclick: () => {
      pickerCat[id] = k;
      if (k === "none") onSet(A(T.NONE));
      else if (k === "steps" && a.t !== T.SEQ)                 // start with the current action as step 1
        onSet(A(T.SEQ, model.addSeq({ press: a.t === T.NONE ? [] : [{ op: "tap", a }], release: [] })));
      else render();
    } }, n)));
  const body = h("div", { class: "pick-body" });
  const set = x => { pickerCat[id] = catOf(x) === "none" ? cat : catOf(x); onSet(x); };

  if (cat === "key") body.append(keyEditor(a, set, opts));
  else if (cat === "media") body.append(h("div", { class: "grid" }, MEDIA.map(m => h("button", { class: "opt",
    "aria-pressed": a.t === T.MEDIA && (a.p1 | a.p2 << 8) === m[0] ? "true" : "false", onclick: () => set(A(T.MEDIA, m[0] & 255, m[0] >> 8)) }, m[1]))));
  else if (cat === "mouse") body.append(mouseEditor(a, set, opts));
  else if (cat === "pad") body.append(
    h("div", { class: "sub-h", text: "Buttons" }),
    h("div", { class: "grid narrow" }, PAD.map((p, n) => p && h("button", { class: "opt", title: p[0],
      "aria-pressed": a.t === T.PAD && a.p1 === n ? "true" : "false", onclick: () => set(A(T.PAD, n)) }, PAD_SHORT[n] || p[0]))),
    h("p", { class: "note", text: "The pad adds a gamepad (Xbox controller) to your computer as soon as any action uses it, and reconnects once to do so." }));
  else if (cat === "device") body.append(deviceEditor(a, set));
  else if (cat === "steps") body.append(a.t === T.SEQ ? stepsEditor(id, a, opts) : h("p", { class: "note", style: "margin:0", text: "Preparing…" }));
  else body.append(h("p", { class: "note", style: "margin:0", text: opts.allowNone ? "Holding does the same as pressing." : "This control does nothing." }));

  wrap.append(h("div", { class: "picker" }, seg, body));
  return wrap;
}

let recording = false;
function keyEditor(a, set, opts = {}) {
  const isKey = a.t === T.KEY, mods = isKey ? a.p1 : 0, code = isKey ? a.p2 : 0, t = T.KEY;
  const rec = h("button", { class: "rec" + (recording ? " on" : ""), onclick: () => { recording = !recording; render(); } },
    recording ? "Press the keys now…" : (isKey && (mods || code) ? keyText(mods, code) : "Click, then press a shortcut"),
    h("small", { text: recording ? "Esc on its own cancels. Some shortcuts (like Win+L) are kept by the system: use the lists below." : "Records keys exactly as you press them." }));
  const modRow = h("div", { class: "mods" }, MODS.slice(0, 4).map(m => h("button", { class: "chip", "aria-pressed": mods & m[0] ? "true" : "false",
    onclick: () => set(A(t, mods ^ m[0], code)) }, m[1])));
  const groups = {};
  for (const k of KEYS.values()) (groups[k.group] ||= []).push(k);
  const keySel = h("select", { class: "plain", onchange: e => set(A(t, mods, +e.target.value)) },
    h("option", { value: 0, text: "No key (modifiers only)", selected: !code }),
    Object.entries(groups).map(([g, ks]) => h("optgroup", { label: g }, ks.map(k => h("option", { value: k.u, selected: k.u === code, text: k.label })))));
  const out = h("div", {}, rec, h("div", { class: "sub-h", text: "Or pick" }), modRow, h("div", { class: "row", style: "margin-top:8px" }, keySel));
  if (isKey && code && (mods & HOLD_MODS) && !opts.inStep) {   // app switchers: offer the two steps
    const held = keyText(mods & HOLD_MODS, 0);
    const when = opts.turn ? `until the knob stops for ${(model.b[S.TURN_HOLD] || 8) / 10} s` : "while you hold the control";
    const tapped = (KEYS.get(code)?.label ?? "the key") + (mods & 0x22 ? " and Shift" : "");
    out.append(h("div", { class: "hold-opt" },
      h("div", { class: "small" }, h("b", { text: `For app switchers like Alt+Tab` }),
        h("div", { class: "muted", text: `Keep ${held} down ${when} and tap ${tapped} ${opts.turn ? "on every click" : "once"}.` }),
        h("button", { class: "btn", style: "margin-top:8px", text: `Keep ${held} held`, onclick: () => set(holdTap(model, mods, code)) }))));
  }
  return out;
}

function mouseEditor(a, set, opts) {
  const v = a.t === T.SCROLL ? s8(a.p1) : 0, hz = a.t === T.SCROLL ? s8(a.p2) : 0;
  const amount = Math.max(1, Math.abs(v || hz || 1));
  const scrollOpt = (label, sv, sh) => h("button", { class: "opt", "aria-pressed": a.t === T.SCROLL && Math.sign(v) === Math.sign(sv) && Math.sign(hz) === Math.sign(sh) ? "true" : "false",
    onclick: () => set(A(T.SCROLL, u8(sv * amount), u8(sh * amount))) }, label);
  const amountBox = stepper(amount, 1, 20, n => {
    if (a.t === T.SCROLL) set(A(T.SCROLL, u8(Math.sign(v) * n), u8(Math.sign(hz) * n)));
    else set(A(T.SCROLL, u8(-n), 0));
  });
  const mx = a.t === T.MOVE ? s8(a.p1) : 10, my = a.t === T.MOVE ? s8(a.p2) : 0;
  return h("div", {},
    h("div", { class: "sub-h", text: "Buttons" }),
    h("div", { class: "grid" }, MOUSE.map(m => h("button", { class: "opt", "aria-pressed": a.t === T.MOUSE && a.p1 === m[0] ? "true" : "false", onclick: () => set(A(T.MOUSE, m[0])) }, m[1]))),
    h("div", { class: "sub-h", text: "Scroll" }),
    h("div", { class: "grid narrow" }, scrollOpt("Up", 1, 0), scrollOpt("Down", -1, 0), scrollOpt("Left", 0, -1), scrollOpt("Right", 0, 1)),
    h("div", { class: "row", style: "margin-top:8px" }, h("span", { class: "muted small", text: opts.turn ? "Notches per click" : "Notches per press" }), amountBox),
    h("div", { class: "sub-h", text: "Move the pointer" }),
    h("div", { class: "row" }, h("span", { class: "muted small", text: "Sideways" }), stepper(mx, -127, 127, n => set(A(T.MOVE, u8(n), u8(my)))),
      h("span", { class: "muted small", text: "Down" }), stepper(my, -127, 127, n => set(A(T.MOVE, u8(mx), u8(n))))),
    a.t !== T.MOVE ? h("div", { class: "row", style: "margin-top:8px" }, h("button", { class: "btn", onclick: () => set(A(T.MOVE, u8(mx), u8(my))) }, "Use pointer movement")) : null);
}

function deviceEditor(a, set) {
  const isP = a.t === T.PROFILE, held = isP && a.p2 === 1;
  const targets = [...Array.from({ length: model.np }, (_, p) => [p, `Profile ${p + 1}`]), [PROF_NEXT, "Next"], [PROF_PREV, "Previous"]];
  return h("div", {},
    h("div", { class: "sub-h", text: "Switch profile" }),
    h("div", { class: "grid narrow" }, targets.map(([p, n]) => h("button", { class: "opt", "aria-pressed": isP && a.p1 === p ? "true" : "false", onclick: () => set(A(T.PROFILE, p, held ? 1 : 0)) }, n))),
    h("div", { class: "row", style: "margin-top:8px" },
      h("label", { class: "switch" }, h("input", { type: "checkbox", checked: held, disabled: !isP || a.p1 >= PROF_NEXT, onchange: e => set(A(T.PROFILE, a.p1, e.target.checked ? 1 : 0)) }), h("span")),
      h("span", { class: "small", text: "Only while held, then switch back" })),
    h("div", { class: "sub-h", text: "Lights" }),
    h("div", { class: "grid" }, LIGHTS.map((l, n) => h("button", { class: "opt", "aria-pressed": a.t === T.LIGHT && a.p1 === n ? "true" : "false", onclick: () => set(A(T.LIGHT, n)) }, l[1]))),
    h("div", { class: "sub-h", text: "Knob axis" }),
    h("div", { class: "grid" }, h("button", { class: "opt", "aria-pressed": a.t === T.CENTER ? "true" : "false", onclick: () => set(A(T.CENTER)) }, "Center the knob axis")));
}

function stepper(value, min, max, onSet, step = 1) {
  const input = h("input", { type: "number", value, min, max, step, "aria-label": "amount", onchange: e => onSet(Math.max(min, Math.min(max, +e.target.value || 0))) });
  return h("span", { class: "stepper" },
    h("button", { "aria-label": "less", onclick: () => onSet(Math.max(min, value - step)) }, "−"), input,
    h("button", { "aria-label": "more", onclick: () => onSet(Math.min(max, value + step)) }, "+"));
}

// ---------------------------------------------------------------- sequence steps
const ICON_UP = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="m18 15-6-6-6 6"/></svg>';
const ICON_DOWN = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="m6 9 6 6 6-6"/></svg>';

function stepsEditor(id, a, opts) {
  const sid = a.p1, sq = model.seq(sid);
  const copy = () => ({ press: sq.press.map(st => ({ ...st })), release: sq.release.map(st => ({ ...st })) });
  const commit = next => change(m => m.setSeq(sid, next), `${id}:steps`);
  const holdLabel = opts.turn ? "Hold while turning" : "Hold while held";
  const section = (key, title, empty) => {
    const list = sq[key];
    const rows = list.map((st, n) => {
      const kinds = [["tap", "Press"], ...(key === "press" ? [["hold", holdLabel]] : []), ["wait", "Wait"]];
      const kind = h("select", { class: "plain step-kind", "aria-label": "Step type", onchange: e => {
          const c = copy(), v = e.target.value;
          c[key][n] = v === "wait" ? { op: "wait", ms: 100 } : { op: v, a: st.a || A(T.NONE) };
          commit(c);
        } }, kinds.map(([v, t]) => h("option", { value: v, selected: st.op === v, text: t })));
      const move = d => { const c = copy(); [c[key][n + d], c[key][n]] = [c[key][n], c[key][n + d]]; commit(c); };
      const tools = h("span", { class: "step-tools" },
        h("button", { class: "icon-btn", title: "Move up", "aria-label": "Move up", disabled: n === 0, onclick: () => move(-1) }, svg(ICON_UP)),
        h("button", { class: "icon-btn", title: "Move down", "aria-label": "Move down", disabled: n === list.length - 1, onclick: () => move(1) }, svg(ICON_DOWN)),
        h("button", { class: "icon-btn", title: "Remove step", "aria-label": "Remove step", onclick: () => { const c = copy(); c[key].splice(n, 1); commit(c); } }, svg(ICON_TRASH)));
      const body = st.op === "wait"
        ? h("div", { class: "row" }, stepper(st.ms, 20, 10000, v => { const c = copy(); c[key][n].ms = v; commit(c); }, 20), h("span", { class: "muted small", text: "milliseconds" }))
        : actionEditor(`${id}/${key}${n}`, st.a, x => { const c = copy(); c[key][n].a = x; commit(c); }, { ...opts, inStep: true, allowNone: undefined });
      return h("div", { class: "step" }, h("span", { class: "step-n", text: n + 1 }),
        h("div", { class: "step-main" }, h("div", { class: "step-head" }, kind, tools), body));
    });
    const add = (op, label) => h("button", { class: "chip", onclick: () => {
        const c = copy();
        c[key].push(op === "wait" ? { op, ms: 100 } : { op, a: A(T.NONE) });
        if (op !== "wait") { openPicker = `${id}/${key}${c[key].length - 1}`; pickerCat[openPicker] = "key"; }
        commit(c);
      } }, label);
    return h("div", { class: "steps-sec" }, h("div", { class: "sub-h", text: title }),
      rows.length ? rows : h("p", { class: "note", style: "margin:0 0 8px", text: empty }),
      h("div", { class: "mods" }, add("tap", "+ Press"), key === "press" ? add("hold", "+ " + holdLabel) : null, add("wait", "+ Wait")));
  };
  const used = model.usedBytes();
  return h("div", { class: "steps" },
    h("p", { class: "note", style: "margin:0 0 10px", text: opts.turn
      ? "These run on every click, in order. What they hold is let go once the knob stops."
      : "These run in order. Held steps stay pressed until you let go; then the steps below run." }),
    section("press", opts.turn ? "On every click" : "When pressed", "No steps yet: add one below."),
    opts.turn ? null : section("release", "When let go", "Nothing extra when you let go."),
    h("div", { class: "mem" }, h("div", { class: "mem-bar" }, h("i", { style: `width:${Math.min(100, used / model.recSize() * 100)}%` })),
      h("span", { text: `${used} of ${model.recSize()} bytes of the pad's memory for combos and steps used` + (model.np > 1 ? ". Using fewer profiles (Settings) gives more." : "") })));
}

// Shortcut recording
window.addEventListener("keydown", e => {
  if (!recording) {
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "z" && !/input|select|textarea/i.test(document.activeElement?.tagName || "")) {
      e.preventDefault(); e.shiftKey ? redo() : undo();
    }
    return;
  }
  e.preventDefault(); e.stopPropagation();
  if (e.code === "Escape" && !e.ctrlKey && !e.shiftKey && !e.altKey && !e.metaKey) { recording = false; render(); return; }
  let mods = (e.ctrlKey ? 1 : 0) | (e.shiftKey ? 2 : 0) | (e.altKey ? 4 : 0) | (e.metaKey ? 8 : 0);
  if (MOD_CODES[e.code]) { mods |= MOD_CODES[e.code]; if (MOD_CODES[e.code] >= 16) mods &= ~(MOD_CODES[e.code] >> 4); return previewRecord(mods); }
  const k = KEY_BY_CODE.get(e.code);
  if (!k) { toast(`That key can't be recorded (${e.code}). Pick it from the list instead.`, true); return; }
  recording = false;
  applyRecorded(A(T.KEY, mods, k.u));
}, true);
window.addEventListener("keyup", e => {
  if (!recording || !MOD_CODES[e.code]) return;
  // a modifier pressed and released on its own: record just the modifier
  if (!e.ctrlKey && !e.shiftKey && !e.altKey && !e.metaKey && pendingMods) { recording = false; applyRecorded(A(T.KEY, pendingMods, 0)); }
}, true);
let pendingMods = 0;
function previewRecord(mods) {
  pendingMods = mods;
  const r = document.querySelector(".rec"); if (r) r.firstChild.textContent = keyText(mods, 0) + " + …";
}
function applyRecorded(a) {
  pendingMods = 0;
  const target = pickerSetters.get(openPicker);
  if (target) target(a);
  render();
}
// ---------------------------------------------------------------- combos
function comboWhen(c) {
  return c.held === c.trig
    ? [h("span", { text: "Hold" }), h("span", { class: "kbd", text: L.name(c.held) }), h("span", { class: "muted", text: "alone" })]
    : [h("span", { text: "Hold" }), h("span", { class: "kbd", text: L.name(c.held) }), h("span", { text: c.trig >= L.buttons ? "and" : "then" }), h("span", { class: "kbd", text: L.name(c.trig) })];
}

function comboRow(c, compact) {
  const replaced = c.prof === ALL && model.combos().some(x => x.prof === prof && x.held === c.held && x.trig === c.trig);
  return h("button", { class: "combo" + (replaced ? " replaced" : ""), title: replaced ? `Profile ${prof + 1} has its own combo for these controls` : null, "data-selected": sel.kind === "combo" && sel.slot === c.slot ? "true" : "false",
      onclick: () => select({ kind: "combo", slot: c.slot }) },
    h("span", { class: "when" }, compact && c.held !== c.trig ? [h("span", { class: "kbd", text: L.name(c.trig) })] : comboWhen(c)),
    h("span", { class: "then", text: describe(c.a) }),
    h("span", { class: "scope", text: replaced ? "replaced here" : c.prof === ALL ? "all profiles" : "" }));
}

function renderCombos() {
  const all = model.combos(), mine = all.filter(c => c.prof === prof || c.prof === ALL);
  $("comboCount").textContent = `${model.usedBytes()} of ${model.recSize()} bytes used`;
  const list = $("comboList"); list.replaceChildren();
  if (!mine.length) list.append(h("div", { class: "empty", text: "No combos in this profile. Add one to make a key do more." }));
  else mine.forEach(c => list.append(comboRow(c)));
  $("addCombo").disabled = model.freeSlot() < 0;
}

function addCombo(held) {
  const slot = model.freeSlot();
  if (slot < 0) { toast("The pad's memory for combos and steps is full. Remove a combo or some steps first.", true); return; }
  const inProf = model.combos().filter(c => c.prof === prof || c.prof === ALL);
  const taken = (hh, t) => inProf.some(c => c.held === hh && c.trig === t);
  const helds = held !== undefined ? [held] : [...L.keys.map(k => k.i), L.knob];
  let pick = null;
  for (const hh of helds) {
    const order = [...L.keys.map(k => k.i), L.knob, L.right, L.left].filter(i => i !== hh);
    const t = order.find(i => !taken(hh, i));
    if (t !== undefined) { pick = { held: hh, trig: t }; break; }
  }
  if (!pick) { const hh = helds[0]; pick = { held: hh, trig: [...L.keys.map(k => k.i), L.knob].find(i => i !== hh) }; }
  change(m => m.setCombo(slot, { ...pick, prof, a: A(T.NONE) }), "add");
  sel = { kind: "combo", slot }; openPicker = `combo${slot}`; pickerCat[openPicker] = "key";
  render();
}

function renderComboInspector(box) {
  const c = model.combos().find(x => x.slot === sel.slot);
  if (!c) { sel = { kind: "input", i: L.keys[0].i }; return renderInspector(); }
  const body = h("div", { class: "panel-b" });
  box.append(h("div", { class: "panel-h insp-title" }, h("h2", { text: "Combo" }), h("div", { class: "spacer" }),
    h("button", { class: "icon-btn", title: "Remove this combo", "aria-label": "Remove this combo", onclick: () => {
      change(m => m.delCombo(c.slot), "del"); sel = { kind: "input", i: c.held }; render(); } }, svg(ICON_TRASH))), body);
  const upd = patch => change(m => m.setCombo(c.slot, { ...c, ...patch }), `combo${c.slot}`);
  const heldSel = h("select", { class: "plain", onchange: e => upd({ held: +e.target.value, trig: c.trig === +e.target.value && c.held !== c.trig ? c.held : c.trig }) },
    Array.from({ length: L.buttons }, (_, i) => h("option", { value: i, selected: i === c.held, text: L.name(i) })));
  const trigSel = h("select", { class: "plain", onchange: e => upd({ trig: +e.target.value }) },
    Array.from({ length: L.inputs }, (_, i) => h("option", { value: i, selected: i === c.trig, text: i === c.held ? `${L.name(i)} alone (long press)` : i >= L.buttons ? `Knob: ${L.name(i).toLowerCase()}` : `Press ${L.name(i)}` })));
  body.append(field("Hold", null, h("div", { class: "row" }, heldSel)));
  body.append(field("Then", null, h("div", { class: "row" }, trigSel)));
  body.append(field("Does", null, actionEditor(`combo${c.slot}`, c.a, a => upd({ a }), { input: c.trig, turn: c.trig >= L.buttons })));
  const scope = h("div", { class: "seg two" },
    h("button", { "aria-pressed": c.prof !== ALL ? "true" : "false", onclick: () => upd({ prof: c.prof === ALL ? prof : c.prof }) }, `Profile ${(c.prof === ALL ? prof : c.prof) + 1} only`),
    h("button", { "aria-pressed": c.prof === ALL ? "true" : "false", onclick: () => upd({ prof: ALL }) }, "All profiles"));
  body.append(field("Works in", null, scope));
  const others = model.combos().filter(x => x.slot !== c.slot && x.held === c.held && x.trig === c.trig);
  const twin = others.find(x => x.prof === c.prof);
  if (twin) body.append(h("p", { class: "note", style: "color:var(--danger)", text: `Another combo uses the same controls (${describe(twin.a)}). The one higher in the list wins; change “Then” or remove one.` }));
  else if (c.prof !== ALL && others.some(x => x.prof === ALL))
    body.append(h("p", { class: "note", text: `In profile ${c.prof + 1} this replaces the all-profiles combo for the same controls (${describe(others.find(x => x.prof === ALL).a)}).` }));
  else if (c.prof === ALL && others.length)
    body.append(h("p", { class: "note", text: `Profile ${others.map(x => x.prof + 1).join(", ")} has its own combo for these controls, which wins there.` }));
  if (c.held !== c.trig) {
    const own = model.action(prof, c.held);
    if (own.t !== T.NONE) body.append(h("p", { class: "note", text: model.now(prof, c.held)
      ? `${L.name(c.held)} holds its own action (${describe(own)}) from the moment you press it; this combo works on top of it.`
      : `${L.name(c.held)} still does its own action (${describe(own)}) when pressed on its own; that is sent when you let go.` }));
  }
  if (c.held === c.trig) body.append(h("p", { class: "note", text: `Hold ${L.name(c.held)} for ${model.b[S.HOLD] / 100} s. A shorter press does its normal action.` }));
}

// ---------------------------------------------------------------- lighting
function renderLighting() {
  $("lightTitle").textContent = `Profile ${prof + 1} lights`;
  $("lightHint").textContent = dev ? "Preview. The pad shows the same light right now." : "Preview of the lights.";
  const e = model.effect(prof), axis = model.action(prof, L.right).t === T.AXIS;
  const box = $("effects"); box.replaceChildren();
  EFFECTS.forEach(([k, n, d], idx) => box.append(h("button", { class: "effect", "aria-pressed": idx === e ? "true" : "false",
    onclick: () => change(m => m.setEffect(prof, idx), "effect") }, h("b", { text: n }), h("small", { text: idx === 5 && !axis ? "Set the knob to a gamepad axis first; until then it's solid." : d }))));
  const sw = $("swatches"); sw.replaceChildren();
  const cur = model.color(prof);
  SWATCHES.forEach(c => sw.append(h("button", { class: "swatch", style: `background:${c}`, title: c, "aria-label": `Color ${c}`,
    "aria-pressed": c === cur ? "true" : "false", onclick: () => change(m => m.setColor(prof, c), "color") })));
  sw.append(h("input", { type: "color", value: cur, "aria-label": "Custom color", onchange: ev => change(m => m.setColor(prof, ev.target.value), "color") }));
  showSlider("brightness", model.b[S.BRIGHT]); showSlider("speed", model.b[S.SPEED]);
}

// ---------------------------------------------------------------- settings
const SLIDER_TEXT = {
  brightness: v => Math.round(v / 2.55) + " %", speed: v => v, optDetents: v => v,
  optHold: v => (v / 100).toFixed(2) + " s", optTap: v => v + " ms", optDebounce: v => v + " ms",
  optTurnHold: v => (v / 10).toFixed(1) + " s",
};
function showSlider(id, v) { $(id).value = v; $(id + "Val").textContent = SLIDER_TEXT[id](v); }

function renderSettings() {
  const b = model.b;
  $("optInvert").checked = !!(b[S.FLAGS] & FLAG.INVERT);
  $("optHalf").checked = !!(b[S.FLAGS] & FLAG.HALF);
  showSlider("optDetents", b[S.DETENTS]); showSlider("optHold", b[S.HOLD]); showSlider("optTap", b[S.TAP]);
  showSlider("optDebounce", b[S.DEBOUNCE]); showSlider("optTurnHold", b[S.TURN_HOLD] || 8);
  $("profBytes").textContent = L.profSize;
  const st = $("optStart"); st.replaceChildren(...Array.from({ length: model.np }, (_, p) => h("option", { value: p, selected: p === b[S.START], text: `Profile ${p + 1}` })));
  const pc = $("optProfiles"); pc.replaceChildren(...Array.from({ length: L.profiles }, (_, k) => h("button", {
    "aria-pressed": k + 1 === model.np ? "true" : "false",
    onclick: () => {
      if (k + 1 === model.np) return;
      const lost = model.combos().filter(c => (c.prof !== ALL && c.prof > k) || (c.a.t === T.PROFILE && c.a.p1 > k && c.a.p1 < PROF_NEXT)).length;
      change(m => m.setProfileCount(k + 1), "profiles");
      if (prof > k) selectProfile(0);
      toast(`${k + 1} profile${k ? "s" : ""}: ${model.recSize()} bytes for combos and steps` + (lost ? `. Removed ${lost} combo${lost > 1 ? "s" : ""} of the dropped profiles.` : ""));
    } }, String(k + 1))));
  const usb = ["Keyboard and mouse", "Keyboard, mouse and gamepad"][L.usb] || "?";
  $("fwInfo").replaceChildren(
    h("dt", { text: "Firmware" }), h("dd", { text: dev ? `Wildpad ${L.fw}` : "Not connected (demo)" }),
    h("dt", { text: "Board" }), h("dd", { text: `${L.boardName}, ${L.leds} light${L.leds === 1 ? "" : "s"}` }),
    h("dt", { text: "Shows up as" }), h("dd", { text: dev ? usb : "—" }),
    h("dt", { text: "Memory" }), h("dd", { text: `${model.np} of ${L.profiles} profiles in use; ${model.recSize()} bytes for combos and steps, ${model.usedBytes()} used` }));
  $("fwUpdate").disabled = !("usb" in navigator);
  renderBoard();
}

// ---------------------------------------------------------------- firmware update (WebUSB)
// Same protocol as tools/flash.py: the CH552's ROM bootloader (4348:55E0, v2.x) on
// bulk endpoint 2.
const BOOT_VID = 0x4348, BOOT_PID = 0x55E0, CODE_SIZE = 14336;
const FW_URLS = ["wildpad.bin", "../firmware/wildpad.bin", "../firmware/build/wildpad.bin"];
// The page runs from the download (web/) or as the website's index
const DOCS = location.pathname.includes("/web/") ? "../docs/" : "docs/";
const DETECT = [0xA1, 0x12, 0x00, 0x52, 0x11, 0x4D, 0x43, 0x55, 0x20, 0x49, 0x53, 0x50, 0x20, 0x26, 0x20, 0x57, 0x43, 0x48, 0x2E, 0x43, 0x4E];
let fwImage = null, updating = false;

// A Wildpad image: fits the code flash and starts with the reset jump (LJMP)
const validFirmware = d => d.length > 1024 && d.length <= CODE_SIZE && d[0] === 0x02;

async function loadFirmware() {
  for (const url of FW_URLS) {
    try {
      const r = await fetch(url, { cache: "no-store" });
      if (!r.ok) continue;
      const data = new Uint8Array(await r.arrayBuffer());
      if (!validFirmware(data)) continue;
      fwImage = { data, name: url.split("/").pop() };
      const ver = await fetch("version.json", { cache: "no-store" }).then(r => r.ok ? r.json() : null).catch(() => null);
      $("fwSource").textContent = ver?.firmware ? `Latest firmware: ${ver.firmware}` : `Latest firmware, ${(data.length / 1024).toFixed(1)} KB`;
      return;
    } catch { /* not served here */ }
  }
  $("fwSource").textContent = "Pick the firmware file (wildpad.bin) with “Use a file…”.";
}

function fwStatus(text, err, progress) {
  const g = $("guide").open, st = $(g ? "gStatus" : "fwStatus"), bar = $(g ? "gBar" : "fwBar");
  if (!st) return;
  st.textContent = text; st.className = "fw-status" + (err ? " err" : "");
  bar.classList.toggle("hidden", progress === undefined);
  if (progress !== undefined) bar.firstChild.style.width = `${Math.round(progress * 100)}%`;
}

const withTimeout = (promise, ms, what) => Promise.race([promise,
  new Promise((_, rej) => setTimeout(() => rej(new Error(`the update module did not answer (${what})`)), ms))]);

class Isp {
  constructor(usb) { this.usb = usb; this.chip = 0; }
  async cmd(bytes) {
    await withTimeout(this.usb.transferOut(2, new Uint8Array(bytes)), 2000, "write");
    const r = await withTimeout(this.usb.transferIn(2, 64), 2000, "read");
    return new Uint8Array(r.data.buffer, r.data.byteOffset, r.data.byteLength);
  }
  async detect() {
    const r = await this.cmd(DETECT);
    if (r.length !== 6 || r[4] !== 0x52) throw new Error("this isn't a CH552");
    this.chip = r[4];
    const c = await this.cmd([0xA7, 0x02, 0x00, 0x1F, 0x00]);
    if (c.length !== 30) throw new Error("could not read the update module's settings");
    const key = (c[22] + c[23] + c[24] + c[25]) & 0xFF;
    const k = new Uint8Array(64); k.set([0xA3, 0x30, 0x00]); k.fill(key, 3, 3 + 0x30);
    await this.cmd(k);
    return `${c[19]}.${c[20]}${c[21]}`;
  }
  async erase(size) {
    const r = await this.cmd([0xA4, 0x01, 0x00, Math.max(8, Math.ceil(size / 1024))]);
    if (r[4] !== 0) throw new Error("erasing failed");
  }
  async write(data, verify, progress) {
    for (let addr = 0; addr < data.length;) {
      const n = Math.min(0x38, data.length - addr), rest = data.length - addr, buf = new Uint8Array(64);
      buf[0] = verify ? 0xA6 : 0xA5; buf[1] = n + 5; buf[3] = addr & 0xFF; buf[4] = addr >> 8; buf[7] = rest & 0xFF;
      buf.set(data.subarray(addr, addr + n), 8);
      for (let x = 7; x < n + 8; x += 8) buf[x] ^= this.chip;
      const r = await this.cmd(buf);
      if (![0x00, 0xFE, 0xF5].includes(r[4])) throw new Error(`${verify ? "checking" : "writing"} failed at 0x${addr.toString(16)}`);
      addr += n;
      progress(addr / data.length);
    }
  }
  async exit() { await this.usb.transferOut(2, new Uint8Array([0xA2, 0x01, 0x00, 0x01])).catch(() => {}); }
}

const isBoot = d => d.vendorId === BOOT_VID && d.productId === BOOT_PID;
// The firmware's update layout (docs/protocol.md): it hands its USB connection to the
// ROM bootloader on request, so Windows needs no extra driver and can't fail to start
// one. It has a serial number, so the browser remembers the permission.
const UPDATE = { vendorId: VID, productId: PID, serialNumber: "WILDPADU" };
const isUpdate = d => d.vendorId === VID && d.productId === PID && d.serialNumber === UPDATE.serialNumber;

// A device the page may use, as soon as it is there
function waitForUsb(match, ms) {
  return new Promise(resolve => {
    const done = d => { clearTimeout(timer); navigator.usb.removeEventListener("connect", on); resolve(d); };
    const on = e => { if (match(e.device)) done(e.device); };
    const timer = setTimeout(() => done(null), ms);
    navigator.usb.addEventListener("connect", on);
    navigator.usb.getDevices().then(list => { const d = list.find(match); if (d) done(d); });
  });
}

async function flashDevice(usb) {
  const data = fwImage.data;
  await usb.open();
  try {
    if (usb.configuration === null) await usb.selectConfiguration(1);
    await usb.claimInterface(0);
    if (isUpdate(usb)) {                                  // hand over to the ROM bootloader
      await usb.controlTransferOut({ requestType: "vendor", recipient: "device", request: 0x5B, value: 0, index: 0 });
      await sleep(50);
    }
    const isp = new Isp(usb);
    const ver = await isp.detect();
    fwStatus(`Bootloader ${ver} ready. Erasing…`, false, 0);
    await isp.erase(data.length);
    await isp.write(data, false, f => fwStatus("Writing…", false, f * 0.5));
    await isp.write(data, true, f => fwStatus("Checking…", false, 0.5 + f * 0.5));
    await isp.exit();
  } finally { await usb.close().catch(() => {}); }
}

async function updateFirmware() {
  if (updating) return;
  if (!("usb" in navigator)) { fwStatus("Firmware updates need Chrome or Edge (WebUSB).", true); return; }
  if (!fwImage) { fwStatus("Pick the firmware file first (Use a file…).", true); return; }
  updating = true;
  $("fwUpdate").disabled = true;
  try {
    let usb;
    if (dev) {
      const known = (await navigator.usb.getDevices()).some(isUpdate);
      fwStatus(known ? "Switching the pad to update mode…" : "Switching the pad to update mode… Pick “Wildpad” in the list when it shows up.", false);
      await command(CMD.BOOT, 1, [], false).catch(() => {});
      // older firmware starts the ROM bootloader instead ("USB Module")
      usb = known ? await waitForUsb(d => isUpdate(d) || isBoot(d), 12000)
                  : await navigator.usb.requestDevice({ filters: [UPDATE, { vendorId: BOOT_VID, productId: BOOT_PID }] });
      if (!usb) throw new Error("the pad didn't show up in update mode");
    } else {
      fwStatus("Plug the pad in while holding key 1, then pick “USB Module” in the list.", false);
      usb = (await navigator.usb.getDevices()).find(isBoot)
        || await navigator.usb.requestDevice({ filters: [{ vendorId: BOOT_VID, productId: BOOT_PID }] });
    }
    await flashDevice(usb);
    fwStatus("Done. The pad restarts with the new firmware.", false, 1);
    toast("Firmware updated");
  } catch (e) {
    if (e.name === "NotFoundError") fwStatus("Nothing picked. Press Update firmware to try again.", true);
    else if (e.name === "SecurityError" || /access denied/i.test(e.message))
      fwStatus("The browser may not use the chip's bootloader. On Windows it needs the WinUSB driver for “USB Module” (Zadig, see below).", true);
    else fwStatus("Update failed: " + (e.message || e) + ". Press Update firmware to try again. A pad whose update broke off waits in its bootloader whenever it's plugged in.", true);
  } finally {
    updating = false;
    $("fwUpdate").disabled = false;
  }
}

// ---------------------------------------------------------------- first install guide
// From the factory firmware: the chip's own bootloader, entered by shorting two pads
// while plugging in. Windows needs the WinUSB driver for it once (Zadig).
const isWindows = /Windows/i.test(navigator.userAgent);
const isLinux = /Linux/i.test(navigator.userAgent) && !/Android/i.test(navigator.userAgent);
// Linux lets the browser use USB devices only with an udev rule (once per computer)
function linuxTip() {
  return isLinux ? h("div", { class: "tip" },
    h("b", { text: "Linux: " }), "let the browser use the pad once. Download ",
    h("a", { href: "60-wildpad.rules", download: "60-wildpad.rules", text: "60-wildpad.rules" }), ", then run ",
    copyChip("sudo cp 60-wildpad.rules /etc/udev/rules.d/ && sudo udevadm control --reload-rules && sudo udevadm trigger"),
    " in the folder you saved it to.") : null;
}
const guide = { step: 0, board: 1, busy: false, done: false, alt: false };

function guideSteps() {
  return [["pad", "Your pad"], ...(isWindows ? [["driver", "Driver"]] : []), ["open", "Open it"], ["install", "Install"], ["done", "Done"]];
}

function openGuide() {
  Object.assign(guide, { step: 0, busy: false, done: false, alt: false });
  renderGuide();
  $("guide").showModal();
}

function copyChip(text) {
  const b = h("button", { type: "button", text: "Copy" });
  b.addEventListener("click", () => navigator.clipboard.writeText(text).then(() => { b.textContent = "Copied"; setTimeout(() => b.textContent = "Copy", 1200); }));
  return h("span", { class: "copy" }, text, b);
}

function renderGuide() {
  const steps = guideSteps(), key = steps[guide.step][0], body = $("guideBody");
  $("guideSteps").replaceChildren(...steps.map(([, name], i) => h("li", { class: i < guide.step ? "done" : i === guide.step ? "on" : "", text: name })));
  $("guideBack").disabled = guide.step === 0 || guide.busy;
  $("guideNext").disabled = guide.busy || (key === "install" && !guide.done);
  $("guideNext").textContent = key === "done" ? "Close" : "Next";
  const P = text => h("p", { text });
  const fill = (...kids) => body.replaceChildren(...kids.flat().filter(k => k != null && k !== false));
  if (key === "pad") {
    fill(h("h3", { text: "Which pad is it?" }),
      P("Wildpad replaces the firmware the pad came with. For that the case is opened once; later updates need no screwdriver."),
      h("div", { class: "board-list" }, [[1, 3, 3, "3 keys and a knob"], [2, 6, 3, "6 keys and a knob"], [0, 0, 0, "Another CH552 pad"]].map(([id, keys, cols, name]) =>
        h("button", { class: "board-card", "aria-pressed": guide.board === id ? "true" : "false", onclick: () => { guide.board = id; renderGuide(); } },
          keys ? miniBoard(keys, cols, true) : h("div", { class: "mini-q", text: "?" }), h("span", { text: name })))),
      !("usb" in navigator) ? h("p", { class: "tip", text: "This browser can't install firmware. Open this page in Chrome or Edge on a computer." }) : null,
      linuxTip(),
      !fwImage ? h("p", { class: "tip", text: "The firmware file isn't loaded: pick wildpad.bin under Settings → Firmware → Use a file… first." }) : null);
  } else if (key === "driver") {
    fill(h("h3", { text: "Install the USB driver (once per computer)" }),
      P("Windows needs a driver to let the browser talk to the chip's built-in installer. Zadig sets it up in a minute; nothing has to be connected yet."),
      h("ol", { class: "do" },
        h("li", {}, "Download ", h("a", { href: "https://zadig.akeo.ie", target: "_blank", rel: "noopener", text: "Zadig" }), " and start it. It needs no installation; allow it to make changes."),
        h("li", {}, "In its menu choose ", h("b", { text: "Device → Create New Device" }), "."),
        h("li", {}, "Type the name ", copyChip("CH552 bootloader"), " into the long field, and ", copyChip("4348"), " and ", copyChip("55E0"), " into the two small USB ID fields."),
        h("li", {}, "Right of the green arrow, pick ", h("b", { text: "WinUSB" }), " with the small arrows."),
        h("li", {}, "Check what the big button says. It must be ", h("b", { text: "Install Driver" }), ". If it says Install WCID Driver, open the ▼ on its right and choose Install Driver. Press it and wait for “The driver was installed successfully”.")),
      h("p", { class: "tip", text: "Done this before on this computer? Then just go on." }));
  } else if (key === "open") {
    const three = guide.board === 1;
    fill(h("h3", { text: "Open the case and find the two pads" }),
      three ? P("Remove the four screws on the bottom and lift off the bottom plate. Next to the black chip marked WCH, find R12: two small pads with nothing soldered on them.")
            : P("Remove the screws on the bottom and lift off the bottom plate. Look for two small empty pads close to the black chip marked WCH, usually labelled R12."),
      three ? h("img", { class: "photo", src: DOCS + "r12_short.jpeg", alt: "Circuit board: the two empty R12 pads next to the chip, circled in red", onclick: e => window.open(e.target.src) }) : null,
      three ? null : h("p", { class: "tip", text: "No photo of this board yet. Try first without opening it: on many 6-key pads one key is wired to the same chip pin, so holding key 3 (or key 4) while plugging in does the same. Otherwise: the pads connect chip pin P1.5 to ground (GND), and connecting those two directly works too." }),
      P("You'll need something small that conducts: tweezers, a paper clip or a piece of wire."));
  } else if (key === "install" && guide.alt) {
    fill(h("h3", { text: "Install with flash.bat (Windows)" }),
      P("The same install without the browser. It uses another driver (libusb-win32), with which Windows starts the chip's installer every time."),
      h("ol", { class: "do" },
        h("li", {}, "Download the Wildpad zip and unpack it. ", h("b", { text: "flash.bat" }), " needs ", h("a", { href: "https://www.python.org/downloads/", target: "_blank", rel: "noopener", text: "Python 3" }), "."),
        h("li", {}, "In Zadig, again ", h("b", { text: "Device → Create New Device" }), ", ", copyChip("CH552 bootloader"), ", ", copyChip("4348"), " ", copyChip("55E0"), "."),
        h("li", {}, "With the small arrows pick ", h("b", { text: "libusb-win32" }), ". The big button must say ", h("b", { text: "Install Driver" }), " (▼ to change it). Press it."),
        h("li", {}, "Start ", h("b", { text: "flash.bat" }), ". When it waits for the bootloader, connect the two pads and plug the pad in."),
        h("li", {}, "It says DONE after a few seconds. Then come back here and press the button below.")),
      h("p", { class: "tip", text: "With libusb-win32 the browser can't reach the chip's installer any more; that's fine. Updates later run from this page through the pad's own update mode and need no driver at all." }),
      h("div", { class: "row" },
        h("button", { class: "btn", text: "Back to the browser install", onclick: () => { guide.alt = false; renderGuide(); } }),
        h("button", { class: "btn primary", text: "flash.bat said DONE", onclick: () => { guide.done = true; guide.alt = false; guide.step++; renderGuide(); } })));
  } else if (key === "install") {
    fill(h("h3", { text: "Install" }),
      h("ol", { class: "do" },
        h("li", {}, "Unplug the pad, then press ", h("b", { text: "Start" }), " below. The browser opens a list of USB devices: leave it open."),
        h("li", {}, "Connect the two pads with the tweezers and, ", h("b", { text: "while holding them" }), ", plug the USB cable in. The lights stay dark: that's right."),
        h("li", {}, h("b", { text: "USB Module" }), " shows up in the list. Pick it and press Connect; you can let go of the pads. Installing takes about ten seconds.")),
      h("div", { class: "row" }, h("button", { class: "btn primary", id: "gStart", text: "Start", disabled: guide.busy || !fwImage || !("usb" in navigator), onclick: guideInstall })),
      h("div", { class: "fw-bar hidden", id: "gBar" }, h("i")),
      h("p", { class: "fw-status", id: "gStatus", role: "status", "aria-live": "polite" }),
      h("p", { class: "tip" }, isWindows
        ? "USB Module doesn't show up although Windows made its connect sound? Unplug the pad and plug it in again while connecting the pads: Windows doesn't start the driver for the chip's installer every time, so it can take a few goes. No sound at all means the pads weren't connected."
        : "USB Module doesn't show up? Unplug the pad and plug it in again while connecting the pads firmly."),
      isWindows ? h("p", {}, h("a", { href: "#", text: "Still no luck? Install with flash.bat instead", onclick: e => { e.preventDefault(); guide.alt = true; renderGuide(); } })) : null);
  } else {
    fill(h("h3", { text: "Done" }),
      P("The pad restarts with Wildpad. Put the bottom plate back on; you won't need to open it again: updates now run from this page."),
      h("ol", { class: "do" },
        h("li", {}, "Press ", h("b", { text: "Connect" }), " and pick the pad."),
        guide.board === 1 ? null : h("li", {}, "Under ", h("b", { text: "Settings → Board" }), ", choose your pad (or let the page find its keys)."),
        h("li", {}, "Click a key on the drawing and choose what it does.")),
      h("div", { class: "row" }, h("button", { class: "btn primary", text: "Connect", onclick: () => { $("guide").close(); connect(); } })));
  }
}

async function guideInstall() {
  guide.busy = true; renderGuide();
  try {
    const usb = (await navigator.usb.getDevices()).find(isBoot)
      || await navigator.usb.requestDevice({ filters: [{ vendorId: BOOT_VID, productId: BOOT_PID }] });
    await flashDevice(usb);
    guide.done = true; guide.busy = false; guide.step++;
    renderGuide();
    toast("Wildpad installed");
  } catch (e) {
    guide.busy = false; renderGuide();
    if (e.name === "NotFoundError") fwStatus("Nothing picked. Press Start to try again.", true);
    else if (e.name === "SecurityError" || /access denied/i.test(e.message))
      fwStatus("The browser may not use USB Module. " + (isWindows ? "Install the driver with Zadig (step 2) and try again." : "Check that no other program uses it."), true);
    else fwStatus("Didn't work: " + (e.message || e) + ". Unplug the pad and start again; nothing is broken.", true);
  }
}

// ---------------------------------------------------------------- board choice
let boardOpen = false, boardChoice = null, wiz = null;
const SKIP_PINS = new Set([8, 9, 12, 14, 15]);          // knob phases, light chain, USB

function miniBoard(keys, cols, knob) {
  return h("div", { class: "mini", "aria-hidden": "true" },
    h("div", { class: "mini-keys", style: `grid-template-columns: repeat(${cols}, 14px)` }, Array.from({ length: keys }, () => h("i"))),
    knob ? h("span", { class: "mini-knob" }) : null);
}

function renderBoard() {
  $("boardName").textContent = L.boardName;
  $("boardChange").textContent = boardOpen ? "Close" : "Change board";
  $("boardPick").classList.toggle("hidden", !boardOpen);
  if (!boardOpen) return;
  const list = $("boardList");
  list.replaceChildren(...Object.entries(BOARDS).map(([id, bd]) => {
    const keys = (bd.map[0] & 15) - 1;
    return h("button", { class: "board-card", "aria-pressed": boardChoice === +id || (boardChoice === null && L.board === +id) ? "true" : "false",
      onclick: () => { stopWizard(); boardChoice = +id; renderBoard(); } },
      miniBoard(keys, keys <= 4 ? keys : Math.ceil(keys / 2), true), h("span", { text: bd.name }));
  }), h("button", { class: "board-card", "aria-pressed": wiz ? "true" : "false", disabled: !dev,
      title: dev ? null : "Connect the pad first", onclick: startWizard },
    h("div", { class: "mini-q", text: "?" }), h("span", { text: dev ? "Another pad: find its keys" : "Another pad (connect it first)" })));
  const ask = boardChoice !== null && boardChoice !== L.board;
  $("boardConfirm").classList.toggle("hidden", !ask);
  if (ask) $("boardConfirmText").textContent = `Start over with factory settings for ${BOARDS[boardChoice].name}?`;
  renderWizard();
}

async function applyBoard(id, map = []) {
  if (!dev) {                                             // demo: just show it
    L = new Layout(id ? boardInfo(id) : boardInfo(0, map));
    model = factory(L); prof = 0; sel = { kind: "input", i: L.keys[0]?.i ?? 0 };
    undoStack = []; redoStack = [];
    boardOpen = false; boardChoice = null; render();
    return;
  }
  try {
    clearTimeout(pushTimer); clearTimeout(saveTimer);
    const r = await command(CMD.BOARD, id, map);
    await command(CMD.SAVE);
    undoStack = []; redoStack = [];
    boardOpen = false; boardChoice = null; stopWizard();
    if (r[0]) {
      reconnecting = true; toast("Board set. The pad reconnects…");
      await command(CMD.REBOOT, 0, [], false).catch(() => {});
    } else {
      await attach(dev);
      toast("Board set, with factory settings for it");
    }
  } catch (e) { fail(e); }
}

// Find a board's keys: press them in order, then the knob
function startWizard() {
  if (!dev) return;
  boardChoice = null;
  wiz = { step: "keys", pins: [], knob: null, base: null, up: null, leds: null, rev: false, timer: null };
  renderBoard();
  wizPoll();
}
function stopWizard() { if (wiz) clearTimeout(wiz.timer); wiz = null; }

async function wizPoll() {
  const w = wiz;
  if (!w || !dev) return;
  try {
    const st = await command(CMD.STATUS);
    const low = [];
    for (let k = 0; k < 16; k++) if (!SKIP_PINS.has(k) && !((k < 8 ? st[5] >> k : st[6] >> (k - 8)) & 1)) low.push(k);
    if (!w.base) w.base = new Set(low);                     // pins that are low anyway
    if (w.up !== null) { if (!low.includes(w.up)) w.up = null; }
    else if (w.step === "keys" || w.step === "knob") {
      const k = low.find(k => !w.base.has(k) && !w.pins.includes(k));
      if (k !== undefined) {
        w.up = k;
        if (w.step === "keys") { w.pins.push(k); if (w.pins.length === 7) w.step = "knob"; }
        else { w.knob = k; w.step = "leds"; }
        renderWizard();
      }
    }
  } catch (e) { stopWizard(); fail(e); renderBoard(); return; }
  if (wiz === w && w.step !== "leds") w.timer = setTimeout(wizPoll, 40);
}

function wizardMap() {
  const w = wiz, pins = w.knob === null ? w.pins : [...w.pins, w.knob];
  const leds = w.leds ?? w.pins.length;
  const map = [pins.length | (w.knob === null ? NO_KNOB : pins.length - 1) << 4,
    leds | (w.rev ? MAP_LED_REVERSED : 0) | (w.knob === null && w.noKnob ? MAP_NO_KNOB : 0), ...pins];
  while (map.length < MAP_LEN) map.push(0xFF);
  const known = Object.entries(BOARDS).find(([, bd]) => bd.map.every((v, i) => v === map[i]) && map.slice(bd.map.length).every(v => v === 0xFF));
  return known ? { id: +known[0], map: [] } : { id: 0, map };
}

function renderWizard() {
  const box = $("boardWizard"), w = wiz;
  box.classList.toggle("hidden", !w);
  if (!w) return;
  const found = h("div", { class: "wiz-found" },
    w.pins.map((k, n) => h("span", { text: `Key ${n + 1}: ${pinName(k)}` })),
    w.knob !== null ? h("span", { text: `Knob: ${pinName(w.knob)}` }) : null);
  const cancel = h("button", { class: "btn", text: "Cancel", onclick: () => { stopWizard(); renderBoard(); } });
  if (w.step === "keys") {
    box.replaceChildren(h("h3", { text: w.pins.length ? `Now key ${w.pins.length + 1}` : "Press key 1" }),
      h("p", { text: "Press the keys one at a time: the top row from left to right, then the next row. Don't press the knob yet." }),
      found, h("div", { class: "row" },
        h("button", { class: "btn primary", text: "That's all the keys", disabled: !w.pins.length, onclick: () => { w.step = "knob"; renderWizard(); } }), cancel));
  } else if (w.step === "knob") {
    box.replaceChildren(h("h3", { text: "Press the knob" }),
      h("p", { text: "Push the knob down like a button." }), found,
      h("div", { class: "row" },
        h("button", { class: "btn", text: "It has a knob, but it doesn't press", onclick: () => { w.step = "leds"; renderWizard(); } }),
        h("button", { class: "btn", text: "There is no knob", onclick: () => { w.noKnob = true; w.step = "leds"; renderWizard(); } }), cancel));
  } else {
    const n = w.leds ?? w.pins.length;
    box.replaceChildren(h("h3", { text: "Lights" }),
      h("p", { text: "How many keys have a light under them? Usually all of them." }), found,
      h("div", { class: "seg", style: "margin-bottom:10px" }, Array.from({ length: 9 }, (_, k) =>
        h("button", { "aria-pressed": k === n ? "true" : "false", text: String(k), onclick: () => { w.leds = k; renderWizard(); } }))),
      h("label", { class: "row", style: "margin-bottom:12px" },
        h("span", { class: "switch" }, h("input", { type: "checkbox", checked: w.rev, onchange: e => { w.rev = e.target.checked; } }), h("span")),
        h("span", { text: "The lights run from the last key to the first (turn on if the wrong key lights up when pressed)" })),
      h("div", { class: "row" },
        h("button", { class: "btn primary", text: "Use this board", onclick: () => {
          const r = wizardMap();
          if (r.id && r.id === L.board) {                   // nothing to change: keep the settings
            stopWizard(); boardOpen = false; renderBoard();
            toast(`That's ${BOARDS[r.id].name}, the board it's set up as already`);
          } else applyBoard(r.id, r.map);
        } }), cancel));
  }
}

function bindSettings() {
  const flag = (id, f) => $(id).addEventListener("change", e => change(m => { m.b[S.FLAGS] = e.target.checked ? m.b[S.FLAGS] | f : m.b[S.FLAGS] & ~f; }, id));
  flag("optInvert", FLAG.INVERT); flag("optHalf", FLAG.HALF);
  // sliders show the value while dragged and apply it when let go
  const num = (id, off) => {
    $(id).addEventListener("input", e => { $(id + "Val").textContent = SLIDER_TEXT[id](+e.target.value); });
    $(id).addEventListener("change", e => change(m => { m.b[off] = +e.target.value; }, id));
  };
  num("optDetents", S.DETENTS); num("optHold", S.HOLD); num("optTap", S.TAP); num("optDebounce", S.DEBOUNCE); num("optTurnHold", S.TURN_HOLD);
  num("brightness", S.BRIGHT); num("speed", S.SPEED);
  $("optStart").addEventListener("change", e => change(m => { m.b[S.START] = +e.target.value; }, "start"));
  $("boardChange").addEventListener("click", () => { boardOpen = !boardOpen; boardChoice = null; stopWizard(); renderBoard(); });
  $("boardNo").addEventListener("click", () => { boardChoice = null; renderBoard(); });
  $("boardYes").addEventListener("click", () => applyBoard(boardChoice));
}

