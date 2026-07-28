"""Verify the explicit 3D-view switch and three-point work-plane GUI flow."""
import ctypes
import math
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
BM_CLICK = 0x00F5
MK_LBUTTON = 0x0001
SW_MAXIMIZE = 3


def find_window(timeout: float = 5.0) -> int:
    deadline = time.time() + timeout
    while time.time() < deadline:
        hwnd = user32.FindWindowW("ModelMakerWindow", None)
        if hwnd:
            return hwnd
        time.sleep(0.05)
    raise RuntimeError("Model Maker window was not found")


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
    root = Path(__file__).resolve().parents[1]
    process = subprocess.Popen([str(root / "build" / "model-maker.exe")])
    window = 0
    try:
        window = find_window()
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
        cyan = sum(1 for r, g, b in after.get_flattened_data()
                   if g > 145 and b > 150 and r < 115)
        if cyan < 80:
            raise AssertionError(f"Active work-plane grid/border is not visibly highlighted: cyan={cyan}")

        capture(window, root / "build" / "work-plane-maximized-window.png")
        print(f"Work-plane GUI smoke passed ({width}x{height}): 3D switch, 3-point command, and reoriented visible grid; cyan={cyan}")
    finally:
        if window:
            user32.PostMessageW(window, WM_CLOSE, 0, 0)
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=3)


if __name__ == "__main__":
    main()
