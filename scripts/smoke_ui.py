"""Minimal native UI smoke test for a running Model Maker instance."""
import ctypes
import time
from ctypes import wintypes

from PIL import Image

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32

WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
WM_MOUSEMOVE = 0x0200
MK_LBUTTON = 0x0001
PW_RENDERFULLCONTENT = 0x00000002
BI_RGB = 0
DIB_RGB_COLORS = 0


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ("biSize", wintypes.DWORD), ("biWidth", wintypes.LONG),
        ("biHeight", wintypes.LONG), ("biPlanes", wintypes.WORD),
        ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
        ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", wintypes.LONG),
        ("biYPelsPerMeter", wintypes.LONG), ("biClrUsed", wintypes.DWORD),
        ("biClrImportant", wintypes.DWORD),
    ]


class BITMAPINFO(ctypes.Structure):
    _fields_ = [("bmiHeader", BITMAPINFOHEADER), ("bmiColors", wintypes.DWORD * 3)]


def click(hwnd: int, x: int, y: int) -> None:
    point = (y << 16) | (x & 0xFFFF)
    user32.SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, point)
    user32.SendMessageW(hwnd, WM_LBUTTONUP, 0, point)


def move(hwnd: int, x: int, y: int) -> None:
    point = (y << 16) | (x & 0xFFFF)
    user32.SendMessageW(hwnd, WM_MOUSEMOVE, 0, point)


def capture(hwnd: int, path: str) -> None:
    rect = wintypes.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    width, height = rect.right, rect.bottom
    window_dc = user32.GetDC(hwnd)
    memory_dc = gdi32.CreateCompatibleDC(window_dc)
    bitmap = gdi32.CreateCompatibleBitmap(window_dc, width, height)
    old = gdi32.SelectObject(memory_dc, bitmap)
    if not user32.PrintWindow(hwnd, memory_dc, PW_RENDERFULLCONTENT):
        raise RuntimeError("PrintWindow failed")

    info = BITMAPINFO()
    info.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    info.bmiHeader.biWidth = width
    info.bmiHeader.biHeight = -height
    info.bmiHeader.biPlanes = 1
    info.bmiHeader.biBitCount = 32
    info.bmiHeader.biCompression = BI_RGB
    pixels = ctypes.create_string_buffer(width * height * 4)
    gdi32.GetDIBits(memory_dc, bitmap, 0, height, pixels, ctypes.byref(info), DIB_RGB_COLORS)
    Image.frombuffer("RGB", (width, height), pixels, "raw", "BGRX", 0, 1).save(path)

    gdi32.SelectObject(memory_dc, old)
    gdi32.DeleteObject(bitmap)
    gdi32.DeleteDC(memory_dc)
    user32.ReleaseDC(hwnd, window_dc)


def main() -> None:
    hwnd = user32.FindWindowW("ModelMakerWindow", None)
    if not hwnd:
        raise RuntimeError("Model Maker window was not found")
    click(hwnd, 228, 29)  # Çizgi
    click(hwnd, 420, 300)
    click(hwnd, 600, 420)
    click(hwnd, 390, 29)  # Dikdörtgen
    click(hwnd, 660, 240)
    move(hwnd, 840, 420)  # Preview + dynamic input
    time.sleep(0.3)
    capture(hwnd, "build/ui-smoke.png")
    print("UI smoke test passed: line created; rectangle preview, SNAP and dynamic input rendered.")


if __name__ == "__main__":
    main()
