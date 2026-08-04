"""Verify the Offset modifier through the real Win32 canvas flow."""
import ctypes
import subprocess
import time
from ctypes import wintypes
from pathlib import Path

from smoke_mirror_snap import find_process_window
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
SW_MAXIMIZE = 3


def send_mouse(hwnd: int, message: int, x: int, y: int, buttons: int = 0) -> None:
    user32.SendMessageW(hwnd, message, buttons, (y << 16) | (x & 0xFFFF))


def click(hwnd: int, x: int, y: int) -> None:
    send_mouse(hwnd, WM_LBUTTONDOWN, x, y, MK_LBUTTON)
    send_mouse(hwnd, WM_LBUTTONUP, x, y)


def cyan_count(image, box: tuple[int, int, int, int]) -> int:
    return sum(
        1 for red, green, blue in image.crop(box).convert("RGB").get_flattened_data()
        if blue > 210 and green > 150 and red < 150
    )


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    process = subprocess.Popen([str(root / "build" / "model-maker.exe")])
    window = 0
    try:
        window = find_process_window(process.pid)
        user32.ShowWindow(window, SW_MAXIMIZE)
        canvas = user32.FindWindowExW(window, 0, "ModelMakerCanvas", None)
        if not canvas:
            raise RuntimeError("Canvas window was not found")
        client = wintypes.RECT()
        user32.GetClientRect(canvas, ctypes.byref(client))
        center_x, center_y = client.right // 2, client.bottom // 2

        # Draw a six-unit horizontal source line with grid snapping disabled.
        user32.SendMessageW(canvas, WM_KEYDOWN, VK_F9, 0)
        click(canvas, center_x - 180, center_y)
        click(canvas, center_x + 180, center_y)

        # O -> select line -> Enter -> distance 2 -> Enter -> pick upper side.
        user32.SendMessageW(canvas, WM_KEYDOWN, ord("O"), 0)
        click(canvas, center_x, center_y)
        user32.SendMessageW(canvas, WM_CHAR, 13, 0)
        user32.SendMessageW(canvas, WM_CHAR, ord("2"), 0)
        user32.SendMessageW(canvas, WM_CHAR, 13, 0)
        send_mouse(canvas, WM_MOUSEMOVE, center_x, center_y - 120)
        click(canvas, center_x, center_y - 120)
        time.sleep(0.25)

        image = capture(canvas, root / "build" / "offset-modifier.png")
        source = cyan_count(image, (center_x - 175, center_y - 3, center_x + 175, center_y + 4))
        offset = cyan_count(image, (center_x - 175, center_y - 123, center_x + 175, center_y - 116))
        if source < 250 or offset < 250:
            raise AssertionError(f"Offset flow did not render both parallel lines (source={source}, offset={offset})")
        print(f"Offset smoke passed: source={source}, offset={offset}; distance=2 units on picked side.")
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
