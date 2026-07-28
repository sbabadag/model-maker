"""Verify AutoCAD-style window and crossing selection for Move/Copy commands."""
import ctypes
import subprocess
import time
from ctypes import wintypes
from pathlib import Path

from smoke_view_cube import capture

user32 = ctypes.windll.user32
WM_CLOSE = 0x0010
WM_KEYDOWN = 0x0100
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
WM_MOUSEMOVE = 0x0200
MK_LBUTTON = 0x0001
VK_F9 = 0x78


def find_window(class_name: str, timeout: float = 5.0) -> int:
    deadline = time.time() + timeout
    while time.time() < deadline:
        hwnd = user32.FindWindowW(class_name, None)
        if hwnd:
            return hwnd
        time.sleep(0.05)
    raise RuntimeError(f"Window class was not found: {class_name}")


def send_mouse(hwnd: int, message: int, x: int, y: int, buttons: int = 0) -> None:
    user32.SendMessageW(hwnd, message, buttons, (y << 16) | (x & 0xFFFF))


def click(hwnd: int, point: tuple[int, int]) -> None:
    send_mouse(hwnd, WM_LBUTTONDOWN, *point, MK_LBUTTON)
    send_mouse(hwnd, WM_LBUTTONUP, *point)


def move(hwnd: int, point: tuple[int, int]) -> None:
    send_mouse(hwnd, WM_MOUSEMOVE, *point)


def color_count(image, box, predicate) -> int:
    return sum(1 for pixel in image.crop(box).convert("RGB").get_flattened_data() if predicate(*pixel))


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    process = subprocess.Popen([str(root / "build" / "model-maker.exe")])
    window = 0
    try:
        window = find_window("ModelMakerWindow")
        canvas = user32.FindWindowExW(window, 0, "ModelMakerCanvas", None)
        if not canvas:
            raise RuntimeError("Canvas window was not found")
        client = wintypes.RECT()
        user32.GetClientRect(canvas, ctypes.byref(client))
        cx, cy = client.right // 2, client.bottom // 2

        # Two separate lines: first can be enclosed; second will only cross the rectangle.
        contained_a, contained_b = (cx - 240, cy - 120), (cx - 160, cy - 120)
        crossing_a, crossing_b = (cx + 20, cy + 20), (cx + 180, cy + 20)
        user32.SendMessageW(canvas, WM_KEYDOWN, VK_F9, 0)  # free pixel placement
        click(canvas, contained_a); click(canvas, contained_b)
        click(canvas, crossing_a); click(canvas, crossing_b)

        user32.SendMessageW(canvas, WM_KEYDOWN, ord("M"), 0)

        # Left-to-right WINDOW preview and completion: only the enclosed first line is selected.
        window_first = (contained_a[0] - 20, contained_a[1] - 35)
        window_second = (contained_b[0] + 20, contained_b[1] + 35)
        click(canvas, window_first)
        move(canvas, window_second)
        time.sleep(0.1)
        window_preview = capture(canvas, root / "build" / "selection-window-preview.png")
        blue = color_count(window_preview,
                           (window_first[0], window_first[1], window_second[0] + 1, window_second[1] + 1),
                           lambda r, g, b: b > 170 and g > 90 and r < 100)
        if blue < 80:
            raise AssertionError(f"Blue WINDOW rectangle was not visibly rendered: {blue} pixels")
        click(canvas, window_second)

        # Right-to-left CROSSING preview and completion: the boundary-crossing second line is selected.
        crossing_first = (cx + 130, cy + 55)
        crossing_second = (cx + 70, cy - 15)
        click(canvas, crossing_first)
        move(canvas, crossing_second)
        time.sleep(0.1)
        crossing_preview = capture(canvas, root / "build" / "selection-crossing-preview.png")
        green_box = color_count(crossing_preview,
                                (crossing_second[0], crossing_second[1], crossing_first[0] + 1, crossing_first[1] + 1),
                                lambda r, g, b: g > 130 and r < 110 and b < 150)
        if green_box < 70:
            raise AssertionError(f"Green CROSSING rectangle was not visibly rendered: {green_box} pixels")
        click(canvas, crossing_second)
        time.sleep(0.1)
        selected = capture(canvas, root / "build" / "selection-window-crossing-result.png")
        selected_green = lambda r, g, b: g > 220 and 55 < r < 130 and 100 < b < 180
        first_count = color_count(selected,
                                  (contained_a[0] + 5, contained_a[1] - 4, contained_b[0] - 5, contained_b[1] + 5),
                                  selected_green)
        second_count = color_count(selected,
                                   (crossing_a[0] + 5, crossing_a[1] - 4, crossing_b[0] - 5, crossing_b[1] + 5),
                                   selected_green)
        if first_count < 45 or second_count < 80:
            raise AssertionError(f"Window/crossing selection result mismatch: {first_count}, {second_count}")
        print(f"Selection window smoke passed: blue={blue}, crossing={green_box}, selected={first_count}/{second_count} pixels.")
    finally:
        if window:
            user32.PostMessageW(window, WM_CLOSE, 0, 0)
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.terminate(); process.wait(timeout=3)


if __name__ == "__main__":
    main()
