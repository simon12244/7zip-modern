import ctypes, ctypes.wintypes as wt, time, subprocess
from PIL import ImageGrab

u32 = ctypes.windll.user32
u32.SetProcessDPIAware()

exe = r"C:/Program Files/7-Zip/7zFM.exe"
folder = r"C:/Users/t/WorkBuddy/2026-11-09-11-25-36/7zip-mod/testdir"

subprocess.run(["taskkill", "/F", "/IM", "7zFM.exe"], capture_output=True)
time.sleep(1)
proc = subprocess.Popen([exe, folder])
time.sleep(5)

found = []

@ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
def cb(hwnd, lp):
    pid = wt.DWORD()
    u32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    if pid.value == proc.pid and u32.IsWindowVisible(hwnd):
        r = wt.RECT()
        u32.GetWindowRect(hwnd, ctypes.byref(r))
        if r.right - r.left > 400 and r.bottom - r.top > 300:
            found.append((hwnd, r))
    return True

u32.EnumWindows(cb, 0)
print("windows:", len(found))
for hwnd, r in found:
    u32.ShowWindow(hwnd, 9)
    u32.SetForegroundWindow(hwnd)
    u32.BringWindowToTop(hwnd)
    time.sleep(1.5)
    im = ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom))
    im.save("ui-shot.png")
    print("saved", im.size)
    break
