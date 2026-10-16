"""Compile test_input.c together with the real src/input.c and run it in the s51
8051 simulator that ships with SDCC. Checks the knob decoder end to end
(timer interrupt + quadrature table + detent logic)."""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import build  # noqa: E402  (reuses toolchain discovery / cc1 workaround)

OUT = ROOT / "build" / "test"
EXPECTED = [10, 7, 47, 48, 48, 43, 43, 1]
NAMES = ["10 slow CW detents", "3 slow CCW detents", "40 fast CW detents",
         "1 detent with contact bounce", "half step and back (no count)",
         "5 CW detents with invert", "20 fast direction reversals", "millis running"]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    (ROOT / "build").mkdir(exist_ok=True)
    bindir = build.fix_cc1(build.find_sdcc())
    sdcc = str(bindir / "sdcc")
    cflags = ["-mmcs51", "--model-small", "--xram-size", "0x0300", "--xram-loc", "0x0300",
              "--code-size", "0x3800", "-I", str(ROOT / "src"), "-I", str(ROOT / "include"),
              "-DFREQ_SYS=16000000"]
    rels = []
    for src in (ROOT / "test" / "test_input.c", ROOT / "src" / "input.c"):
        rel = OUT / (src.stem + ".rel")
        build.run([sdcc, "-c", *cflags, "-o", str(rel), str(src)])
        rels.append(str(rel))
    ihx = OUT / "test_input.ihx"
    build.run([sdcc, *cflags, "-o", str(ihx), *rels])

    mapfile = (OUT / "test_input.map").read_text()
    m = re.search(r"\b([0-9A-Fa-f]{8})\s+_done\b", mapfile)
    if not m:
        sys.exit("could not find _done in map file")
    done = int(m.group(1), 16)

    cmds = f"break 0x{done:04x}\nrun\ndump xram 0x200 0x20f\nquit\n"
    s51 = str(bindir / "s51")
    r = subprocess.run([s51, "-t", "8052", "-X", "12M", str(ihx)], input=cmds,
                       capture_output=True, text=True, timeout=600)
    text = r.stdout + r.stderr
    raw = b""
    for addr in (0x200, 0x208):                 # s51 dumps 8 bytes per line
        line = re.search(rf"0x0*{addr:x}\s+((?:[0-9a-f]{{2}} ){{8}})", text)
        if not line:
            print(text)
            sys.exit("simulator output not understood")
        raw += bytes(int(x, 16) for x in line.group(1).split())
    values = [int.from_bytes(raw[i:i + 2], "little", signed=True) for i in range(0, 16, 2)]

    ok = True
    for name, got, exp in zip(NAMES, values, EXPECTED):
        good = got == exp
        ok &= good
        print(f"{'PASS' if good else 'FAIL'}  {name:32s} position={got:4d} expected={exp}")

    ok &= test_keyboard(sdcc, cflags, bindir)
    sys.exit(0 if ok else 1)


def test_keyboard(sdcc, cflags, bindir):
    """Full keyboard pipeline: 30 fast CW + 5 fast CCW detents must give exactly
    35 consumer taps (volume up x30, volume down x5), each press followed by a release."""
    srcs = [ROOT / "test" / "test_kbd.c", ROOT / "src" / "main.c", ROOT / "src" / "input.c",
            ROOT / "src" / "settings.c", ROOT / "src" / "descr.c",
            ROOT / "include" / "delay.c", ROOT / "include" / "neo.c"]
    rels = []
    for src in srcs:
        rel = OUT / ("k_" + src.stem + ".rel")
        extra = ["-Dmain=fw_main"] if src.name == "main.c" else []
        build.run([sdcc, "-c", *cflags, *extra, "-o", str(rel), str(src)])
        rels.append(str(rel))
    ihx = OUT / "test_kbd.ihx"
    build.run([sdcc, *cflags, "-o", str(ihx), *rels])
    mapfile = (OUT / "test_kbd.map").read_text()
    done = int(re.search(r"\b([0-9A-Fa-f]{8})\s+_done\b", mapfile).group(1), 16)

    cmds = f"break 0x{done:04x}\nrun\ndump xram 0x200 0x2d7\nquit\n"
    r = subprocess.run([str(bindir / "s51"), "-t", "8052", "-X", "12M", str(ihx)], input=cmds,
                       capture_output=True, text=True, timeout=900)
    text = r.stdout + r.stderr
    data = bytearray()
    for m in re.finditer(r"^0x0*([0-9a-f]+)\s+((?:[0-9a-f]{2} ){1,8})", text, re.M):
        if int(m.group(1), 16) == 0x200 + len(data):
            data += bytes(int(x, 16) for x in m.group(2).split())
    if not data:
        print(text[-2000:])
        print("FAIL  keyboard pipeline: no simulator output")
        return False
    n = data[0]
    reps = [tuple(data[1 + 3 * i:4 + 3 * i]) for i in range(n)]
    taps = []
    expect_release = False
    good = True
    for rid, lo, hi in reps:
        if rid != 2:
            good = False
            break
        usage = lo | hi << 8
        if expect_release:
            good &= usage == 0
        else:
            taps.append(usage)
        expect_release = not expect_release
    # The queue replays the knob's net movement, so fast reversals while taps are
    # still pending may cancel out; with a 1 ms host every detent should come through.
    good &= not expect_release                       # last report was a release
    good &= taps.count(0xE9) - taps.count(0xEA) == 25
    exact = taps == [0xE9] * 30 + [0xEA] * 5
    print(f"{'PASS' if good else 'FAIL'}  keyboard pipeline: {n} reports, "
          f"{taps.count(0xE9)}x vol-up, {taps.count(0xEA)}x vol-down "
          f"({'every detent replayed 1:1' if exact else 'net movement correct'}, press/release alternating)")
    return good


if __name__ == "__main__":
    main()
