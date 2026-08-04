"""Exercise an intersection OSNAP in the real Win32 canvas and capture it."""
import ctypes
import subprocess
import time
from ctypes import wintypes
from pathlib import Path

from smoke_view_cube import capture

user32 = ctypes.windll.user32
WM_CLOSE = 0x0010
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
WM_MOUSEMOVE = 0x0200
WM_KEYDOWN = 0x0100
WM_COMMAND = 0x0111
MK_LBUTTON = 0x0001
VK_ESCAPE = 0x1B
CMD_ZOOM_WINDOW = 306


def find_window(class_name: str, timeout: float = 5.0) -> int:
    deadline = time.time() + timeout
    while time.time() < deadline:
        hwnd = user32.FindWindowW(class_name, None)
        if hwnd:
            return hwnd
        time.sleep(0.05)
    raise RuntimeError(f"Window class was not found: {class_name}")


def send_mouse(hwnd: int, message: int, x: int, y: int, buttons: int = 0) -> None:
    lparam = (y << 16) | (x & 0xFFFF)
    user32.SendMessageW(hwnd, message, buttons, lparam)


def click(hwnd: int, x: int, y: int) -> None:
    send_mouse(hwnd, WM_LBUTTONDOWN, x, y, MK_LBUTTON)
    send_mouse(hwnd, WM_LBUTTONUP, x, y)


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

        # Two asymmetric lines cross at the origin, away from both midpoints.
        click(canvas, cx - 60, cy)
        click(canvas, cx + 180, cy)
        click(canvas, cx, cy + 120)
        click(canvas, cx, cy - 60)
        send_mouse(canvas, WM_MOUSEMOVE, cx + 2, cy + 2)
        time.sleep(0.2)

        screenshot = capture(canvas, root / "build" / "osnap-intersection.png")
        crop = screenshot.crop((cx - 15, cy - 15, cx + 16, cy + 16))
        green = sum(1 for red, green, blue in crop.get_flattened_data()
                    if green > 180 and green > red * 1.35 and green > blue * 1.05)
        if green < 8:
            raise AssertionError("Intersection OSNAP marker was not painted at the crossing")

        user32.SendMessageW(canvas, WM_KEYDOWN, ord('M'), 0)
        send_mouse(canvas, WM_MOUSEMOVE, cx + 2, cy + 2)
        selection_crop = capture(canvas, root / "build" / "osnap-selection-suppressed.png").crop(
            (cx - 15, cy - 15, cx + 16, cy + 16))
        selection_green = sum(1 for red, green, blue in selection_crop.get_flattened_data()
                              if green > 180 and green > red * 1.35 and green > blue * 1.05)
        if selection_green >= 8:
            raise AssertionError("OSNAP marker remained active during entity selection")

        user32.SendMessageW(canvas, WM_KEYDOWN, VK_ESCAPE, 0)
        user32.SendMessageW(window, WM_COMMAND, CMD_ZOOM_WINDOW, 0)
        send_mouse(canvas, WM_MOUSEMOVE, cx + 2, cy + 2)
        zoom_crop = capture(canvas, root / "build" / "osnap-zoom-suppressed.png").crop(
            (cx - 15, cy - 15, cx + 16, cy + 16))
        zoom_green = sum(1 for red, green, blue in zoom_crop.get_flattened_data()
                         if green > 180 and green > red * 1.35 and green > blue * 1.05)
        if zoom_green >= 8:
            raise AssertionError("OSNAP marker remained active during Zoom Window")
        print(f"OSNAP smoke passed: normal={green}, selection={selection_green}, zoom={zoom_green} green pixels.")
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
