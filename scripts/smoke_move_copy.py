"""Exercise AutoCAD-style select/base/destination Move and multi-copy flows."""
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


def press(hwnd: int, key: str) -> None:
    user32.SendMessageW(hwnd, WM_KEYDOWN, ord(key), 0)


def enter(hwnd: int) -> None:
    user32.SendMessageW(hwnd, WM_CHAR, 13, 0)


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
        cx, cy = client.right // 2, client.bottom // 2
        start = (cx - 180, cy - 120)
        end = (cx - 60, cy - 120)

        # Draw a deterministic 2-unit source line with free cursor input.
        user32.SendMessageW(canvas, WM_KEYDOWN, VK_F9, 0)
        click(canvas, *start)
        click(canvas, *end)

        # MOVE: select object, Enter, base point, destination point.
        press(canvas, "M")
        click(canvas, cx - 120, cy - 120)
        enter(canvas)
        click(canvas, *start)
        moved_start = (start[0], start[1] + 120)
        click(canvas, *moved_start)
        time.sleep(0.15)
        moved = capture(canvas, root / "build" / "move-command.png")
        old_pixels = cyan_count(moved, (start[0] + 8, start[1] - 3, end[0] - 8, start[1] + 4))
        moved_pixels = cyan_count(moved, (moved_start[0] + 8, moved_start[1] - 3,
                                          moved_start[0] + 112, moved_start[1] + 4))
        if old_pixels > 5 or moved_pixels < 80:
            raise AssertionError(f"MOVE result mismatch: old={old_pixels}, moved={moved_pixels}")

        # COPY: same selection/base/destination flow; original must remain.
        press(canvas, "K")
        click(canvas, moved_start[0] + 60, moved_start[1])
        enter(canvas)
        click(canvas, *moved_start)
        copied_start = (moved_start[0] + 180, moved_start[1])
        click(canvas, *copied_start)
        enter(canvas)  # COPY is multiple by default; Enter finishes it.
        time.sleep(0.15)
        copied = capture(canvas, root / "build" / "copy-command.png")
        source_pixels = cyan_count(copied, (moved_start[0] + 8, moved_start[1] - 3,
                                            moved_start[0] + 112, moved_start[1] + 4))
        copy_pixels = cyan_count(copied, (copied_start[0] + 8, copied_start[1] - 3,
                                          copied_start[0] + 112, copied_start[1] + 4))
        if source_pixels < 80 or copy_pixels < 80:
            raise AssertionError(f"COPY result mismatch: source={source_pixels}, copy={copy_pixels}")

        print(f"Move/Copy smoke test passed: move={moved_pixels}, source={source_pixels}, copy={copy_pixels} pixels.")
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
