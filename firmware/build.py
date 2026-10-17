"""Build the Wildpad firmware with SDCC.

Usage:  python build.py          -> build/wildpad.bin and .hex
"""
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"
TARGET = "wildpad"

FREQ_SYS = 16000000
XRAM_SIZE = 0x02C0
XRAM_LOC = 0x0140          # endpoint buffers use 0x0000-0x013F
CODE_SIZE = 0x3800          # 14 KiB; the bootloader lives above 0x3800

SOURCES = [
    ROOT / "src" / "main.c",          # must be first: holds main() and sees all ISRs
    ROOT / "src" / "usbdev.c",
    ROOT / "src" / "descr.c",
    ROOT / "src" / "input.c",
    ROOT / "src" / "settings.c",
    ROOT / "include" / "delay.c",
    ROOT / "include" / "neo.c",
]


def find_sdcc():
    exe = shutil.which("sdcc")
    if exe:
        return Path(exe).parent
    for cand in (Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "SDCC" / "bin",
                 Path(r"C:\Program Files (x86)\SDCC\bin")):
        if (cand / "sdcc.exe").exists():
            return cand
    sys.exit("SDCC not found. Install it with:  winget install SDCC.SDCC")


def fix_cc1(bindir: Path) -> Path:
    """SDCC 4.5 for Windows ships its preprocessor backend as 'cc1' without '.exe',
    so sdcpp cannot start it. Build a local toolchain: a copy of bin/ with cc1.exe
    added, plus directory junctions to the original include/ and lib/."""
    if (bindir / "cc1.exe").exists() or not (bindir / "cc1").exists():
        return bindir
    tool = BUILD / "toolchain"
    tbin = tool / "bin"
    # sdcpp looks up "cc1" (no suffix) here and CreateProcess then needs "cc1.exe"
    libexec = tool / "libexec" / "sdcc" / "x86_64-w64-mingw32" / "12.1.0"
    if not (libexec / "cc1.exe").exists():
        print("Preparing local SDCC toolchain (works around missing cc1.exe) ...")
        shutil.copytree(bindir, tbin, dirs_exist_ok=True)
        libexec.mkdir(parents=True, exist_ok=True)
        shutil.copy(bindir / "cc1", libexec / "cc1")
        shutil.copy(bindir / "cc1", libexec / "cc1.exe")
        for dll in bindir.glob("*.dll"):
            shutil.copy(dll, libexec / dll.name)
        for sub in ("include", "lib"):
            link = tool / sub
            if not link.exists():
                subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(bindir.parent / sub)],
                               check=True, capture_output=True)
    return tbin


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    out = (r.stdout + r.stderr).strip()
    if out:
        print(out)
    if r.returncode:
        sys.exit(f"FAILED: {' '.join(map(str, cmd))}")


def ihx_to_bin(ihx: Path, out: Path):
    mem = bytearray([0xFF] * CODE_SIZE)
    top, base = 0, 0
    for line in ihx.read_text().splitlines():
        if not line.startswith(":"):
            continue
        raw = bytes.fromhex(line[1:])
        n, addr, typ = raw[0], (raw[1] << 8) | raw[2], raw[3]
        if typ == 0:
            a = base + addr
            mem[a:a + n] = raw[4:4 + n]
            top = max(top, a + n)
        elif typ == 4:
            base = ((raw[4] << 8) | raw[5]) << 16
    out.write_bytes(bytes(mem[:top]))
    return top


def main():
    BUILD.mkdir(exist_ok=True)
    bindir = fix_cc1(find_sdcc())
    sdcc = str(bindir / "sdcc")
    cflags = ["-mmcs51", "--model-small", "--no-xinit-opt", "--opt-code-size", "--max-allocs-per-node", "50000",
              "--xram-size", str(XRAM_SIZE), "--xram-loc", str(XRAM_LOC),
              "--code-size", str(CODE_SIZE),
              "-I", str(ROOT / "src"), "-I", str(ROOT / "include"),
              f"-DFREQ_SYS={FREQ_SYS}"]
    rels = []
    for src in SOURCES:
        rel = BUILD / (src.stem + ".rel")
        print(f"CC {src.relative_to(ROOT)}")
        run([sdcc, "-c", *cflags, "-o", str(rel), str(src)])
        rels.append(str(rel))
    ihx = BUILD / f"{TARGET}.ihx"
    print("LD", ihx.name)
    run([sdcc, *cflags, "-o", str(ihx), *rels])
    size = ihx_to_bin(ihx, BUILD / f"{TARGET}.bin")
    shutil.copy(ihx, BUILD / f"{TARGET}.hex")

    mem = (BUILD / f"{TARGET}.mem").read_text()
    print(mem.strip().splitlines()[-1] if mem else "")
    stack = re.search(r"Stack starts at: 0x(\w+) \(sp set to 0x\w+\) with (\d+) bytes", mem)
    print(f"FLASH: {size} / {CODE_SIZE} bytes ({100 * size // CODE_SIZE}%)")
    if stack:
        print(f"STACK: {stack.group(2)} bytes free")
    # Statics in XRAM must start at zero; SDCC only links that start-up code on request
    # (see requestXramClear() in main.c). Real RAM holds junk after reset.
    if "crtxclear.rel" not in (BUILD / f"{TARGET}.map").read_text():
        sys.exit("FAILED: start-up code that clears XRAM is not linked")
    print(f"OK -> {BUILD / (TARGET + '.bin')}")


if __name__ == "__main__":
    main()
