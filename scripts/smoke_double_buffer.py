"""Verify the reusable Win32/GDI viewport back buffer survives repaint and resize churn."""
import ctypes
import subprocess
import time
from pathlib import Path

from smoke_osnap import find_window
from smoke_view_cube import capture

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32
WM_CLOSE = 0x0010
SWP_NOMOVE = 0x0002
SWP_NOZORDER = 0x0004
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
GR_GDIOBJECTS = 0

kernel32.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
kernel32.OpenProcess.restype = ctypes.c_void_p
user32.GetGuiResources.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
user32.GetGuiResources.restype = ctypes.c_uint32


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    process = subprocess.Popen([str(root / "build" / "model-maker.exe")])
    window = 0
    process_handle = None
    try:
        window = find_window("ModelMakerWindow")
        canvas = user32.FindWindowExW(window, 0, "ModelMakerCanvas", None)
        if not canvas:
            raise RuntimeError("Canvas window was not found")
        process_handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, process.pid)
        if not process_handle:
            raise RuntimeError("Could not query GUI resources")
        time.sleep(0.1)
        before = user32.GetGuiResources(process_handle, GR_GDIOBJECTS)
        for index in range(80):
            width = 900 + (index % 7) * 37
            height = 650 + (index % 5) * 31
            user32.SetWindowPos(window, 0, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER)
            user32.UpdateWindow(window)
        after = user32.GetGuiResources(process_handle, GR_GDIOBJECTS)
        image = capture(canvas, root / "build" / "double-buffer-resize.png")
        if image.width < 400 or image.height < 300:
            raise AssertionError("Viewport capture was unexpectedly small")
        if after > before + 3:
            raise AssertionError(f"GDI resource count grew during resize churn: {before} -> {after}")
        print(f"Reusable double-buffer smoke passed: GDI objects {before} -> {after}, capture={image.width}x{image.height}.")
    finally:
        if process_handle:
            kernel32.CloseHandle(process_handle)
        if window:
            user32.PostMessageW(window, WM_CLOSE, 0, 0)
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=3)


if __name__ == "__main__":
    main()
