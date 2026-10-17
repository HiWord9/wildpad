"""Minimal WinUSB transport (Windows only, standard library only).

libusb skips devices whose hub reports "current configuration 0" - which is exactly
what the CH552 bootloader looks like after the firmware jumps into it. WinUSB itself
does not care, so on Windows the flasher talks to the bootloader directly.
"""
import ctypes
import ctypes.wintypes as W
import winreg

_cfg = ctypes.WinDLL("cfgmgr32")
_k32 = ctypes.WinDLL("kernel32", use_last_error=True)
_wu = ctypes.WinDLL("winusb", use_last_error=True)

GENERIC_RW = 0x80000000 | 0x40000000
FILE_SHARE_RW = 0x1 | 0x2
OPEN_EXISTING = 3
FILE_FLAG_OVERLAPPED = 0x40000000
INVALID_HANDLE = W.HANDLE(-1).value
PIPE_TRANSFER_TIMEOUT = 0x03


class GUID(ctypes.Structure):
    _fields_ = [("Data1", W.DWORD), ("Data2", W.WORD), ("Data3", W.WORD), ("Data4", ctypes.c_ubyte * 8)]


class PIPE_INFO(ctypes.Structure):
    _fields_ = [("PipeType", ctypes.c_int), ("PipeId", ctypes.c_ubyte),
                ("MaximumPacketSize", ctypes.c_ushort), ("Interval", ctypes.c_ubyte)]


class IFACE_DESC(ctypes.Structure):
    _fields_ = [("bLength", ctypes.c_ubyte), ("bDescriptorType", ctypes.c_ubyte),
                ("bInterfaceNumber", ctypes.c_ubyte), ("bAlternateSetting", ctypes.c_ubyte),
                ("bNumEndpoints", ctypes.c_ubyte), ("bInterfaceClass", ctypes.c_ubyte),
                ("bInterfaceSubClass", ctypes.c_ubyte), ("bInterfaceProtocol", ctypes.c_ubyte),
                ("iInterface", ctypes.c_ubyte)]


_k32.CreateFileW.restype = W.HANDLE
_k32.CreateFileW.argtypes = [W.LPCWSTR, W.DWORD, W.DWORD, ctypes.c_void_p, W.DWORD, W.DWORD, W.HANDLE]
_wu.WinUsb_Initialize.argtypes = [W.HANDLE, ctypes.POINTER(ctypes.c_void_p)]
_wu.WinUsb_Free.argtypes = [ctypes.c_void_p]
_wu.WinUsb_QueryInterfaceSettings.argtypes = [ctypes.c_void_p, ctypes.c_ubyte, ctypes.POINTER(IFACE_DESC)]
_wu.WinUsb_QueryPipe.argtypes = [ctypes.c_void_p, ctypes.c_ubyte, ctypes.c_ubyte, ctypes.POINTER(PIPE_INFO)]
_wu.WinUsb_SetPipePolicy.argtypes = [ctypes.c_void_p, ctypes.c_ubyte, W.ULONG, W.ULONG, ctypes.c_void_p]
_wu.WinUsb_WritePipe.argtypes = [ctypes.c_void_p, ctypes.c_ubyte, ctypes.c_void_p, W.ULONG,
                                 ctypes.POINTER(W.ULONG), ctypes.c_void_p]
_wu.WinUsb_ReadPipe.argtypes = _wu.WinUsb_WritePipe.argtypes


def present_instances(vid, pid):
    """Device instance IDs of present USB devices with this VID/PID."""
    flags = 0x1 | 0x100                       # FILTER_ENUMERATOR | FILTER_PRESENT
    size = ctypes.c_ulong(0)
    if _cfg.CM_Get_Device_ID_List_SizeW(ctypes.byref(size), "USB", flags) != 0:
        return []
    buf = ctypes.create_unicode_buffer(size.value)
    if _cfg.CM_Get_Device_ID_ListW("USB", buf, size, flags) != 0:
        return []
    tag = f"USB\\VID_{vid:04X}&PID_{pid:04X}\\"
    return [i for i in buf[:size.value].split("\0") if i.upper().startswith(tag)]


def node_problem(instance):
    """Windows problem code of a device node (0 = none, 10 = could not start)."""
    dn, status, problem = ctypes.c_ulong(), ctypes.c_ulong(), ctypes.c_ulong()
    if _cfg.CM_Locate_DevNodeW(ctypes.byref(dn), instance, 0) != 0:
        return 0
    if _cfg.CM_Get_DevNode_Status(ctypes.byref(status), ctypes.byref(problem), dn, 0) != 0:
        return 0
    return problem.value


def interface_guid(instance):
    """DeviceInterfaceGUID(s) registered by the WinUSB install (None = no driver)."""
    try:
        key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                             rf"SYSTEM\CurrentControlSet\Enum\{instance}\Device Parameters")
    except OSError:
        return None
    with key:
        for name in ("DeviceInterfaceGUIDs", "DeviceInterfaceGUID"):
            try:
                val, _ = winreg.QueryValueEx(key, name)
            except OSError:
                continue
            return val[0] if isinstance(val, list) else val
    return None


def _device_path(instance, guid_text):
    g = GUID()
    ctypes.oledll.ole32.CLSIDFromString(guid_text, ctypes.byref(g))
    size = ctypes.c_ulong(0)
    if _cfg.CM_Get_Device_Interface_List_SizeW(ctypes.byref(size), ctypes.byref(g), instance, 0) != 0:
        return None
    buf = ctypes.create_unicode_buffer(size.value)
    if _cfg.CM_Get_Device_Interface_ListW(ctypes.byref(g), instance, buf, size, 0) != 0:
        return None
    paths = [p for p in buf[:size.value].split("\0") if p]
    return paths[0] if paths else None


USB_DEVICE_GUID = "{A5DCBF10-6530-11D2-901F-00C04FB951ED}"   # every non-composite USB device


class SETUP_PACKET(ctypes.Structure):
    _pack_ = 1
    _fields_ = [("RequestType", ctypes.c_ubyte), ("Request", ctypes.c_ubyte),
                ("Value", ctypes.c_ushort), ("Index", ctypes.c_ushort), ("Length", ctypes.c_ushort)]


_wu.WinUsb_ControlTransfer.argtypes = [ctypes.c_void_p, SETUP_PACKET, ctypes.c_void_p, W.ULONG,
                                       ctypes.POINTER(W.ULONG), ctypes.c_void_p]


class WinUsbDevice:
    def __init__(self, instance, timeout_ms=2000, guid=None):
        """guid: the device interface to open; default: the one the WinUSB install
        registered (USB_DEVICE_GUID works for single-interface devices)."""
        guid = guid or interface_guid(instance)
        if guid is None:
            raise LookupError("no WinUSB driver")
        path = _device_path(instance, guid)
        if path is None:
            raise OSError("device interface not ready yet")
        self.file = _k32.CreateFileW(path, GENERIC_RW, FILE_SHARE_RW, None, OPEN_EXISTING,
                                     FILE_FLAG_OVERLAPPED, None)
        if self.file in (None, INVALID_HANDLE):
            raise OSError(f"CreateFile failed ({ctypes.get_last_error()})")
        self.h = ctypes.c_void_p()
        if not _wu.WinUsb_Initialize(self.file, ctypes.byref(self.h)):
            err = ctypes.get_last_error()
            _k32.CloseHandle(self.file)
            raise OSError(f"WinUsb_Initialize failed ({err})")
        desc = IFACE_DESC()
        _wu.WinUsb_QueryInterfaceSettings(self.h, 0, ctypes.byref(desc))
        self.ep_in = self.ep_out = None
        for i in range(desc.bNumEndpoints):
            info = PIPE_INFO()
            _wu.WinUsb_QueryPipe(self.h, 0, i, ctypes.byref(info))
            if info.PipeId & 0x80:
                self.ep_in = info.PipeId
            else:
                self.ep_out = info.PipeId
        t = W.ULONG(timeout_ms)
        for ep in (self.ep_in, self.ep_out):
            _wu.WinUsb_SetPipePolicy(self.h, ep, PIPE_TRANSFER_TIMEOUT, ctypes.sizeof(t), ctypes.byref(t))

    def write(self, data):
        buf = (ctypes.c_ubyte * len(data)).from_buffer_copy(bytes(data))
        done = W.ULONG(0)
        if not _wu.WinUsb_WritePipe(self.h, self.ep_out, buf, len(data), ctypes.byref(done), None):
            raise OSError(f"USB write failed ({ctypes.get_last_error()})")
        return done.value

    def read(self, size=64):
        buf = (ctypes.c_ubyte * size)()
        done = W.ULONG(0)
        if not _wu.WinUsb_ReadPipe(self.h, self.ep_in, buf, size, ctypes.byref(done), None):
            raise OSError(f"USB read failed ({ctypes.get_last_error()})")
        return bytes(buf[:done.value])

    def control_out(self, request, value=0, index=0):
        """Vendor request to the device, no data."""
        done = W.ULONG(0)
        if not _wu.WinUsb_ControlTransfer(self.h, SETUP_PACKET(0x40, request, value, index, 0),
                                          None, 0, ctypes.byref(done), None):
            raise OSError(f"USB control request failed ({ctypes.get_last_error()})")

    def close(self):
        _wu.WinUsb_Free(self.h)
        _k32.CloseHandle(self.file)
