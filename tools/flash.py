#!/usr/bin/env python3
"""Flash the Wildpad firmware into a CH552 macropad.

  python flash.py [firmware.bin] [--wait]
  (--wait: don't ask the running firmware, just wait for a bootloader entered by hand)

1. If Wildpad firmware is already running, it is told to jump into the USB
   bootloader (no need to open the case).
2. Otherwise enter the bootloader by hand:
     - stock firmware: open the case, short the R12 pads (or P1.5 to GND) while
       plugging in the USB cable
     - Wildpad firmware: hold key 1 while plugging in
3. Windows needs the WinUSB driver for the bootloader (USB ID 4348:55E0) once;
   the script walks you through it with Zadig.

On Windows the bootloader is driven through the WinUSB API directly (libusb ignores
it after a firmware jump because the hub reports configuration 0). Elsewhere pyusb
is used. The programming protocol is chprog by Stefan Wagner (MIT), based on
chflasher by Aaron Christophel and wchprog by juliuswwj.
"""
import os
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))               # winusb.py lives next to this script
DEFAULT_BIN = [HERE.parent / "firmware" / "wildpad.bin",              # release package
               HERE.parent / "firmware" / "build" / "wildpad.bin"]      # local build

CH_VID, CH_PID = 0x4348, 0x55E0
MODE_WRITE, MODE_VERIFY = 0xA5, 0xA6
DETECT_CHIP_CMD = (0xA1, 0x12, 0x00, 0x52, 0x11, 0x4D, 0x43, 0x55, 0x20, 0x49, 0x53, 0x50,
                   0x20, 0x26, 0x20, 0x57, 0x43, 0x48, 0x2E, 0x43, 0x4E)
CODE_FLASH_SIZE = 14336
WINDOWS = os.name == "nt"

ZADIG_URL = "https://github.com/pbatard/libwdi/releases/download/v1.5.1/zadig-2.9.exe"
ZADIG_SHA256 = "4ecaa95df3da3621486a043aef8b3050b8bafe7c901402871e816229ef82039b"

DRIVER_STEPS = """
The bootloader (USB ID 4348:55E0) needs the WinUSB driver. This is needed once per PC,
and it can be installed without the macropad connected:

  1. Zadig opens now (allow admin rights; it is downloaded on first use).
  2. Menu  Device -> Create New Device
  3. Name field (top): CH552 bootloader
     USB ID fields:    4348    55E0     (leave the third box empty)
  4. Driver (right of the green arrow): WinUSB
  5. Click "Install Driver" and wait for "The driver was installed successfully".
"""


def need(module, pipname):
    try:
        return __import__(module)
    except ImportError:
        print(f"Installing {pipname} ...")
        subprocess.check_call([sys.executable, "-m", "pip", "install", "-q", pipname])
        return __import__(module)


# ===================================================================================
# Getting into the bootloader
# ===================================================================================
def try_software_bootloader(timeout=6):
    """If Wildpad firmware is running, ask it to jump into the bootloader. Runs in a
    thread: a pad that stopped answering USB requests would otherwise hang here."""
    import threading
    result = []
    t = threading.Thread(target=lambda: result.append(_software_bootloader()), daemon=True)
    t.start()
    t.join(timeout)
    if t.is_alive():
        print("The pad does not answer. Unplug it and plug it back in while holding key 1.")
        return False
    return bool(result and result[0])


def _software_bootloader():
    """Works with every Wildpad configuration protocol (1, 2 and 3)."""
    try:
        hid = need("hid", "hidapi")
        path = next((d["path"] for d in hid.enumerate(0x1189, 0x8890) if d.get("usage_page") == 0xFF54), None)
        if path is None:
            return False
        dev = hid.device()
        dev.open_path(path)
    except Exception:
        return False
    try:
        dev.send_feature_report(b"\x00" + bytes([1, 77]) + bytes(38))         # INFO / READ
        proto = 1
        for _ in range(50):
            time.sleep(0.005)
            r = bytes(dev.get_feature_report(0, 41))
            r = r[1:] if len(r) == 41 else r
            if len(r) >= 4 and r[0] == 1 and r[1] == 77:
                proto = r[3]
                break
        dev.send_feature_report(b"\x00" + bytes([8 if proto == 1 else 10, 78]) + bytes(38))
        print("Asked the running firmware to start the USB bootloader.")
        return True
    except Exception:
        return False
    finally:
        dev.close()


def wait_for_firmware(seconds):
    """Wait until the running firmware's configuration interface is back."""
    import threading
    hid = need("hid", "hidapi")
    end = time.time() + seconds
    while time.time() < end:
        found = []
        t = threading.Thread(target=lambda: found.append(
            any(d.get("usage_page") == 0xFF54 for d in hid.enumerate(0x1189, 0x8890))), daemon=True)
        t.start()
        t.join(3)
        if t.is_alive():                                # a pad that does not answer
            return False
        if found and found[0]:
            time.sleep(0.5)
            return True
        time.sleep(0.2)
    return False


def open_zadig():
    """Start Zadig, downloading the official signed release on first use."""
    import hashlib
    import urllib.request
    zadig = HERE / "zadig-2.9.exe"
    if not zadig.exists():
        print(f"Downloading Zadig from {ZADIG_URL} ...")
        data = urllib.request.urlopen(ZADIG_URL, timeout=60).read()
        if hashlib.sha256(data).hexdigest() != ZADIG_SHA256:
            sys.exit("Zadig download has an unexpected checksum - not starting it.")
        zadig.write_bytes(data)
    os.startfile(str(zadig))                # goes through UAC, unlike Popen


def winusb_installed():
    """True if some 4348:55E0 device node already uses WinUSB (driver set up before)."""
    ps = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32" / "WindowsPowerShell" / "v1.0" / "powershell.exe"
    r = subprocess.run([str(ps), "-NoProfile", "-Command",
                        "Get-PnpDevice | Where-Object InstanceId -match 'VID_4348&PID_55E0' | ForEach-Object { "
                        "(Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName DEVPKEY_Device_Service).Data }"],
                       capture_output=True, text=True)
    return "winusb" in r.stdout.lower()


def driver_missing():
    print(DRIVER_STEPS)
    open_zadig()


def wait_windows(seconds, give_up_on_code10=True):
    """Wait for the bootloader and open it through WinUSB. Returns a transport or None.
    give_up_on_code10: return at once when Windows fails to start the bootloader's
    driver (the caller retries); otherwise keep waiting for the next attempt."""
    import winusb
    end = time.time() + seconds
    seen, last_err, said = None, None, False
    while time.time() < end:
        inst = winusb.present_instances(CH_VID, CH_PID)
        if inst:
            if seen is None:
                seen = time.time()
                print("  bootloader connected")
            if winusb.interface_guid(inst[0]) is None:
                if time.time() - seen > 3:              # give Windows a moment to bind
                    print("\nThe bootloader has no WinUSB driver yet.")
                    driver_missing()
                    sys.exit("Install the driver, then run flash.bat again.")
            elif winusb.node_problem(inst[0]) == 10:
                if give_up_on_code10:
                    print("  Windows did not start the bootloader driver this time (code 10)")
                    return None
                if not said:
                    print("  Windows did not start the bootloader driver (code 10): unplug the pad and enter the bootloader again")
                    said = True
            else:
                try:
                    return winusb.WinUsbDevice(inst[0])
                except OSError as e:
                    last_err = e                        # interface not ready yet
        elif seen is not None:
            said = False
            print(f"  bootloader went away after {time.time() - seen:.1f} s"
                  + (f" (could not open it: {last_err})" if last_err else "") + " - enter it again")
            seen, last_err = None, None
        time.sleep(0.05)
    if last_err:
        print(f"  could not open the bootloader: {last_err} - unplug the macropad and try again")
    return None


class PyUsbTransport:
    """Non-Windows transport (pyusb + libusb)."""

    def __init__(self, dev):
        import usb.util
        dev.set_configuration()
        intf = dev.get_active_configuration()[(0, 0)]
        out = lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT
        self.epout = usb.util.find_descriptor(intf, custom_match=out)
        self.epin = usb.util.find_descriptor(intf, custom_match=lambda e: not out(e))

    def write(self, data):
        self.epout.write(bytes(data))

    def read(self, size=64):
        return bytes(self.epin.read(size, 2000))

    def close(self):
        pass


def wait_pyusb(seconds):
    need("usb", "pyusb")
    import usb.core
    end = time.time() + seconds
    while time.time() < end:
        dev = usb.core.find(idVendor=CH_VID, idProduct=CH_PID)
        if dev is not None:
            return PyUsbTransport(dev)
        time.sleep(0.1)
    return None


def wait_for_bootloader(seconds, retrying=True):
    return wait_windows(seconds, retrying) if WINDOWS else wait_pyusb(seconds)


# ===================================================================================
# CH55x bootloader protocol (v2.x)
# ===================================================================================
class Programmer:
    def __init__(self, transport):
        self.t = transport
        self.chipid = 0
        self.bootloader = "?"

    def cmd(self, data):
        self.t.write(bytes(data))
        return self.t.read(64)

    def detect(self):
        r = self.cmd(DETECT_CHIP_CMD)
        if len(r) != 6:
            raise RuntimeError("chip identification failed (old v1 bootloader is not supported)")
        self.chipid = r[4]
        if self.chipid != 0x52:
            raise RuntimeError(f"unexpected chip CH5{self.chipid - 30}, expected CH552")
        cfg = self.cmd((0xA7, 0x02, 0x00, 0x1F, 0x00))
        if len(cfg) != 30:
            raise RuntimeError("could not read the bootloader configuration")
        self.bootloader = f"{cfg[19]}.{cfg[20]}{cfg[21]}"
        key = (cfg[22] + cfg[23] + cfg[24] + cfg[25]) & 0xFF
        self.cmd(bytes([0xA3, 0x30, 0x00]) + bytes([key] * 0x30) + bytes(64 - 3 - 0x30))

    def erase(self, size):
        blocks = max(8, (size + 1023) // 1024)
        r = self.cmd((0xA4, 0x01, 0x00, blocks))
        if r[4] != 0:
            raise RuntimeError("erase failed")

    def write(self, data, mode):
        addr, rest = 0, len(data)
        while addr < len(data):
            n = min(0x38, rest)
            buf = bytearray(64)
            buf[0], buf[1] = mode, n + 5
            buf[3], buf[4] = addr & 0xFF, (addr >> 8) & 0xFF
            buf[7] = rest & 0xFF
            buf[8:8 + n] = data[addr:addr + n]
            for x in range(n + 8):
                if x % 8 == 7:
                    buf[x] ^= self.chipid
            r = self.cmd(buf)                       # full 64-byte packet, like chprog
            if r[4] not in (0x00, 0xFE, 0xF5):
                raise RuntimeError(("write" if mode == MODE_WRITE else "verify") + f" failed at 0x{addr:04x}")
            addr += n
            rest -= n

    def exit(self):
        self.t.write(bytes((0xA2, 0x01, 0x00, 0x01)))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    wait_only = "--wait" in sys.argv                # skip the request to the running firmware
    found = [p for p in DEFAULT_BIN if p.exists()]
    path = Path(args[0]) if args else max(found, key=lambda p: p.stat().st_mtime, default=None)
    if path is None or not path.exists():
        sys.exit("firmware .bin not found")
    data = path.read_bytes()
    if len(data) > CODE_FLASH_SIZE:
        sys.exit("firmware too large")
    print(f"Firmware: {path} ({len(data)} bytes)")

    if WINDOWS and not winusb_installed():
        driver_missing()
        input("Press Enter here when Zadig says the driver was installed ...")

    t = None
    attempts = 8
    for attempt in range(1, 0 if wait_only else attempts + 1):
        jumped = try_software_bootloader()
        for _ in range(5 if attempt > 1 else 0):    # firmware may still be starting up
            if jumped:
                break
            time.sleep(1)
            jumped = try_software_bootloader()
        if not jumped:
            break
        t = wait_for_bootloader(10)
        if t is not None or attempt == attempts:
            break
        # After a jump from the firmware, Windows fails to start the bootloader's
        # WinUSB driver about every other time (problem code 10, STATUS_UNSUCCESSFUL);
        # restarting the device needs admin rights, so wait for the bootloader to
        # time out and jump again.
        print(f"  trying again ({attempt + 1} of {attempts}) - this takes about 10 s ...")
        wait_for_firmware(15)
    if t is None:
        print(f"\nWaiting for the bootloader ({180 if wait_only else 60} s) ...")
        print("  stock firmware : open the case, short R12 (or P1.5 to GND) while plugging in USB")
        print("  Wildpad firmware: hold key 1 while plugging in USB")
        print("  The bootloader only stays about 10 s; flashing starts as soon as it shows up.")
        t = wait_for_bootloader(180 if wait_only else 60, retrying=False)
    if t is None:
        sys.exit("Bootloader not found.")

    try:
        isp = Programmer(t)
        isp.detect()
        print(f"Found CH552, bootloader v{isp.bootloader}")
        print("Erasing ...")
        isp.erase(len(data))
        print("Writing ...")
        isp.write(data, MODE_WRITE)
        print("Verifying ...")
        isp.write(data, MODE_VERIFY)
        isp.exit()
    finally:
        t.close()
    print(f"\nDONE: {len(data)} bytes written and verified. The macropad restarts by itself.")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError) as e:
        sys.exit(f"error: {e}")
