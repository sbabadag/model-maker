import ctypes
import sys

SW_MAXIMIZE = 3
hwnd = ctypes.windll.user32.FindWindowW("ModelMakerWindow", None)
if hwnd:
    ctypes.windll.user32.ShowWindow(hwnd, SW_MAXIMIZE)
    print(f"Maximized: {hwnd}")
else:
    print("Window not found")
    sys.exit(1)
