"""Verify a typed scalar draws the requested length toward the cursor."""
import ctypes
import subprocess
import time
from ctypes import wintypes
from pathlib import Path

from smoke_view_cube import capture

user32 = ctypes.windll.user32
WM_CLOSE = 0x0010
WM_CHAR = 0x0102
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


def click(hwnd: int, x: int, y: int) -> None:
    send_mouse(hwnd, WM_LBUTTONDOWN, x, y, MK_LBUTTON)
    send_mouse(hwnd, WM_LBUTTONUP, x, y)


def cyan_count(image, box: tuple[int, int, int, int]) -> int:
    return sum(
        1 for red, green, blue in image.crop(box).convert("RGB").get_flattened_data()
        if blue > 220 and green > 160 and red < 150
    )


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
        start_x, start_y = client.right // 2 - 200, client.bottom // 2 + 100

        # Free drafting: establish direction with the cursor, then enter a 2-unit length.
        user32.SendMessageW(canvas, WM_KEYDOWN, VK_F9, 0)
        click(canvas, start_x, start_y)
        send_mouse(canvas, WM_MOUSEMOVE, start_x + 200, start_y)
        user32.SendMessageW(canvas, WM_CHAR, ord("2"), 0)
        user32.SendMessageW(canvas, WM_CHAR, 13, 0)
        time.sleep(0.2)

        screenshot = capture(canvas, root / "build" / "directional-distance.png")
        expected = cyan_count(screenshot, (start_x + 8, start_y - 3, start_x + 116, start_y + 4))
        beyond = cyan_count(screenshot, (start_x + 132, start_y - 3, start_x + 188, start_y + 4))
        if expected < 80:
            raise AssertionError(f"Typed distance did not produce the expected 120-pixel line ({expected} cyan pixels)")
        if beyond > 4:
            raise AssertionError(f"Line continued beyond the typed 2-unit endpoint ({beyond} cyan pixels)")
        print(f"Directional distance smoke test passed: {expected} line pixels, {beyond} beyond endpoint.")
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
