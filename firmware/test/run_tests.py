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
    cmds = f"break 0x{done:04x}\nrun\ndump xram 0x{dump_from:x} 0x{end:x}\nquit\n"
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


def test_keyboard():
    raw = simulate("kbd", [TEST / "test_kbd.c", *FIRMWARE], 0x300, 1 + 5 * 100,
                   {"main.c": ["main=fw_main"]})
    n = raw[0]
    reps = [raw[1 + 5 * i:6 + 5 * i] for i in range(n)]
    con = [r for r in reps if r[0] == 2]
    mouse = [r for r in reps if r[0] == 3]
    usages = [r[1] | r[2] << 8 for r in con]

    # A: 35 volume taps, each followed by a release
    knob = usages[:70]
    taps = knob[0::2]
    check(all(u == 0 for u in knob[1::2]) and len(knob) == 70,
          "keyboard: every knob step is a press + release pair")
    check(taps == [0xE9] * 30 + [0xEA] * 5, "keyboard: 30 fast CW + 5 CCW -> 30 vol-up, 5 vol-down",
          f"{taps.count(0xE9)} up, {taps.count(0xEA)} down")
    # B: short press of key 2 -> play/pause tap (sent on release)
    check(usages[70:72] == [0xCD, 0], "keyboard: short press of key 2 -> play/pause tap",
          f"got {[hex(u) for u in usages[70:72]]}")
    # C: hold key 2 (Fn) + 3 CW detents -> 3 notches of scroll, no volume
    scroll = sum(int.from_bytes(bytes([r[4]]), "little", signed=True) for r in mouse)
    check(scroll == 3 and len(usages) == 72, "keyboard: Fn (hold key 2) + 3 detents -> 3 scroll notches",
          f"scroll {scroll}, extra consumer reports {len(usages) - 72}")


def test_wheel():
    raw = simulate("whl", [TEST / "test_wheel.c", *FIRMWARE], 0x300, 4 + 6 * 90,
                   {"main.c": ["main=fw_main"], "settings.c": ["SET_load=SET_load_real"]})
    n, personality, key = raw[0], raw[2], raw[3]
    logs = [raw[4 + 6 * i:10 + 6 * i] for i in range(n)]
    lx = [int.from_bytes(r[0:2], "little", signed=True) for r in logs]
    check(personality == 1, "wheel: runs as the composite keyboard + XInput device")
    trunc = lambda k: int(32767 * k / 10)          # C integer division truncates toward 0
    steps = [trunc(k) for k in range(1, 11)]
    check(lx[1:11] == steps, "wheel: 10 CW detents -> 10 % steps up to full right lock",
          f"{lx[1:11]}")
    left = lx[11:31]
    check(left == [trunc(k) for k in range(9, -11, -1)],
          "wheel: 3 extra CW ignored (hard stop), 20 CCW -> full left lock", f"{left[:3]}...{left[-2:]}")
    check(lx[31] == 0, "wheel: knob press re-centers", f"LX {lx[31]}")
    rt = [r[5] for r in logs[32:]]
    a_btn = [r[3] & 0x10 for r in logs[32:]]
    check(255 in rt, "wheel: key 1 -> right trigger")
    check(any(a_btn), "wheel: key 2 -> A button")
    check(key == 0x04, "wheel: key 3 -> keyboard 'a' while in wheel mode", f"keycode 0x{key:02x}")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    test_decoder()
    test_keyboard()
    test_wheel()
    print(f"\n{sum(results)}/{len(results)} passed")
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
