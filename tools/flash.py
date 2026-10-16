#!/usr/bin/env python3
"""Flash the TISHRIC Knob firmware into the CH552 macropad.

  python flash.py [firmware.bin]

1. If the open firmware is already running, the device is told to jump into its
   USB bootloader (no need to open the case).
2. Otherwise put it into the bootloader by hand:
     - stock firmware: open the case, short the R12 pads (or P1.5 to GND) while
       plugging in the USB cable
     - open firmware: hold key 1 while plugging in
3. Windows needs the WinUSB driver for the bootloader device (USB ID 4348:55E0)
   once - this script explains how if it is missing.

The programming protocol is chprog by Stefan Wagner (MIT), based on chflasher by
Aaron Christophel and wchprog by juliuswwj.
"""
import os
import struct
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_BIN = [HERE.parent / "release" / "tishric_knob.bin",
               HERE.parent / "firmware" / "build" / "tishric_knob.bin"]

CH_VID, CH_PID = 0x4348, 0x55E0
MODE_WRITE_V2, MODE_VERIFY_V2 = 0xA5, 0xA6
DETECT_CHIP_CMD_V2 = (0xA1, 0x12, 0x00, 0x52, 0x11, 0x4D, 0x43, 0x55, 0x20, 0x49, 0x53, 0x50,
                      0x20, 0x26, 0x20, 0x57, 0x43, 0x48, 0x2E, 0x43, 0x4E)
CODE_FLASH_SIZE = 14336


def need(module, pipname):
    try:
        return __import__(module)
    except ImportError:
        print(f"Installing {pipname} ...")
        subprocess.check_call([sys.executable, "-m", "pip", "install", "-q", pipname])
        return __import__(module)


def libusb_backend():
    need("usb", "pyusb")
    lp = need("libusb_package", "libusb-package")
    import usb.backend.libusb1
    return usb.backend.libusb1.get_backend(find_library=lp.find_library)


def try_software_bootloader():
    """If our firmware is running, ask it to jump into the bootloader."""
    try:
        sys.path.insert(0, str(HERE))
        need("hid", "hidapi")
        import tishric
        k = tishric.Knob()
    except Exception:
        return False
    try:
        k.command(tishric.CMD_BOOTLOADER, wait=False)
        print("Open firmware found - switched it to the bootloader.")
        return True
    finally:
        k.close()


def bootloader_present_in_windows():
    """True if Windows currently sees the 4348:55E0 bootloader, even without a driver
    (libusb can't see a device that has no driver)."""
    if os.name != "nt":
        return False
    import ctypes
    cfg = ctypes.WinDLL("cfgmgr32")
    flags = 0x1 | 0x100                     # CM_GETIDLIST_FILTER_ENUMERATOR | _PRESENT
    size = ctypes.c_ulong(0)
    if cfg.CM_Get_Device_ID_List_SizeW(ctypes.byref(size), "USB", flags) != 0:
        return False
    buf = ctypes.create_unicode_buffer(size.value)
    if cfg.CM_Get_Device_ID_ListW("USB", buf, size, flags) != 0:
        return False
    ids = buf[:size.value].split("\0")
    return any("VID_4348&PID_55E0" in i.upper() for i in ids)


def winusb_installed():
    """True if some 4348:55E0 device node already uses WinUSB (driver set up before)."""
    if os.name != "nt":
        return True
    ps = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32" / "WindowsPowerShell" / "v1.0" / "powershell.exe"
    r = subprocess.run([str(ps), "-NoProfile", "-Command",
                        "Get-PnpDevice | Where-Object InstanceId -match 'VID_4348&PID_55E0' | ForEach-Object { "
                        "(Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName DEVPKEY_Device_Service).Data }"],
                       capture_output=True, text=True)
    return "winusb" in r.stdout.lower()


DRIVER_STEPS = """
The bootloader (USB ID 4348:55E0) needs the WinUSB driver. This is needed once per PC,
and it can be installed without the macropad connected:

  1. Zadig opens now (allow admin rights). If not: run tools\\zadig-2.9.exe
  2. Menu  Device -> Create New Device
  3. Name field (top): CH552 bootloader
     USB ID fields:    4348    55E0     (leave the third box empty)
  4. Driver (right of the green arrow): WinUSB
  5. Click "Install Driver" and wait for "The driver was installed successfully".
"""


def open_zadig():
    zadig = next(HERE.glob("zadig*.exe"), None)
    if zadig:
        os.startfile(str(zadig))            # goes through UAC, unlike Popen


def ensure_driver():
    if winusb_installed():
        return
    print(DRIVER_STEPS)
    open_zadig()
    input("Press Enter here when Zadig says the driver was installed ...")


def wait_for_bootloader(backend, seconds):
    """Poll until libusb can see the bootloader. Windows may list the device a few
    seconds before its driver is loaded, so only complain after a grace period."""
    import usb.core
    end = time.time() + seconds
    seen_since = None
    while time.time() < end:
        dev = usb.core.find(idVendor=CH_VID, idProduct=CH_PID, backend=backend)
        if dev is not None:
            if seen_since:
                print(f"  libusb opened it {time.time() - seen_since:.1f} s after Windows saw it")
            return dev
        if bootloader_present_in_windows():
            if seen_since is None:
                seen_since = time.time()
                print("  bootloader detected by Windows, waiting for its driver ...")
            elif time.time() - seen_since > 8:
                if winusb_installed():
                    print("\nThe bootloader uses WinUSB, but libusb still can't see it.")
                    print("Please send this output to the developer:")
                    for d in usb.core.find(find_all=True, backend=backend):
                        print(f"  libusb sees {d.idVendor:04x}:{d.idProduct:04x}")
                    sys.exit(1)
                print("\nWindows sees the bootloader, but it has no WinUSB driver yet.")
                print(DRIVER_STEPS)
                open_zadig()
                sys.exit("Install the driver, then run flash.bat again.")
        elif seen_since is not None:
            print("  bootloader disappeared again (it times out after ~10 s)")
            seen_since = None
        time.sleep(0.1)
    return None


class Programmer:
    def __init__(self, dev):
        import usb.util
        dev.set_configuration()
        intf = dev.get_active_configuration()[(0, 0)]
        out = lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT
        inn = lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN
        self.epout = usb.util.find_descriptor(intf, custom_match=out)
        self.epin = usb.util.find_descriptor(intf, custom_match=inn)
        self.chipid = 0
        self.bootloader = "?"

    def cmd(self, data):
        self.epout.write(bytes(data))
        return bytes(self.epin.read(64, 2000))

    def detect(self):
        r = self.cmd(DETECT_CHIP_CMD_V2)
        if len(r) != 6:
            raise RuntimeError("chip identification failed (old v1 bootloader is not supported)")
        self.chipid = r[4]
        if self.chipid != 0x52:
            raise RuntimeError(f"unexpected chip CH5{self.chipid - 30}, expected CH552")
        cfg = self.cmd((0xA7, 0x02, 0x00, 0x1F, 0x00))
        if len(cfg) != 30:
            raise RuntimeError("could not read bootloader config")
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
                raise RuntimeError(("write" if mode == MODE_WRITE_V2 else "verify") + f" failed at 0x{addr:04x}")
            addr += n
            rest -= n

    def exit(self):
        self.epout.write(bytes((0xA2, 0x01, 0x00, 0x01)))


def main():
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else next((p for p in DEFAULT_BIN if p.exists()), None)
    if path is None or not path.exists():
        sys.exit("firmware .bin not found")
    data = path.read_bytes()
    if len(data) > CODE_FLASH_SIZE:
        sys.exit("firmware too large")
    print(f"Firmware: {path} ({len(data)} bytes)")

    backend = libusb_backend()
    import usb.core

    dev = usb.core.find(idVendor=CH_VID, idProduct=CH_PID, backend=backend)
    if dev is None:
        ensure_driver()
    if dev is None and try_software_bootloader():
        dev = wait_for_bootloader(backend, 8)
    if dev is None:
        print("\nWaiting for the bootloader (60 s) ...")
        print("  stock firmware: open the case, short R12 (or P1.5 to GND) while plugging in USB")
        print("  open firmware : hold key 1 while plugging in USB")
        print("  The bootloader only stays about 10 s; flashing starts as soon as it shows up.")
        dev = wait_for_bootloader(backend, 60)
    if dev is None:
        sys.exit("Bootloader not found.")

    try:
        isp = Programmer(dev)
        isp.detect()
    except usb.core.USBError as e:
        print(f"USB error: {e}")
        print(DRIVER_STEPS)
        open_zadig()
        sys.exit(1)
    print(f"Found CH552, bootloader v{isp.bootloader}")
    print("Erasing ...")
    isp.erase(len(data))
    print("Writing ...")
    isp.write(data, MODE_WRITE_V2)
    print("Verifying ...")
    isp.write(data, MODE_VERIFY_V2)
    isp.exit()
    print(f"\nDONE: {len(data)} bytes written and verified. The knob restarts by itself.")


if __name__ == "__main__":
    main()
