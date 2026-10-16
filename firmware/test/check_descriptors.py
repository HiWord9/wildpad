"""Static checks of the USB descriptors inside the built firmware image.
Reads build/tishric_knob.bin + .map, walks every descriptor and parses the HID
report descriptors to confirm report sizes match what the firmware sends."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = (ROOT / "build" / "tishric_knob.bin").read_bytes()
MAP = (ROOT / "build" / "tishric_knob.map").read_text()
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
    i, itfs, eps = 0, 0, []
    while i < len(cfg):
        ln, typ = cfg[i], cfg[i + 1]
        check(ln >= 2 and i + ln <= len(cfg), f"{label}: descriptor at {i} type 0x{typ:02x} len {ln} fits")
        if typ == 4:
            itfs += 1
        if typ == 5:
            eps.append((cfg[i + 2], cfg[i + 4] | cfg[i + 5] << 8, cfg[i + 6]))
        i += ln
    check(itfs == cfg[4], f"{label}: {itfs} interface descriptors == bNumInterfaces {cfg[4]}")
    return eps


def parse_hid(desc, label):
    """Return {(kind, report_id): bits} for input/output/feature reports."""
    sizes, i = {}, 0
    gl = {"size": 0, "count": 0, "id": 0}
    stack, depth = [], 0
    while i < len(desc):
        b = desc[i]
        n = [0, 1, 2, 4][b & 3]
        val = int.from_bytes(desc[i + 1:i + 1 + n], "little")
        tag, typ = b & 0xFC, (b >> 2) & 3
        if typ == 0:                                  # main
            if tag in (0x80, 0x90, 0xB0):
                kind = {0x80: "in", 0x90: "out", 0xB0: "feat"}[tag]
                k = (kind, gl["id"])
                sizes[k] = sizes.get(k, 0) + gl["size"] * gl["count"]
            elif tag == 0xA0:
                depth += 1
            elif tag == 0xC0:
                depth -= 1
        elif typ == 1:                                # global
            if tag == 0x74: gl["size"] = val
            elif tag == 0x94: gl["count"] = val
            elif tag == 0x84: gl["id"] = val
            elif tag == 0xA4: stack.append(dict(gl))
            elif tag == 0xB4: gl = stack.pop()
        i += 1 + n
    check(depth == 0, f"{label}: collections balanced")
    return sizes


# ---------------------------------------------------------------- keyboard mode
dev = blob("KBD_DevDescr", 18)
check(dev[0] == 18 and dev[1] == 1, "keyboard: device descriptor header")
check((dev[8] | dev[9] << 8, dev[10] | dev[11] << 8) == (0x1189, 0x8890), "keyboard: VID/PID 1189:8890")
cfg = blob("KBD_CfgDescr", BIN[sym("KBD_CfgDescr") + 2])
eps = walk_config(cfg, "keyboard config")
check([e[0] for e in eps] == [0x81, 0x83], "keyboard: endpoints EP1 IN, EP3 IN")
r0len = BIN[sym("KBD_Report0Len")]
r1len = BIN[sym("KBD_Report1Len")]
check(cfg[18 + 7] == r0len, f"keyboard: HID0 wDescriptorLength {cfg[25]} == report0 {r0len}")
check(cfg[43 + 7] == r1len, f"keyboard: HID1 wDescriptorLength {cfg[50]} == report1 {r1len}")
check(cfg[18 + 1] == 0x21 and cfg[43 + 1] == 0x21, "keyboard: HID descriptor offsets 18 / 43")
s0 = parse_hid(blob("KBD_Report0", r0len), "report0")
check(s0.get(("in", 1)) == 64, f"keyboard report = 8 bytes + id (got {s0.get(('in', 1))} bits)")
check(s0.get(("in", 2)) == 16, "consumer report = 2 bytes + id")
check(s0.get(("in", 3)) == 40, f"mouse report = 5 bytes + id (got {s0.get(('in', 3))} bits)")
check(s0.get(("out", 1)) == 8, "keyboard LED output = 1 byte + id")
max_in = max(v for (k, _), v in s0.items() if k == "in") // 8 + 1
check(max_in <= eps[0][1], f"largest EP1 report {max_in} B <= wMaxPacketSize {eps[0][1]}")
s1 = parse_hid(blob("KBD_Report1", r1len), "report1")
check(s1.get(("in", 0)) == 64, "vendor input report = 8 bytes (no id)")
check(s1.get(("feat", 0)) == 320, "vendor feature report = 40 bytes (no id)")

# ---------------------------------------------------------------- gamepad mode
dev = blob("PAD_DevDescr", 18)
check((dev[8] | dev[9] << 8, dev[10] | dev[11] << 8) == (0x045E, 0x028E), "gamepad: VID/PID 045E:028E")
check(dev[4:7] == bytes([0xFF, 0xFF, 0xFF]), "gamepad: vendor-specific device class")
cfg = blob("PAD_CfgDescr", 153)
eps = walk_config(cfg, "gamepad config")
check(eps[0][:2] == (0x81, 32) and eps[1][:2] == (0x02, 32), "gamepad: EP1 IN / EP2 OUT, 32 bytes")
check(cfg[9 + 5:9 + 8] == bytes([0xFF, 0x5D, 0x01]), "gamepad: interface 0 is FF/5D/01 (XInput)")

print()
if errors:
    sys.exit(f"{len(errors)} descriptor check(s) failed")
print("all descriptor checks passed")
