"""Verify Move/Copy destination previews show a base-to-cursor tracker line."""
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
    send_mouse(hwnd, WM_LBUTTONDOWN, x, y, 1)
    send_mouse(hwnd, WM_LBUTTONUP, x, y)


def yellow_count(image, box: tuple[int, int, int, int]) -> int:
    return sum(
        1 for red, green, blue in image.crop(box).convert("RGB").get_flattened_data()
        if red > 190 and green > 145 and blue < 130
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
        cx, cy = client.right // 2, client.bottom // 2

        # Create and select a horizontal source away from the tracker probe.
        user32.SendMessageW(canvas, WM_KEYDOWN, VK_F9, 0)
        source_a = (cx - 190, cy - 130)
        source_b = (cx - 70, cy - 130)
        click(canvas, *source_a)
        click(canvas, *source_b)
        user32.SendMessageW(canvas, WM_KEYDOWN, ord("M"), 0)
        click(canvas, cx - 130, cy - 130)
        user32.SendMessageW(canvas, WM_CHAR, 13, 0)

        # Base and cursor form a vertical path. Do not click the destination.
        base = (cx + 40, cy - 90)
        destination = (cx + 40, cy + 110)
        click(canvas, *base)
        send_mouse(canvas, WM_MOUSEMOVE, *destination)
        time.sleep(0.15)
        image = capture(canvas, root / "build" / "transform-tracker.png")

        # Probe the middle of the path, away from base, cursor, and translated geometry.
        tracker_pixels = yellow_count(image, (base[0] - 3, base[1] + 45,
                                              base[0] + 4, destination[1] - 45))
        if tracker_pixels < 18:
            raise AssertionError(f"Transform tracker line missing: yellow pixels={tracker_pixels}")
        print(f"Transform tracker smoke test passed: {tracker_pixels} yellow path pixels.")
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
