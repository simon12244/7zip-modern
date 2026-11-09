import ctypes, ctypes.wintypes as wt, time, subprocess, sys
from PIL import Image

u32 = ctypes.windll.user32
g32 = ctypes.windll.gdi32
u32.SetProcessDPIAware()
u32.PrintWindow.argtypes = [wt.HWND, wt.HDC, wt.UINT]

exe = sys.argv[1] if len(sys.argv) > 1 else r"C:/Users/t/WorkBuddy/2026-11-09-11-25-36/7zip-mod/run/7zFM.exe"
out = sys.argv[2] if len(sys.argv) > 2 else "ui-shot.png"
folder = r"C:/Users/t/WorkBuddy/2026-11-09-11-25-36/7zip-mod/testdir"


def capture(hwnd, path):
    r = wt.RECT()
    u32.GetWindowRect(hwnd, ctypes.byref(r))
    w, h = r.right - r.left, r.bottom - r.top
    hdc = u32.GetWindowDC(hwnd)
    mdc = g32.CreateCompatibleDC(hdc)
    bmp = g32.CreateCompatibleBitmap(hdc, w, h)
    g32.SelectObject(mdc, bmp)
    u32.PrintWindow(hwnd, mdc, 2)  # PW_RENDERFULLCONTENT

    class BIH(ctypes.Structure):
        _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG),
                    ("biPlanes", wt.WORD), ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
                    ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", wt.LONG),
                    ("biYPelsPerMeter", wt.LONG), ("biClrUsed", wt.DWORD), ("biClrImportant", wt.DWORD)]

    bi = BIH()
    bi.biSize = ctypes.sizeof(BIH)
    bi.biWidth, bi.biHeight = w, -h
    bi.biPlanes, bi.biBitCount = 1, 32
    buf = ctypes.create_string_buffer(w * h * 4)
    g32.GetDIBits(mdc, bmp, 0, h, buf, ctypes.byref(bi), 0)
    img = Image.frombuffer("RGBA", (w, h), buf, "raw", "BGRA", 0, 1).convert("RGB")
    img.save(path)
    g32.DeleteObject(bmp)
    g32.DeleteDC(mdc)
    u32.ReleaseDC(hwnd, hdc)
    return img.size


subprocess.run(["taskkill", "/F", "/IM", "7zFM.exe"], capture_output=True)
time.sleep(1)
proc = subprocess.Popen([exe, folder])
time.sleep(4)

found = []


@ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
def cb(hwnd, lp):
    pid = wt.DWORD()
    u32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    if pid.value == proc.pid and u32.IsWindowVisible(hwnd):
        r = wt.RECT()
        u32.GetWindowRect(hwnd, ctypes.byref(r))
        cls = ctypes.create_unicode_buffer(64)
        u32.GetClassNameW(hwnd, cls, 64)
        if cls.value != "#32770" and r.right - r.left > 400 and r.bottom - r.top > 300:
            found.append(hwnd)
    return True


u32.EnumWindows(cb, 0)
print("windows:", len(found))
for hwnd in found:
    u32.ShowWindow(hwnd, 9)
    time.sleep(1.0)
    print("saved", out, capture(hwnd, out))
    break
