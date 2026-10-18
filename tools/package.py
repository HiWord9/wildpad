"""Release files. Run firmware/build.py first.

  python tools/package.py              dist/wildpad-<version>.zip: firmware, flasher,
                                       setup page, command-line tool, launchers, docs
  python tools/package.py --site DIR   the website (GitHub Pages): the setup page as
                                       index.html next to the firmware it installs
  python tools/package.py --version    print the firmware version
"""
import json
import re
import shutil
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def version():
    cfg = (ROOT / "firmware" / "src" / "config.h").read_text()
    parts = [re.search(rf"#define FW_{k}\s+(\d+)", cfg).group(1) for k in ("MAJOR", "MINOR", "PATCH")]
    return ".".join(parts)


def site(out, fw, ver):
    out.mkdir(parents=True, exist_ok=True)
    shutil.copy(ROOT / "tools" / "configurator.html", out / "index.html")
    shutil.copy(fw, out / "wildpad.bin")
    shutil.copy(ROOT / "tools" / "60-wildpad.rules", out / "60-wildpad.rules")
    (out / "docs").mkdir(exist_ok=True)
    shutil.copy(ROOT / "docs" / "r12_short.jpeg", out / "docs" / "r12_short.jpeg")
    (out / "version.json").write_text(json.dumps({"firmware": ver}))
    print(f"site in {out}")


def main():
    if "--version" in sys.argv:
        print(version())
        return
    fw = ROOT / "firmware" / "build" / "wildpad.bin"
    if not fw.exists():
        sys.exit("build the firmware first: python firmware/build.py")
    ver = version()
    if "--site" in sys.argv:
        site(Path(sys.argv[sys.argv.index("--site") + 1]), fw, ver)
        return
    files = {
        fw: "firmware/wildpad.bin",
        ROOT / "flash.bat": "flash.bat",
        ROOT / "configure.bat": "configure.bat",
        ROOT / "requirements.txt": "requirements.txt",
        ROOT / "README.md": "README.md",
        ROOT / "LICENSE": "LICENSE",
        ROOT / "firmware" / "LICENSE": "firmware/LICENSE",
    }
    for name in ("flash.py", "winusb.py", "wildpad.py", "configurator.html", "find_python.cmd", "60-wildpad.rules"):
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
