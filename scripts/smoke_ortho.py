"""Verify F8 Ortho locks a real Win32 drawing preview horizontally or vertically."""
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
VK_F8 = 0x77
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


def child_texts(parent: int) -> list[str]:
    texts: list[str] = []
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    @callback_type
    def collect(hwnd: int, _lparam: int) -> bool:
        length = user32.GetWindowTextLengthW(hwnd)
        if length:
            buffer = ctypes.create_unicode_buffer(length + 1)
            user32.GetWindowTextW(hwnd, buffer, len(buffer))
            texts.append(buffer.value)
        return True

    user32.EnumChildWindows(parent, collect, 0)
    return texts


def yellow_count(image, box: tuple[int, int, int, int]) -> int:
    return sum(
        1 for red, green, blue in image.crop(box).convert("RGB").get_flattened_data()
        if red > 180 and green > 140 and blue < 150
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

        # Remove grid quantization, enable Ortho, set the first point, then move diagonally.
        user32.SendMessageW(canvas, WM_KEYDOWN, VK_F9, 0)
        user32.SendMessageW(canvas, WM_KEYDOWN, VK_F8, 0)
        click(canvas, cx, cy)
        send_mouse(canvas, WM_MOUSEMOVE, cx + 140, cy + 55)
        time.sleep(0.2)

        texts = child_texts(window)
        if not any("ORTHO F8: Açık" in text for text in texts):
            raise AssertionError("F8 did not enable Ortho in the status bar")

        screenshot = capture(canvas, root / "build" / "ortho-f8.png")
        locked = yellow_count(screenshot, (cx + 100, cy - 4, cx + 136, cy + 5))
        diagonal = yellow_count(screenshot, (cx + 100, cy + 38, cx + 136, cy + 59))
        if locked < 4:
            raise AssertionError("Ortho preview was not painted on the horizontal axis")
        if diagonal > 2:
            raise AssertionError("Ortho preview still followed diagonal mouse movement")
        print(f"F8 Ortho smoke test passed: {locked} locked-axis pixels, {diagonal} diagonal pixels.")
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
