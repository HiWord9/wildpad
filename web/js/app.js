// =============================================================================== wiring
document.querySelectorAll(".tab").forEach(t => t.addEventListener("click", () => {
  tab = t.dataset.tab;
  document.querySelectorAll(".tab").forEach(x => x.setAttribute("aria-selected", x === t ? "true" : "false"));
  ["controls", "lighting", "settings"].forEach(v => $("view-" + v).classList.toggle("hidden", v !== tab));
  recording = false;
  render();
}));
$("connect").addEventListener("click", connect);
$("bannerBtn").addEventListener("click", connect);
$("disconnectBtn").addEventListener("click", async () => {
  if (dev) {
    try { await dev.close(); } catch {}
    detach();
  }
});
$("themeBtn").addEventListener("click", toggleTheme);
initTheme();
try {
  window.matchMedia("(prefers-color-scheme: dark)").addEventListener("change", () => {
    if (!localStorage.getItem("wildpad-theme")) updateThemeIcon();
  });
} catch {}
$("undo").addEventListener("click", undo);
$("redo").addEventListener("click", redo);
$("addCombo").addEventListener("click", () => addCombo());
$("exportBtn").addEventListener("click", () => {
  const blob = new Blob([JSON.stringify(model.toJSON(), null, 2)], { type: "application/json" });
  const a = h("a", { href: URL.createObjectURL(blob), download: "wildpad-settings.json" });
  document.body.append(a); a.click(); a.remove(); setTimeout(() => URL.revokeObjectURL(a.href), 1000);
});
$("importBtn").addEventListener("click", () => $("importFile").click());
$("importFile").addEventListener("change", async e => {
  const f = e.target.files[0]; e.target.value = "";
  if (!f) return;
  try {
    const j = JSON.parse(await f.text());
    const next = model.clone(); next.fromJSON(j);
    change(m => m.load(next.bytes()), "import");
    toast("Settings loaded from " + f.name);
  } catch (err) { toast("Couldn't load that file: " + err.message, true); }
});
$("factoryBtn").addEventListener("click", () => $("factoryConfirm").classList.remove("hidden"));
$("factoryNo").addEventListener("click", () => $("factoryConfirm").classList.add("hidden"));
$("factoryYes").addEventListener("click", () => {
  $("factoryConfirm").classList.add("hidden");
  change(m => m.load(factory(L, m).bytes()), "factory");
  toast("Factory settings restored");
});
$("fwUpdate").addEventListener("click", updateFirmware);
$("guideBtn").addEventListener("click", () => openGuide());
$("guideLink").addEventListener("click", e => { e.preventDefault(); openGuide(); });
$("guideClose").addEventListener("click", () => $("guide").close());
$("guideBack").addEventListener("click", () => { guide.step--; renderGuide(); });
$("guideNext").addEventListener("click", () => { if (guide.step === guideSteps().length - 1) $("guide").close(); else { guide.step++; renderGuide(); } });
$("fwPick").addEventListener("click", () => $("fwFile").click());
$("fwFile").addEventListener("change", async e => {
  const f = e.target.files[0]; e.target.value = "";
  if (!f) return;
  const data = new Uint8Array(await f.arrayBuffer());
  if (!validFirmware(data)) { toast("That isn't a Wildpad firmware file", true); return; }
  fwImage = { data, name: f.name };
  $("fwSource").textContent = `${f.name}, ${(data.length / 1024).toFixed(1)} KB`;
});
loadFirmware();
bindSettings();

if ("hid" in navigator) {
  navigator.hid.addEventListener("disconnect", e => { if (e.device === dev) detach(); });
  navigator.hid.addEventListener("connect", e => {
    if (!dev && e.device.collections.some(c => c.usagePage === PAGE)) setTimeout(() => attach(e.device), 300);
  });
  navigator.hid.getDevices().then(list => {
    const d = list.find(x => x.vendorId === VID && x.productId === PID && x.collections.some(c => c.usagePage === PAGE));
    if (d) attach(d);
  });
}
window.addEventListener("beforeunload", e => { if (dev && (needSave || pushing)) { e.preventDefault(); e.returnValue = ""; } });
setSync("off");
render();
