"""Release files. Run firmware/build.py first.

  python tools/package.py [--release TAG]
        dist/wildpad-VERSION.zip (wildpad-dev.zip without --release): firmware,
        flasher, setup page, command-line tool, launchers, docs
  python tools/package.py --site DIR [--release TAG]
        the website (GitHub Pages): the setup page next to the firmware it installs
  python tools/package.py --notes TAG
        checks the tag against the firmware (built first) and the previous release,
        and prints the line the release notes get about the firmware
  python tools/package.py --version
        print the firmware version

Release tags start with the firmware version (FW_MAJOR/MINOR/PATCH in
firmware/src/config.h): v0.2.0 is the first release with firmware 0.2.0. A fourth
number marks a release with the same firmware and a newer setup page or tools:
v0.2.0.1, v0.2.0.2, ... Pre-releases add a suffix: v0.3.0-beta.1.
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
TAG = re.compile(r"v((\d+\.\d+\.\d+)(?:\.(\d+))?(?:-[0-9A-Za-z.-]+)?)")   # version, firmware, revision


def version(cfg=None):
    cfg = cfg or (ROOT / "firmware" / "src" / "config.h").read_text()
    parts = [re.search(rf"#define FW_{k}\s+(\d+)", cfg).group(1) for k in ("MAJOR", "MINOR", "PATCH")]
    return ".".join(parts)


def parse_tag(tag):
    m = TAG.fullmatch(tag)
    if not m or (m[3] or "").startswith("0"):              # the fourth number counts from 1
        sys.exit(f"release tag {tag!r} isn't vFIRMWARE or vFIRMWARE.N, e.g. v0.2.0, v0.2.0.1 or v0.3.0-beta.1")
    return m


def release_version(tag):
    return parse_tag(tag)[1]


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
    """The firmware line of the release notes. The tag must name the firmware it
    carries, and two different firmwares never carry the same version: the previous
    release is built and compared byte for byte."""
    m = parse_tag(tag)
    fw, revision = version(), m[3]
    if m[2] != fw:
        sys.exit(f"{tag} names firmware {m[2]}, but firmware/src/config.h says {fw}: "
                 f"tag it v{fw} (new firmware) or v{fw}.N (same firmware, newer page or tools)")
    prev = git("describe", "--tags", "--abbrev=0", "--match", "v[0-9]*", "--exclude", "v*-*",   # not pre-releases
               f"{tag}^").stdout.strip()
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
                 "raise FW_MAJOR/MINOR/PATCH in firmware/src/config.h and tag the new version")
    if revision is None and same:
        sys.exit(f"the firmware is the same as in {prev}: tag this release v{fw}.N, "
                 "or raise the firmware version if it should be a new one")
    if revision is not None and not same:
        sys.exit(f"{tag} says the firmware is unchanged, but it differs from {prev}")
    if same:
        return f"**Firmware {fw}**, the same as in {prev}: no need to update the pad."
    return f"**Firmware {fw}** (new)"


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
