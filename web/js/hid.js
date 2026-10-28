// =============================================================================== state
const $ = id => document.getElementById(id);
let L = new Layout(DEMO_INFO);
let model = factory(L);
let dev = null;                     // HIDDevice when connected
let prof = 0;                       // profile shown (and active on the device)
let sel = { kind: "input", i: L.keys[0].i };   // selection: {kind:"input", i} or {kind:"combo", slot}
let openPicker = null;              // id of the expanded action picker
let live = { pos: 0, btn: 0, prof: 0, flags: 0 };
let undoStack = [], redoStack = [], lastUndoKey = null, lastUndoTime = 0;
let tab = "controls";

// =============================================================================== transport
let busyChain = Promise.resolve(), seq = 0;
const sleep = ms => new Promise(r => setTimeout(r, ms));
function serial(fn) { const p = busyChain.then(fn, fn); busyChain = p.catch(() => {}); return p; }

function command(c, off = 0, data = [], wait = true) {
  return serial(async () => {
    if (!dev) throw new Error("not connected");
    seq = seq % 120 + 1;
    const buf = new Uint8Array(FLEN);
    buf.set([c, seq, off, data.length]); buf.set(data, 4);
    await dev.sendFeatureReport(0, buf);
    if (!wait) return null;
    for (let i = 0; i < 80; i++) {
      const dv = await dev.receiveFeatureReport(0);
      let b = new Uint8Array(dv.buffer, dv.byteOffset, dv.byteLength);
      if (b.length === FLEN + 1) b = b.subarray(1);
      if (b[0] === c && b[1] === seq) {
        if (b[3] !== PROTO) throw new Error(`the pad runs older firmware (protocol ${b[3]}). Update it with flash.bat`);
        if (b[2]) throw new Error("the pad refused: " + (STATUS_TEXT[b[2]] || b[2]));
        return b.slice(4);
      }
      await sleep(3);
    }
    throw new Error("no answer from the pad");
  });
}

async function readAll(size) {
  const out = new Uint8Array(size);
  for (let off = 0; off < size; off += CHUNK) {
    const n = Math.min(CHUNK, size - off);
    out.set((await command(CMD.READ, off, new Array(n).fill(0))).slice(0, n), off);
  }
  return out;
}

// Push the model to the device (live), then store it a moment later.
let pushTimer = null, saveTimer = null, pushing = false, pendingPush = false, needSave = false;
function schedulePush() {
  if (!dev) return;
  needSave = true;
  clearTimeout(saveTimer);
  setSync("busy");
  clearTimeout(pushTimer);
  pushTimer = setTimeout(push, 120);
}
async function push() {
  if (!dev) return;
  if (pushing) { pendingPush = true; return; }
  pushing = true;
  needSave = true;
  clearTimeout(saveTimer);
  try {
    const b = model.bytes();
    for (let off = 0; off < b.length; off += CHUNK) await command(CMD.WRITE, off, Array.from(b.subarray(off, off + CHUNK)));
    await command(CMD.COMMIT);
    needSave = true;
    clearTimeout(saveTimer);
    saveTimer = setTimeout(save, 900);
  } catch (e) { fail(e); }
  pushing = false;
  if (pendingPush) { pendingPush = false; push(); }
}
async function save() {
  if (!dev || pushing || pendingPush) { clearTimeout(saveTimer); saveTimer = setTimeout(save, 400); return; }
  try {
    const r = await command(CMD.SAVE);
    needSave = false;
    if (r[0]) {
      toast(model.usesGamepad() ? "Saved. The pad reconnects to add the gamepad…" : "Saved. The pad reconnects without the gamepad…");
      reconnecting = true;
      await command(CMD.REBOOT, 0, [], false).catch(() => {});
    }
    setSync("saved");
  } catch (e) { fail(e); }
}

// =============================================================================== connection
let reconnecting = false;
async function connect() {
  if (!("hid" in navigator)) return;
  try {
    const list = await navigator.hid.requestDevice({ filters: [{ vendorId: VID, productId: PID, usagePage: PAGE }] });
    if (list.length) await attach(list[0]);
  } catch (e) { fail(e); }
}

async function attach(d) {
  try {
    if (!d.opened) await d.open();
    dev = d;
    setSync("busy", "Connecting…");
    const info = await command(CMD.INFO);
    const nl = new Layout(info);
    if (nl.version !== SVER) throw new Error("this page and the firmware don't match. Update the firmware with flash.bat");
    L = nl;
    model = new Model(L, await readAll(L.size));
    prof = L.current;
    const st = await command(CMD.STATUS);
    live = { pos: (st[0] | st[1] << 8) << 16 >> 16, btn: st[2], prof: st[3], flags: st[4] };
    if (!reconnecting) { undoStack = []; redoStack = []; }
    if (!sel || (sel.kind === "input" && sel.i >= L.inputs)) sel = { kind: "input", i: L.keys[0]?.i ?? 0 };
    dev.oninputreport = e => {
      if (e.data.byteLength < 5) return;
      const prev = live;
      live = { pos: e.data.getInt16(0, true), btn: e.data.getUint8(2), prof: e.data.getUint8(3), flags: e.data.getUint8(4) };
      if (live.prof !== prev.prof) {                       // the pad switched profiles
        if (live.prof !== prof) prof = live.prof;
        render();
      }
      else renderLive();
    };
    if (reconnecting) toast("Reconnected");
    reconnecting = false;
    setSync("saved");
    render();
  } catch (e) { dev = null; fail(e); render(); }
}

function detach() {
  dev = null;
  setSync("off");
  render();
}

function setSync(state, text) {
  const dot = $("dot"), label = $("connText");
  dot.className = "dot" + (state === "saved" ? " on" : state === "busy" ? " busy" : "");
  $("disconnectBtn").disabled = state === "busy";        // not while changes are on their way
  label.textContent = text || (state === "saved" ? "Connected · saved on the pad" : state === "busy" ? "Saving…" : reconnecting ? "Reconnecting…" : "Demo");
}

function fail(e) {
  console.error(e);
  toast(e.message || String(e), true);
  if (dev) setSync("error", needSave ? "Connected · changes not saved" : "Connected · operation failed");
}

let toastTimer;
function toast(text, err) {
  const t = $("toast"); t.textContent = text; t.className = "toast on" + (err ? " err" : "");
  clearTimeout(toastTimer); toastTimer = setTimeout(() => t.className = "toast" + (err ? " err" : ""), err ? 6000 : 3200);
}

