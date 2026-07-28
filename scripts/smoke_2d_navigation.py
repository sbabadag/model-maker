"""Native GUI smoke test for 2D middle-button pan, Zoom Extents, and Zoom Window."""
import ctypes
import subprocess
import time
from ctypes import wintypes
from pathlib import Path

from PIL import ImageChops
from smoke_view_cube import capture

user32 = ctypes.windll.user32
WM_CLOSE = 0x0010
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
WM_MBUTTONDOWN = 0x0207
WM_MBUTTONUP = 0x0208
WM_MOUSEMOVE = 0x0200
WM_MOUSEWHEEL = 0x020A
MK_LBUTTON = 0x0001
MK_MBUTTON = 0x0010
BM_CLICK = 0x00F5
SW_MAXIMIZE = 3
CMD_ZOOM_EXTENTS = 305
CMD_ZOOM_WINDOW = 306


def lparam(x: int, y: int) -> int:
    return (y & 0xFFFF) << 16 | (x & 0xFFFF)


def find_window(timeout: float = 5.0) -> int:
    deadline = time.time() + timeout
    while time.time() < deadline:
        hwnd = user32.FindWindowW("ModelMakerWindow", None)
        if hwnd:
            return hwnd
        time.sleep(0.05)
    raise RuntimeError("Model Maker window was not found")


def click(canvas: int, x: int, y: int) -> None:
    user32.SendMessageW(canvas, WM_LBUTTONDOWN, MK_LBUTTON, lparam(x, y))
    user32.SendMessageW(canvas, WM_LBUTTONUP, 0, lparam(x, y))
    time.sleep(0.08)


def cyan_line_segment(image):
    pixels = image.convert("RGB").load()
    rows = []
    for y in range(image.height):
        xs = [x for x in range(image.width)
              if pixels[x, y][2] > 210 and pixels[x, y][1] > 150 and pixels[x, y][0] < 150]
        if xs:
            rows.append((len(xs), y, min(xs), max(xs)))
    if not rows:
        raise AssertionError("No model-colored line pixels were found")
    _count, y, minimum_x, maximum_x = max(rows)
    return minimum_x, y, maximum_x, y


def cyan_bbox(image):
    mask = image.convert("RGB").point(lambda _v: _v)
    pixels = mask.load()
    xs, ys = [], []
    for y in range(image.height):
        for x in range(image.width):
            r, g, b = pixels[x, y]
            if b > 210 and g > 150 and r < 150:
                xs.append(x); ys.append(y)
    if not xs:
        raise AssertionError("No model-colored pixels were found")
    return min(xs), min(ys), max(xs), max(ys)


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    process = subprocess.Popen([str(root / "build" / "model-maker.exe")])
    window = 0
    try:
        window = find_window()
        user32.ShowWindow(window, SW_MAXIMIZE)
        user32.UpdateWindow(window)
        time.sleep(0.3)
        canvas = user32.FindWindowExW(window, 0, "ModelMakerCanvas", None)
        user32.SendMessageW(user32.GetDlgItem(window, 603), BM_CLICK, 0, 0)
        extents = user32.GetDlgItem(window, CMD_ZOOM_EXTENTS)
        zoom_window = user32.GetDlgItem(window, CMD_ZOOM_WINDOW)
        if not canvas or not extents or not zoom_window:
            raise AssertionError("Zoom Extents/Zoom Window controls are missing")
        rect = wintypes.RECT(); user32.GetClientRect(canvas, ctypes.byref(rect))
        cx, cy = rect.right // 2, rect.bottom // 2

        click(canvas, cx - 120, cy)
        click(canvas, cx + 120, cy)
        before = capture(canvas, root / "build" / "navigation-before-pan.png")
        before_box = cyan_line_segment(before)
        user32.SendMessageW(canvas, WM_MBUTTONDOWN, MK_MBUTTON, lparam(cx, cy))
        user32.SendMessageW(canvas, WM_MOUSEMOVE, MK_MBUTTON, lparam(cx + 80, cy + 45))
        user32.SendMessageW(canvas, WM_MBUTTONUP, 0, lparam(cx + 80, cy + 45))
        time.sleep(0.15)
        after_pan = capture(canvas, root / "build" / "navigation-after-pan.png")
        after_box = cyan_line_segment(after_pan)
        if abs((after_box[0] - before_box[0]) - 80) > 5 or abs((after_box[1] - before_box[1]) - 45) > 5:
            raise AssertionError(f"2D middle-button pan did not track mouse displacement: {before_box} -> {after_box}")

        for _ in range(10):
            user32.SendMessageW(canvas, WM_MOUSEWHEEL, (-120 & 0xFFFF) << 16, 0)
        time.sleep(0.1)
        zoomed_out_width = cyan_bbox(capture(canvas, root / "build" / "navigation-zoomed-out.png"))[2]
        user32.SendMessageW(extents, BM_CLICK, 0, 0)
        time.sleep(0.15)
        fitted_image = capture(canvas, root / "build" / "navigation-extents.png")
        fitted = cyan_bbox(fitted_image)
        fitted_width = fitted[2] - fitted[0]
        if fitted_width < rect.right * 0.65:
            raise AssertionError(f"Zoom Extents did not fit the model across the viewport ({fitted_width}px)")

        user32.SendMessageW(zoom_window, BM_CLICK, 0, 0)
        first = (cx - 180, cy - 120)
        second = (cx + 180, cy + 120)
        click(canvas, *first)
        user32.SendMessageW(canvas, WM_MOUSEMOVE, 0, lparam(*second))
        time.sleep(0.1)
        overlay = capture(canvas, root / "build" / "navigation-window-overlay.png")
        blue_overlay_pixels = sum(1 for r, g, b in overlay.convert("RGB").get_flattened_data()
                                  if b > 150 and 70 < g < 190 and r < 100)
        if blue_overlay_pixels < 500:
            raise AssertionError("Zoom Window selection rectangle was not visibly rendered")
        click(canvas, *second)
        time.sleep(0.15)
        window_fitted = capture(canvas, root / "build" / "navigation-window-fitted.png")
        if ImageChops.difference(fitted_image, window_fitted).getbbox() is None:
            raise AssertionError("Zoom Window did not change the 2D view")

        print("2D navigation smoke test passed: middle-button pan, Zoom Extents, and Zoom Window all changed the real canvas.")
    finally:
        if window:
            user32.PostMessageW(window, WM_CLOSE, 0, 0)
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.terminate(); process.wait(timeout=3)


if __name__ == "__main__":
    main()
