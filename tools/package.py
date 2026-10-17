"""Create the release zip: dist/wildpad-<version>.zip

Contains the built firmware, the flasher, the setup page, the command-line tool,
the launcher scripts and the documentation. Run firmware/build.py first.
"""
import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def version():
    cfg = (ROOT / "firmware" / "src" / "config.h").read_text()
    parts = [re.search(rf"#define FW_{k}\s+(\d+)", cfg).group(1) for k in ("MAJOR", "MINOR", "PATCH")]
    return ".".join(parts)


def main():
    fw = ROOT / "firmware" / "build" / "wildpad.bin"
    if not fw.exists():
        sys.exit("build the firmware first: python firmware/build.py")
    ver = version()
    files = {
        fw: "firmware/wildpad.bin",
        ROOT / "flash.bat": "flash.bat",
        ROOT / "configure.bat": "configure.bat",
        ROOT / "README.md": "README.md",
        ROOT / "LICENSE": "LICENSE",
        ROOT / "firmware" / "LICENSE": "firmware/LICENSE",
    }
    for name in ("flash.py", "winusb.py", "wildpad.py", "configurator.html", "find_python.cmd"):
        files[ROOT / "tools" / name] = f"tools/{name}"
    for doc in (ROOT / "docs").iterdir():
        files[doc] = f"docs/{doc.name}"

    out = ROOT / "dist" / f"wildpad-{ver}.zip"
    out.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for src, arc in files.items():
            z.write(src, f"wildpad-{ver}/{arc}")
    print(f"{out} ({out.stat().st_size // 1024} KiB, {len(files)} files)")


if __name__ == "__main__":
    main()
