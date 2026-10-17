"""Static checks of the USB descriptors inside the built firmware image.
Reads build/wildpad.bin + .map, walks every descriptor and parses the HID report
descriptors to confirm report sizes match what the firmware sends."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = (ROOT / "build" / "wildpad.bin").read_bytes()
MAP = (ROOT / "build" / "wildpad.map").read_text()
errors = []


def sym(name):
    m = re.search(rf"\b([0-9A-F]{{8}})\s+_{name}\b", MAP)
    if not m:
        sys.exit(f"symbol {name} not found")
    return int(m.group(1), 16)


def blob(name, n):
    a = sym(name)
    return BIN[a:a + n]


def check(cond, msg):
    if not cond:
        errors.append(msg)
    print(("ok   " if cond else "FAIL ") + msg)


def walk_config(cfg, label):
    total = cfg[2] | cfg[3] << 8
    check(total == len(cfg), f"{label}: wTotalLength {total} == {len(cfg)}")
    i, itfs, eps, bad = 0, [], [], False
    while i < len(cfg):
        ln, typ = cfg[i], cfg[i + 1]
        bad |= not (ln >= 2 and i + ln <= len(cfg))
        if typ == 4:
            itfs.append(tuple(cfg[i + 2:i + 9]))
        if typ == 5:
            eps.append((cfg[i + 2], cfg[i + 4] | cfg[i + 5] << 8))
        i += max(ln, 1)
    check(not bad, f"{label}: every descriptor length fits")
    check(len(itfs) == cfg[4], f"{label}: {len(itfs)} interfaces == bNumInterfaces {cfg[4]}")
    return itfs, eps


def parse_hid(desc, label):
    """Return {(kind, report_id): bits} for input/output/feature reports."""
    sizes, i, depth = {}, 0, 0
    gl, stack = {"size": 0, "count": 0, "id": 0}, []
    while i < len(desc):
        b = desc[i]
        n = [0, 1, 2, 4][b & 3]
        val = int.from_bytes(desc[i + 1:i + 1 + n], "little")
        tag, typ = b & 0xFC, (b >> 2) & 3
        if typ == 0:
            if tag in (0x80, 0x90, 0xB0):
                k = ({0x80: "in", 0x90: "out", 0xB0: "feat"}[tag], gl["id"])
                sizes[k] = sizes.get(k, 0) + gl["size"] * gl["count"]
            elif tag == 0xA0:
                depth += 1
            elif tag == 0xC0:
                depth -= 1
        elif typ == 1:
            if tag == 0x74: gl["size"] = val
            elif tag == 0x94: gl["count"] = val
            elif tag == 0x84: gl["id"] = val
            elif tag == 0xA4: stack.append(dict(gl))
            elif tag == 0xB4: gl = stack.pop()
        i += 1 + n
    check(depth == 0, f"{label}: collections balanced")
    return sizes


def ids(dev):
    return (dev[8] | dev[9] << 8, dev[10] | dev[11] << 8, dev[12] | dev[13] << 8)


# ---------------------------------------------------------------- device descriptors
kbd_dev, whl_dev = blob("DevDescrKeyboard", 18), blob("DevDescrGamepad", 18)
check(ids(kbd_dev)[:2] == (0x1189, 0x8890) and ids(whl_dev)[:2] == (0x1189, 0x8890), "keyboard/gamepad: VID/PID 1189:8890")
check(ids(kbd_dev)[2] != ids(whl_dev)[2], "keyboard and gamepad layouts use different bcdDevice (separate MS OS cache)")

# ---------------------------------------------------------------- HID report descriptors
r0len, r1len = BIN[sym("Report0Len")], BIN[sym("Report1Len")]
s0 = parse_hid(blob("Report0", r0len), "report0")
check(s0.get(("in", 1)) == 64, "keyboard report = 8 bytes + id")
check(s0.get(("in", 2)) == 16, "consumer report = 2 bytes + id")
check(s0.get(("in", 3)) == 40, "mouse report = 5 bytes + id")
check(s0.get(("out", 1)) == 8, "keyboard LED output = 1 byte + id")
s1 = parse_hid(blob("Report1", r1len), "report1")
check(s1.get(("in", 0)) == 64, "vendor input report = 8 bytes")
check(s1.get(("feat", 0)) == 320, "vendor feature report = 40 bytes")

# ---------------------------------------------------------------- configurations
kcfg = blob("CfgDescrKeyboard", BIN[sym("CfgDescrKeyboard") + 2])
itfs, eps = walk_config(kcfg, "keyboard config")
check([e[0] for e in eps] == [0x81, 0x83], "keyboard: EP1 IN (HID), EP3 IN (config)")
check(kcfg[25] == r0len and kcfg[50] == r1len, "keyboard: HID descriptor lengths match report descriptors")

wcfg = blob("CfgDescrGamepad", BIN[sym("CfgDescrGamepad") + 2])
itfs, eps = walk_config(wcfg, "gamepad config")
check(wcfg[9:59] == kcfg[9:59], "gamepad: interfaces 0/1 identical to the keyboard layout")
check(itfs[2][0] == 2 and itfs[2][3:6] == (0xFF, 0x5D, 0x01), "gamepad: interface 2 is XInput FF/5D/01")
check([e for e in eps if e[0] in (0x82, 0x02)] == [(0x82, 32), (0x02, 32)], "gamepad: XInput on EP2 IN + EP2 OUT, 32 bytes")
xdesc = wcfg[59 + 9:59 + 9 + 17]
check(xdesc[1] == 0x21 and xdesc[6] == 0x82 and xdesc[13] == 0x02, "gamepad: XInput class descriptor points at EP 0x82 / 0x02")

# ---------------------------------------------------------------- Microsoft OS descriptors
ms = blob("MsOsString", 18)
check(ms[:2] == bytes([0x12, 0x03]) and ms[2:16] == "MSFT100".encode("utf-16-le"), "MS OS string: 'MSFT100'")
code = ms[16]
src = (ROOT / "src" / "config.h").read_text()
check(code == int(re.search(r"MS_OS_VENDOR_CODE\s+(0x[0-9A-Fa-f]+)", src).group(1), 16), "MS OS string: vendor code matches config.h")
cid = blob("MsCompatId", 40)
check(int.from_bytes(cid[0:4], "little") == 40 and cid[6] == 4 and cid[8] == 1, "compat ID: header (40 bytes, wIndex 4, 1 function)")
check(cid[16] == 2 and cid[17] == 1 and cid[18:26] == b"XUSB10\0\0", "compat ID: interface 2 -> 'XUSB10'")

print()
if errors:
    sys.exit(f"{len(errors)} descriptor check(s) failed")
print("all descriptor checks passed")
