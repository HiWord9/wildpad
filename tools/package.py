"""Release files. Run firmware/build.py first.

  python tools/package.py [--release vX.Y.Z]
        dist/wildpad-X.Y.Z.zip (wildpad-dev.zip without --release): firmware,
        flasher, setup page, command-line tool, launchers, docs
  python tools/package.py --site DIR [--release vX.Y.Z]
        the website (GitHub Pages): the setup page next to the firmware it installs
  python tools/package.py --notes vX.Y.Z
        builds the previous release's firmware and compares it with this one (built
        first): stops if they differ but carry the same version, otherwise prints the
        line the release notes get about the firmware
  python tools/package.py --version
        print the firmware version

A release (tag vX.Y.Z) is Wildpad as a whole. The firmware has its own version,
FW_MAJOR/MINOR/PATCH in firmware/src/config.h, that changes only when the firmware
does, so a release that only changes the setup page or the tools leaves it alone.
"""
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TAG = re.compile(r"v(\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?)")


def version(cfg=None):
    cfg = cfg or (ROOT / "firmware" / "src" / "config.h").read_text()
    parts = [re.search(rf"#define FW_{k}\s+(\d+)", cfg).group(1) for k in ("MAJOR", "MINOR", "PATCH")]
    return ".".join(parts)


def release_version(tag):
    m = TAG.fullmatch(tag)
    if not m:
        sys.exit(f"release tag {tag!r} isn't vMAJOR.MINOR.PATCH, e.g. v0.2.0 or v0.2.0-beta.1")
    return m[1]


def drop_tree(tree):
    """Remove the checkout of the previous release. On Windows its build has
    junctions into the SDCC install (firmware/build.py): unlink those first, so
    nothing behind them is touched."""
    for sub in ("include", "lib"):
        link = tree / "firmware" / "build" / "toolchain" / sub
        if os.path.isjunction(link) or link.is_symlink():
            os.rmdir(link) if os.path.isjunction(link) else link.unlink()
    git("worktree", "remove", "--force", str(tree))
    shutil.rmtree(tree, ignore_errors=True)
    git("worktree", "prune")


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)


def notes(tag, fw_bin):
    """The firmware line of the release notes. Two different firmwares never carry the
    same version: the previous release is built and compared byte for byte."""
    release_version(tag)
    fw = version()
    prev = git("describe", "--tags", "--abbrev=0", "--match", "v[0-9]*", f"{tag}^").stdout.strip()
    if not prev:
        return f"**Firmware {fw}**"
    tree = ROOT / ".ci" / "previous"
    drop_tree(tree)
    if git("worktree", "add", "--detach", str(tree), prev).returncode:
        sys.exit(f"could not check out {prev}")
    try:
        if subprocess.run([sys.executable, "firmware/build.py"], cwd=tree, capture_output=True).returncode:
            sys.exit(f"could not build the firmware of {prev}")
        same = (tree / "firmware" / "build" / "wildpad.bin").read_bytes() == fw_bin.read_bytes()
        prev_fw = version((tree / "firmware" / "src" / "config.h").read_text())
    finally:
        drop_tree(tree)
    if not same and fw == prev_fw:
        sys.exit(f"the firmware changed since {prev} but its version is still {fw}: "
                 "raise FW_MAJOR/MINOR/PATCH in firmware/src/config.h")
    return f"**Firmware {fw}**" + (f" (the same as in {prev})" if same else "")


def site(out, fw, ver, release):
    out.mkdir(parents=True, exist_ok=True)
    shutil.copytree(ROOT / "web", out, dirs_exist_ok=True)
    shutil.copy(fw, out / "wildpad.bin")
    (out / "docs").mkdir(exist_ok=True)
    shutil.copy(ROOT / "docs" / "r12_short.jpeg", out / "docs" / "r12_short.jpeg")
    (out / "version.json").write_text(json.dumps({"release": release, "firmware": ver}))
    print(f"site in {out}")


def main():
    arg = lambda name: sys.argv[sys.argv.index(name) + 1] if name in sys.argv else None
    if "--version" in sys.argv:
        print(version())
        return
    release = release_version(arg("--release")) if arg("--release") else "dev"
    fw = ROOT / "firmware" / "build" / "wildpad.bin"
    if not fw.exists():
        sys.exit("build the firmware first: python firmware/build.py")
    if arg("--notes"):
        print(notes(arg("--notes"), fw))
        return
    ver = version()
    if arg("--site"):
        site(Path(arg("--site")), fw, ver, release)
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
    for name in ("flash.py", "winusb.py", "wildpad.py", "find_python.cmd"):
        files[ROOT / "tools" / name] = f"tools/{name}"
    for f in (ROOT / "web").rglob("*"):
        if f.is_file():
            files[f] = f.relative_to(ROOT).as_posix()
    for doc in (ROOT / "docs").iterdir():
        files[doc] = f"docs/{doc.name}"

    out = ROOT / "dist" / f"wildpad-{release}.zip"
    out.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for src, arc in files.items():
            z.write(src, f"wildpad-{release}/{arc}")
    print(f"{out} ({out.stat().st_size // 1024} KiB, {len(files)} files, firmware {ver})")


if __name__ == "__main__":
    main()
