"""Smoke-test drawing a committed line inside the 3D viewport."""
import ctypes
import time
from ctypes import wintypes
from pathlib import Path

from PIL import Image

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
WM_RBUTTONDOWN = 0x0204
WM_MOUSEMOVE = 0x0200
MK_LBUTTON = 0x0001
PW_RENDERFULLCONTENT = 2
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


def send(hwnd: int, message: int, x: int, y: int, buttons: int = 0) -> None:
    user32.SendMessageW(hwnd, message, buttons, (y << 16) | (x & 0xFFFF))


def click(hwnd: int, x: int, y: int) -> None:
    send(hwnd, WM_LBUTTONDOWN, x, y, MK_LBUTTON)
    send(hwnd, WM_LBUTTONUP, x, y)


def capture(hwnd: int, path: Path) -> Image.Image:
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
    image = Image.frombuffer("RGB", (width, height), pixels, "raw", "BGRX", 0, 1).copy()
    image.save(path)
    gdi32.SelectObject(memory_dc, old)
    gdi32.DeleteObject(bitmap)
    gdi32.DeleteDC(memory_dc)
    user32.ReleaseDC(hwnd, window_dc)
    return image


def cyan_count(image: Image.Image) -> int:
    return sum(1 for pixel in image.get_flattened_data() if pixel == (104, 202, 255))


def main() -> None:
    hwnd = user32.FindWindowW("ModelMakerWindow", None)
    if not hwnd:
        raise RuntimeError("Model Maker window was not found")

    click(hwnd, 30, 29)       # New/clear
    click(hwnd, 537, 29)      # Cube -> 3D viewport
    click(hwnd, 690, 29)      # Disable OSNAP; keep grid snap deterministic.
    click(hwnd, 228, 29)      # Line tool must remain in 3D
    time.sleep(0.2)
    before = capture(hwnd, Path("build/3d-drawing-before.png"))

    click(hwnd, 350, 380)
    send(hwnd, WM_MOUSEMOVE, 650, 460)
    time.sleep(0.1)
    click(hwnd, 650, 460)
    time.sleep(0.2)
    line_image = capture(hwnd, Path("build/3d-line-committed.png"))

    click(hwnd, 390, 29)      # Rectangle
    click(hwnd, 760, 320)
    click(hwnd, 900, 440)
    time.sleep(0.2)
    rectangle_image = capture(hwnd, Path("build/3d-rectangle-committed.png"))

    click(hwnd, 468, 29)      # Circle
    click(hwnd, 300, 555)
    click(hwnd, 520, 610)
    time.sleep(0.2)
    circle_image = capture(hwnd, Path("build/3d-circle-committed.png"))

    click(hwnd, 300, 29)      # Polyline
    click(hwnd, 650, 600)
    click(hwnd, 800, 610)
    click(hwnd, 880, 540)
    send(hwnd, WM_RBUTTONDOWN, 880, 540)
    time.sleep(0.2)
    after = capture(hwnd, Path("build/3d-drawing-tools-committed.png"))

    stages = [
        ("line", before, line_image, 5),
        ("rectangle", line_image, rectangle_image, 40),
        ("circle", rectangle_image, circle_image, 40),
        ("polyline", circle_image, after, 20),
    ]
    deltas = {}
    for name, previous, current, minimum in stages:
        delta = cyan_count(current) - cyan_count(previous)
        deltas[name] = delta
        if delta < minimum:
            raise RuntimeError(f"3D {name} was not visibly committed (cyan pixel delta={delta})")
    print(f"3D drawing smoke test passed in parallel view: {deltas}")


if __name__ == "__main__":
    main()
