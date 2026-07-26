"""Smoke-test the interactive 3D ViewCube in a running Model Maker window."""
import ctypes
import time
from ctypes import wintypes
from pathlib import Path

from PIL import Image, ImageChops

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


def main() -> None:
    hwnd = user32.FindWindowW("ModelMakerWindow", None)
    if not hwnd:
        raise RuntimeError("Model Maker window was not found")
    rect = wintypes.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    width = rect.right
    center = max(84, width - 84)

    click(hwnd, 537, 29)  # Add cube and enter 3D mode.
    time.sleep(0.2)
    iso = capture(hwnd, Path("build/view-cube-isometric.png"))

    send(hwnd, WM_MOUSEMOVE, center + 10, 116)
    time.sleep(0.2)
    hover = capture(hwnd, Path("build/view-cube-hover-top.png"))
    blue_pixels = sum(1 for pixel in hover.crop((center - 38, 82, center + 39, 127)).get_flattened_data()
                      if pixel == (42, 155, 220))
    if blue_pixels < 100:
        raise RuntimeError("Top-face hover highlight was not rendered")

    click(hwnd, center, 102)
    time.sleep(0.2)
    top = capture(hwnd, Path("build/view-cube-top-view.png"))
    canvas_box = (100, 100, max(101, width - 170), rect.bottom - 60)
    if ImageChops.difference(iso.crop(canvas_box), top.crop(canvas_box)).getbbox() is None:
        raise RuntimeError("Clicking the ViewCube top face did not change the model projection")

    print(f"ViewCube smoke test passed ({width}x{rect.bottom}): rendered, hover-highlighted, and switched to TOP view.")


if __name__ == "__main__":
    main()
