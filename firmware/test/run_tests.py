"""Firmware tests in the s51 8051 simulator that ships with SDCC.

The real firmware sources are compiled together with small test harnesses
(test_*.c) that stub out USB, log every report and drive the knob / keys from a
timer interrupt. Run:  python firmware/test/run_tests.py
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import build  # noqa: E402  (reuses toolchain discovery / cc1 workaround)

OUT = ROOT / "build" / "test"
SRC, INC, TEST = ROOT / "src", ROOT / "include", ROOT / "test"
FIRMWARE = [SRC / "main.c", SRC / "input.c", SRC / "settings.c", SRC / "descr.c",
            INC / "delay.c", INC / "neo.c"]

results = []


def check(cond, name, detail=""):
    results.append(bool(cond))
    print(f"{'PASS' if cond else 'FAIL'}  {name}" + (f"  ({detail})" if detail else ""))


def simulate(tag, sources, dump_from, dump_len, defines=None):
    """Build `sources` into one image, run until _done, return xram[dump_from:+dump_len]."""
    defines = defines or {}
    bindir = build.fix_cc1(build.find_sdcc())
    sdcc = str(bindir / "sdcc")
    # variables above the harness log area; endpoint buffers stay at 0x0000-0x013F
    cflags = ["-mmcs51", "--model-small", "--xram-size", "0x0A00", "--xram-loc", "0x0600",
              "--code-size", "0x3800", "-I", str(SRC), "-I", str(INC), "-DFREQ_SYS=16000000"]
    rels = []
    for src in sources:
        rel = OUT / f"{tag}_{src.stem}.rel"
        extra = [f"-D{d}" for d in defines.get(src.name, [])]
        build.run([sdcc, "-c", *cflags, *extra, "-o", str(rel), str(src)])
        rels.append(str(rel))
    ihx = OUT / f"{tag}.ihx"
    build.run([sdcc, *cflags, "-o", str(ihx), *rels])
    done = int(re.search(r"\b([0-9A-Fa-f]{8})\s+_done\b", (OUT / f"{tag}.map").read_text()).group(1), 16)

    end = dump_from + dump_len - 1
    # Real RAM is not zero after reset; fill it with junk so code that relies on
    # uninitialised variables fails here too (the start-up code must clear them).
    junk = "fill xram 0 0xfff 0xa5\nfill iram 8 0xff 0x5a\n"
    cmds = junk + f"break 0x{done:04x}\nrun\ndump xram 0x{dump_from:x} 0x{end:x}\nquit\n"
    r = subprocess.run([str(bindir / "s51"), "-t", "8052", "-X", "12M", str(ihx)], input=cmds,
                       capture_output=True, text=True, timeout=900)
    text = r.stdout + r.stderr
    data = bytearray()
    for m in re.finditer(r"^0x0*([0-9a-f]+)\s+((?:[0-9a-f]{2} ){1,8})", text, re.M):
        if int(m.group(1), 16) == dump_from + len(data):
            data += bytes(int(x, 16) for x in m.group(2).split())
    if len(data) < dump_len:
        print(text[-1500:])
        raise RuntimeError(f"{tag}: simulator output not understood")
    return bytes(data[:dump_len])


def test_decoder():
    raw = simulate("dec", [TEST / "test_input.c", SRC / "input.c"], 0x200, 16)
    values = [int.from_bytes(raw[i:i + 2], "little", signed=True) for i in range(0, 16, 2)]
    expected = [10, 7, 47, 48, 48, 43, 43, 1]
    names = ["10 slow CW detents", "3 slow CCW detents", "40 fast CW detents",
             "1 detent with contact bounce", "half step and back (no count)",
             "5 CW detents with invert", "20 fast direction reversals", "millis running"]
    for name, got, exp in zip(names, values, expected):
        check(got == exp, f"decoder: {name}", f"position {got}, expected {exp}")


def test_keys():
    raw = simulate("keys", [TEST / "test_keys.c", *FIRMWARE], 0x300, 4 + 5 * 100,
                   {"main.c": ["main=fw_main"]})
    n, brightness, profile, personality = raw[0], raw[1], raw[2], raw[3]
    reps = [raw[4 + 5 * i:9 + 5 * i] for i in range(n)]
    con = [r[1] | r[2] << 8 for r in reps if r[0] == 2]
    kbd = [(r[1], r[2]) for r in reps if r[0] == 1]
    wheel = [int.from_bytes(bytes([r[2]]), "little", signed=True) for r in reps if r[0] == 3]

    check(personality == 0, "keys: no gamepad action -> plain keyboard USB layout")
    knob = con[:70]
    check(len(knob) == 70 and all(u == 0 for u in knob[1::2]),
          "keys: every knob click is a press + release pair")
    taps = knob[0::2]
    check(taps == [0xE9] * 30 + [0xEA] * 5, "keys: 30 fast CW + 5 CCW clicks -> 30 volume up, 5 down",
          f"{taps.count(0xE9)} up, {taps.count(0xEA)} down")
    check(con[70:72] == [0xE2, 0], "keys: short knob press -> mute on release",
          f"got {[hex(u) for u in con[70:72]]}")
    check(brightness == 128 + 3 * 16 and len(con) == 72,
          "keys: hold knob + 3 clicks -> brightness up 3 steps, no volume, no mute",
          f"brightness {brightness}, extra media reports {len(con) - 72}")
    check(profile == 1, "keys: hold knob + key 2 -> profile 2", f"profile index {profile}")
    check(kbd[:2] == [(1, 0x06), (0, 0)], "keys: key 1 in profile 2 -> Ctrl+C", f"{kbd[:2]}")
    check(sum(wheel) == -1, "keys: knob click in profile 2 -> one notch of scrolling down",
          f"wheel {wheel}")


def test_gamepad():
    raw = simulate("pad", [TEST / "test_gamepad.c", *FIRMWARE], 0x300, 0x13 + 6 * 60,
                   {"main.c": ["main=fw_main"], "settings.c": ["SET_load=SET_load_real"]})
    n, nkeys, personality = raw[0], raw[1], raw[2]
    keys = list(raw[3:3 + nkeys])
    logs = [raw[0x13 + 6 * i:0x13 + 6 * i + 6] for i in range(n)]
    lx = [int.from_bytes(r[0:2], "little", signed=True) for r in logs]
    check(personality == 1, "gamepad: an axis action adds the gamepad (keyboard + XInput layout)")
    trunc = lambda k: int(32767 * k / 10)          # C integer division truncates toward 0
    check(lx[1:11] == [trunc(k) for k in range(1, 11)],
          "gamepad: 10 CW clicks -> 10 % steps up to full right lock", f"{lx[1:11]}")
    check(lx[11:31] == [trunc(k) for k in range(9, -11, -1)],
          "gamepad: 3 extra CW ignored (hard stop), 20 CCW -> full left lock",
          f"{lx[11:14]}...{lx[29:31]}")
    check(lx[31] == 0, "gamepad: knob press -> center", f"LX {lx[31]}")
    rest = logs[32:]
    rt = [r[5] for r in rest]
    check(rt[:2] == [255, 0], "gamepad: short press of key 1 -> RT tap", f"RT {rt}")
    check(any(r[3] & 0x10 for r in rest), "gamepad: key 2 -> A button")
    check(all(v == 0 for v in lx[31:]), "gamepad: knob turned while key 3 held -> stick stays centered",
          f"LX after center {sorted(set(lx[31:]))}")
    check(keys[:4] == [0x05, 0, 0x05, 0], "gamepad: hold key 3 + 2 clicks -> 'b' twice, no 'a'",
          f"keys {[hex(k) for k in keys]}")
    check(keys[4:6] == [0x04, 0], "gamepad: short press of key 3 -> 'a'")
    check(keys[6:8] == [0x06, 0] and rt.count(255) == 1, "gamepad: long press of key 1 -> 'c', no RT")


def test_switch():
    raw = simulate("sw", [TEST / "test_switch.c", *FIRMWARE], 0x300, 1 + 2 * 30,
                   {"main.c": ["main=fw_main"], "settings.c": ["SET_load=SET_load_real"]})
    reps = [(raw[1 + 2 * i], raw[2 + 2 * i]) for i in range(raw[0])]
    alt, shift, tab = 0x04, 0x02, 0x2B
    a = [(alt, tab), (alt, 0), (alt, tab), (alt, 0), (alt, tab), (alt, 0), (alt | shift, tab), (alt, 0), (0, 0)]
    b = [(alt, tab), (alt, 0), (alt, tab), (alt, 0), (alt | shift, tab), (alt, 0), (0, 0)]
    fmt = lambda r: [(hex(m), hex(k)) for m, k in r]
    check(reps[:9] == a, "window switching: knob press = Alt+Tab with Alt held, turn = Tab / Shift+Tab",
          f"{fmt(reps[:9])}")
    check(reps[9:] == b, "window switching: turning alone holds Alt between clicks, lets go when it stops",
          f"{fmt(reps[9:])}")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    test_decoder()
    test_keys()
    test_gamepad()
    test_switch()
    print(f"\n{sum(results)}/{len(results)} passed")
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
