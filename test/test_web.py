"""The setup page in a real browser.

- Loads the page (from the repository, or a built site with --site DIR) and fails on
  any script error, error message in the console or missing file.
- Clicks through it in demo mode: tabs, every control, presets, undo, theme, guide.
- Reads test/actions.json with the page's own code, like test_actions.py does with
  wildpad.py, and compares every key, media, mouse, gamepad, axis and light name and
  the settings file of every board between the page and wildpad.py.

Needs Google Chrome and: pip install -r test/requirements.txt
Run: python test/test_web.py [--site DIR]
"""
import functools
import http.server
import json
import sys
import threading
from pathlib import Path

from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import wildpad  # noqa: E402

V = json.loads((ROOT / "test" / "actions.json").read_text(encoding="utf-8"))
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)
        print("FAIL", what)


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def serve(directory):
    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Quiet, directory=str(directory)))
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd


def open_page(browser, url):
    page = browser.new_page()
    problems = []
    page.on("pageerror", lambda e: problems.append(f"script error: {e}"))
    page.on("console", lambda m: m.type == "error" and not m.text.startswith("Failed to load resource")
            and problems.append(f"console: {m.text}"))
    # the page tries a few places for the firmware file, and version.json only exists on
    # the website; only those may be missing
    page.on("response", lambda r: r.url.startswith(url.split("/", 3)[0] + "//" + url.split("/", 3)[2])
            and r.status >= 400 and not r.url.endswith(("wildpad.bin", "version.json"))
            and problems.append(f"{r.status} {r.url}"))
    page.goto(url, wait_until="networkidle")
    return page, problems


def click_through(page):
    check(page.locator(".cap").count() == 3, "the demo pad shows 3 keys")
    check(page.evaluate("fwImage !== null"), "the firmware file is found")
    for tab in ("lighting", "settings", "controls"):
        page.click(f'.tab[data-tab="{tab}"]')
    for i in range(page.locator("#device .cap").count()):
        page.locator("#device .cap").nth(i).click()
    page.click("#knob")
    for preset in page.locator("select[aria-label='Fill this profile with a preset'] option").all_inner_texts()[1:]:
        page.select_option("select[aria-label='Fill this profile with a preset']", label=preset)
    check(not page.is_disabled("#undo"), "presets can be undone")
    page.click("#undo")
    page.click("#themeBtn")
    page.click("#themeBtn")
    page.click("#guideBtn")
    check(page.is_visible("#guide"), "the install guide opens")
    page.click("#guideClose")
    page.click(".tab[data-tab='settings']")
    page.click("#boardChange")
    page.click(".tab[data-tab='controls']")


def check_actions(page):
    r = page.evaluate("""V => {
      const out = { canonical: [], aliases: [], invalid: [], sequences: [], invalid_sequences: [] };
      const parse = t => { try { const a = textToAction(t); return [a.t, a.p1, a.p2]; } catch (e) { return null; } };
      for (const [t, a] of V.canonical) out.canonical.push([t, parse(t), actionToText(A(...a))]);
      for (const [t] of V.aliases) out.aliases.push([t, parse(t)]);
      for (const t of V.invalid) out.invalid.push([t, parse(t)]);
      for (const [t] of V.sequences) {
        const a = textToAction(t);
        out.sequences.push([t, a.t === T.SEQ ? seqBytes(model.seqs.get(a.p1)) : null]);
      }
      for (const t of V.invalid_sequences) out.invalid_sequences.push([t, parse(t)]);
      return out;
    }""", V)
    for (text, a), (_, got, back) in zip(V["canonical"], r["canonical"]):
        check(got == a, f"page reads {text!r} as {got}, expected {a}")
        check(back == text, f"page writes {a} as {back!r}, expected {text!r}")
    for (text, a), (_, got) in zip(V["aliases"], r["aliases"]):
        check(got == a, f"page reads {text!r} as {got}, expected {a}")
    for text, got in r["invalid"] + r["invalid_sequences"]:
        check(got is None, f"page accepts {text!r} as {got}")
    for (text, b), (_, got) in zip(V["sequences"], r["sequences"]):
        check(got == b, f"page stores {text!r} as {got}, expected {b}")


def check_names(page):
    """Every name either side knows, read by both; every code written by both."""
    js = page.evaluate("""() => ({
      key: [...KEY_BY_NAME.keys()], mod: MODS.map(m => m[2]), media: MEDIA.map(m => m[3]),
      mouse: MOUSE.map(m => m[2]), pad: PAD.filter(Boolean).map(p => p[1]),
      axis: AXES.map(a => a[0]), light: LIGHTS.map(l => l[0]) })""")
    py = {"key": list(wildpad.KEYCODES), "mod": list(wildpad.MODIFIERS), "media": list(wildpad.MEDIA),
          "mouse": list(wildpad.MOUSE_NAMES), "pad": wildpad.PAD_NAMES[1:], "axis": wildpad.AXES,
          "light": wildpad.LIGHT_NAMES}
    prefix = {"key": "key:", "mod": "key:", "media": "media:", "mouse": "mouse:", "pad": "pad:",
              "axis": "axis:", "light": "light:"}
    texts = []
    for kind in py:
        check(set(js[kind]) == set(py[kind]),
              f"{kind} names differ: page only {sorted(set(js[kind]) - set(py[kind]))}, "
              f"wildpad.py only {sorted(set(py[kind]) - set(js[kind]))}")
        texts += [prefix[kind] + n for n in set(js[kind]) | set(py[kind])]
    actions = [[wildpad.ACT_KEY, 0, c] for c in range(256)] + [[wildpad.ACT_KEY, m, 0] for m in range(1, 256)]
    actions += [[wildpad.ACT_MEDIA, u & 0xFF, u >> 8] for u in wildpad.MEDIA.values()]
    got = page.evaluate("""([texts, actions]) => [
      texts.map(t => { try { const a = textToAction(t); return [a.t, a.p1, a.p2]; } catch (e) { return null; } }),
      actions.map(a => actionToText(A(...a)))]""", [texts, actions])
    for text, a in zip(texts, got[0]):
        try:
            want = list(wildpad.parse_action(text))
        except wildpad.ActionError:
            want = None
        check(a == want, f"{text!r}: page {a}, wildpad.py {want}")
    for a, text in zip(actions, got[1]):
        check(text == wildpad.format_action(tuple(a)), f"{a}: page {text!r}, wildpad.py {wildpad.format_action(tuple(a))!r}")


def check_files(page):
    """The settings file of every board, both ways, with the factory settings."""
    for board in (1, 2, 3):
        r = page.evaluate("""board => {
          const info = Array.from(boardInfo(board)), m = factory(new Layout(info));
          return { info, bytes: Array.from(m.bytes()), json: m.toJSON() };
        }""", board)
        L = wildpad.Layout(r["info"])
        s = wildpad.Settings(L, bytes(r["bytes"]))
        check(s.to_json() == r["json"], f"board {board}: the page and wildpad.py write different files")
        s2 = wildpad.Settings(L, bytes(r["bytes"]))
        s2.from_json(r["json"])
        check(list(s2.finalize()) == r["bytes"], f"board {board}: wildpad.py reads the page's file differently")
        back = page.evaluate("""([board, j]) => {
          const m = factory(new Layout(Array.from(boardInfo(board)))); m.fromJSON(j); return Array.from(m.bytes());
        }""", [board, s.to_json()])
        check(back == r["bytes"], f"board {board}: the page reads wildpad.py's file differently")


def check_sync_errors(page):
    """Failed writes, commits and saves must keep changes marked as unsaved."""
    results = page.evaluate("""async () => {
      const originalError = console.error;
      console.error = () => {}; // these transport errors are expected
      const results = [];
      let failedCommand, request;
      dev = {
        sendFeatureReport: async (_, bytes) => {
          request = bytes;
          if (bytes[0] === failedCommand) throw new Error("Test USB failure");
        },
        receiveFeatureReport: async () => {
          const reply = new Uint8Array(FLEN);
          reply.set([request[0], request[1], 0, PROTO]);
          return new DataView(reply.buffer);
        }
      };
      try {
        needSave = false;
        schedulePush();
        clearTimeout(pushTimer);
        results.push(["an edit is unsaved before the delayed write", needSave]);
        for (const cmd of [CMD.WRITE, CMD.COMMIT, CMD.SAVE]) {
          failedCommand = cmd;
          needSave = false;
          await push();
          clearTimeout(saveTimer);
          if (cmd === CMD.SAVE) await save();
          const closing = new Event("beforeunload", { cancelable: true });
          window.dispatchEvent(closing);
          results.push([`command ${cmd}: failure stays unsaved`, needSave]);
          results.push([`command ${cmd}: status reports unsaved changes`,
            $("connText").textContent === "Connected · changes not saved"]);
          results.push([`command ${cmd}: closing warns about unsaved changes`, closing.defaultPrevented]);
          results.push([`command ${cmd}: disconnect is available after failure`, !$("disconnectBtn").disabled]);
        }
        failedCommand = null;
        await push();
        clearTimeout(saveTimer);
        await save();
        results.push(["a successful retry clears the unsaved state", !needSave]);
        results.push(["only a successful save reports saved on the pad",
          $("connText").textContent === "Connected · saved on the pad"]);
      } finally {
        clearTimeout(pushTimer);
        clearTimeout(saveTimer);
        clearTimeout(toastTimer);
        dev = null;
        console.error = originalError;
      }
      return results;
    }""")
    for name, ok in results:
        check(ok, name)


def main():
    site = Path(sys.argv[sys.argv.index("--site") + 1]) if "--site" in sys.argv else None
    httpd = serve(site or ROOT)
    url = f"http://127.0.0.1:{httpd.server_address[1]}/" + ("" if site else "web/")
    with sync_playwright() as p:
        browser = p.chromium.launch(channel="chrome")
        page, problems = open_page(browser, url)
        click_through(page)
        check_actions(page)
        check_names(page)
        check_files(page)
        check_sync_errors(page)
        for msg in problems:
            check(False, msg)
        browser.close()
    httpd.shutdown()
    print(f"{url}: " + (f"{len(failures)} failed" if failures else "all page checks passed"))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
