"""GUI regression for the camera-synchronized, draggable 3D ViewCube."""
import ctypes
import subprocess
import time
from ctypes import wintypes
from pathlib import Path

from PIL import Image, ImageChops

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
WM_CLOSE = 0x0010
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
WM_MOUSEMOVE = 0x0200
BM_CLICK = 0x00F5
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


def find_window(timeout: float = 5.0) -> int:
    deadline = time.time() + timeout
    while time.time() < deadline:
        hwnd = user32.FindWindowW("ModelMakerWindow", None)
        if hwnd:
            return hwnd
        time.sleep(0.05)
    raise RuntimeError("Model Maker window was not found")


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    process = subprocess.Popen([str(root / "build" / "model-maker.exe")])
    hwnd = 0
    try:
        hwnd = find_window()
        canvas = user32.FindWindowExW(hwnd, 0, "ModelMakerCanvas", None)
        if not canvas:
            raise RuntimeError("Drawing canvas was not found")
        user32.SendMessageW(user32.GetDlgItem(hwnd, 603), BM_CLICK, 0, 0)
        user32.SendMessageW(user32.GetDlgItem(hwnd, 300), BM_CLICK, 0, 0)
        time.sleep(0.25)

        rect = wintypes.RECT()
        user32.GetClientRect(canvas, ctypes.byref(rect))
        width, height = rect.right, rect.bottom
        center = max(96, width - 96)
        before = capture(canvas, root / "build" / "view-cube-before-drag.png")

        # Dragging on the cube orbits the camera instead of selecting/drawing.
        send(canvas, WM_LBUTTONDOWN, center, 119, MK_LBUTTON)
        send(canvas, WM_MOUSEMOVE, center + 38, 145, MK_LBUTTON)
        send(canvas, WM_LBUTTONUP, center + 38, 145)
        time.sleep(0.25)
        dragged = capture(canvas, root / "build" / "view-cube-after-drag.png")

        cube_box = (center - 66, 55, center + 66, 190)
        model_box = (80, 80, max(81, center - 100), height - 50)
        cube_change = ImageChops.difference(before.crop(cube_box), dragged.crop(cube_box)).getbbox()
        model_change = ImageChops.difference(before.crop(model_box), dragged.crop(model_box)).getbbox()
        if cube_change is None:
            raise RuntimeError("Dragging did not rotate the ViewCube")
        if model_change is None:
            raise RuntimeError("Dragging the ViewCube did not orbit the model view")

        # The triad is rendered from global X/Y/Z using the same camera orientation.
        axis_crop = dragged.crop((center - 72, 190, center + 15, 235))
        pixels = list(axis_crop.get_flattened_data())
        red = sum(1 for r, g, b in pixels if r > 180 and g < 130 and b < 145)
        green = sum(1 for r, g, b in pixels if g > 150 and r < 145 and b < 165)
        blue = sum(1 for r, g, b in pixels if b > 180 and r < 145 and g < 180)
        if min(red, green, blue) < 3:
            raise RuntimeError(f"Global XYZ triad is incomplete: red={red}, green={green}, blue={blue}")

        # HOME returns to isometric, then clicking the global top face selects TOP.
        click(canvas, center + 42, 215)
        time.sleep(0.15)
        iso = capture(canvas, root / "build" / "view-cube-home.png")
        click(canvas, center, 94)
        time.sleep(0.2)
        top = capture(canvas, root / "build" / "view-cube-top-view.png")
        if ImageChops.difference(iso.crop(model_box), top.crop(model_box)).getbbox() is None:
            raise RuntimeError("Clicking the global top face did not switch the model to TOP view")

        print(f"ViewCube manipulation smoke passed ({width}x{height}): cube/model orbit, global XYZ triad, HOME and TOP face verified.")
    finally:
        if hwnd:
            user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=3)


if __name__ == "__main__":
    main()
