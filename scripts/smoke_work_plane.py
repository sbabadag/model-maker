"""Verify a gray borderless work-plane grid with local XYZ arrows at its base point."""
import argparse
import ctypes
import math
import subprocess
import time
from ctypes import wintypes
from pathlib import Path

from PIL import ImageChops
from smoke_mirror_snap import find_process_window
from smoke_view_cube import capture

user32 = ctypes.windll.user32
WM_CLOSE = 0x0010
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
BM_CLICK = 0x00F5
MK_LBUTTON = 0x0001
SW_MAXIMIZE = 3



def click(hwnd: int, x: int, y: int) -> None:
    user32.SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, (y << 16) | (x & 0xFFFF))
    user32.SendMessageW(hwnd, WM_LBUTTONUP, 0, (y << 16) | (x & 0xFFFF))


def status_text(parent: int) -> str:
    result = ""
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    @callback_type
    def collect(hwnd: int, _lparam: int) -> bool:
        nonlocal result
        length = user32.GetWindowTextLengthW(hwnd)
        if length:
            buffer = ctypes.create_unicode_buffer(length + 1)
            user32.GetWindowTextW(hwnd, buffer, len(buffer))
            if "OSNAP:" in buffer.value:
                result = buffer.value
        return True

    user32.EnumChildWindows(parent, collect, 0)
    return result


def project(point: tuple[float, float, float], width: int, height: int) -> tuple[int, int]:
    x, y, z = point
    yaw, pitch, scale = -0.55, 0.45, 65.0
    cy, sy = math.cos(yaw), math.sin(yaw)
    cp, sp = math.cos(pitch), math.sin(pitch)
    yaw_x = x * cy + z * sy
    yaw_z = -x * sy + z * cy
    view_y = y * cp - yaw_z * sp
    return round(width * 0.5 + yaw_x * scale), round(height * 0.5 - view_y * scale)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--keep-open", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    process = subprocess.Popen([str(root / "build" / "model-maker.exe")])
    window = 0
    keep = False
    try:
        window = find_process_window(process.pid)
        user32.ShowWindow(window, SW_MAXIMIZE)
        time.sleep(0.2)
        canvas = user32.FindWindowExW(window, 0, "ModelMakerCanvas", None)
        if not canvas:
            raise RuntimeError("Canvas was not found")

        user32.SendMessageW(user32.GetDlgItem(window, 603), BM_CLICK, 0, 0)
        user32.SendMessageW(user32.GetDlgItem(window, 303), BM_CLICK, 0, 0)
        time.sleep(0.15)
        if "3B Paralel" not in status_text(window):
            raise AssertionError("The 3B Görünüm button did not switch the empty canvas to 3D")
        empty_3d = capture(canvas, root / "build" / "work-plane-empty-3d.png")

        user32.SendMessageW(user32.GetDlgItem(window, 300), BM_CLICK, 0, 0)
        time.sleep(0.15)
        before = capture(canvas, root / "build" / "work-plane-before.png")
        user32.SendMessageW(user32.GetDlgItem(window, 304), BM_CLICK, 0, 0)
        if "1. noktayı" not in status_text(window):
            raise AssertionError("Work-plane command did not request its first point")

        rect = wintypes.RECT()
        user32.GetClientRect(canvas, ctypes.byref(rect))
        width, height = rect.right, rect.bottom
        points = [(-1.3, -1.3, -1.3), (1.3, -1.3, -1.3), (-1.3, -1.3, 1.3)]
        click(canvas, *project(points[0], width, height))
        if "2. noktayı" not in status_text(window):
            raise AssertionError("Work-plane command did not advance to its second point")
        click(canvas, *project(points[1], width, height))
        if "3. noktayı" not in status_text(window):
            raise AssertionError("Work-plane command did not advance to its third point")
        click(canvas, *project(points[2], width, height))
        time.sleep(0.25)
        if "WORK PLANE —" in status_text(window):
            raise AssertionError("Work-plane command did not complete after three valid points")

        after = capture(canvas, root / "build" / "work-plane-after.png")
        scene_box = (40, 40, max(41, width - 190), height - 30)
        if ImageChops.difference(before.crop(scene_box), after.crop(scene_box)).getbbox() is None:
            raise AssertionError("The visible grid did not reorient to the three-point work plane")
        scene = after.crop(scene_box)
        pixels = list(scene.get_flattened_data())
        gray = sum(1 for r, g, b in pixels if 48 <= r <= 90 and abs(r - g) <= 8 and abs(g - b) <= 8)
        cyan_border = sum(1 for r, g, b in pixels if r < 110 and g > 165 and b > 175)
        before_cyan = sum(1 for r, g, b in before.crop(scene_box).get_flattened_data()
                          if r < 110 and g > 165 and b > 175)
        red_axis = sum(1 for r, g, b in pixels if r > 175 and g < 125 and b < 150)
        green_axis = sum(1 for r, g, b in pixels if g > 170 and r < 125 and b < 165)
        blue_axis = sum(1 for r, g, b in pixels if b > 190 and r < 130 and g < 170)
        if gray < 1000:
            raise AssertionError(f"Work-plane grid is not visibly gray: gray={gray}")
        if cyan_border > before_cyan + 20:
            raise AssertionError(
                f"Legacy cyan work-plane border/label added pixels: before={before_cyan}, after={cyan_border}")
        if min(red_axis, green_axis, blue_axis) < 10:
            raise AssertionError(
                f"Local XYZ arrows are not all visible: red={red_axis}, green={green_axis}, blue={blue_axis}")

        capture(window, root / "build" / "work-plane-maximized-window.png")
        print(f"Work-plane GUI smoke passed ({width}x{height}): gray={gray}, cyan={before_cyan}->{cyan_border}, "
              f"XYZ={red_axis}/{green_axis}/{blue_axis}; grid reoriented without an outer contour.")
        if args.keep_open:
            keep = True
            print(f"Verified maximized work-plane instance left open; PID={process.pid}.")
    finally:
        if window and not keep:
            user32.PostMessageW(window, WM_CLOSE, 0, 0)
        if not keep:
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=3)


if __name__ == "__main__":
    main()
