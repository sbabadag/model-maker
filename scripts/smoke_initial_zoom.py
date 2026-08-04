"""Verify mouse-wheel zoom changes the empty startup canvas."""
import ctypes
import subprocess
import time
from pathlib import Path

from PIL import ImageChops

from smoke_view_cube import capture

user32 = ctypes.windll.user32

WM_CLOSE = 0x0010
WM_MOUSEWHEEL = 0x020A
WHEEL_DELTA = 120


def find_window(class_name: str, timeout: float = 5.0) -> int:
    deadline = time.time() + timeout
    while time.time() < deadline:
        hwnd = user32.FindWindowW(class_name, None)
        if hwnd:
            return hwnd
        time.sleep(0.05)
    raise RuntimeError(f"Window class was not found: {class_name}")


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    process = subprocess.Popen([str(root / "build" / "model-maker.exe")])
    window = 0
    try:
        window = find_window("ModelMakerWindow")
        canvas = user32.FindWindowExW(window, 0, "ModelMakerCanvas", None)
        if not canvas:
            raise RuntimeError("Canvas window was not found")

        before = capture(canvas, root / "build" / "initial-before-zoom.png")
        user32.SendMessageW(canvas, WM_MOUSEWHEEL, WHEEL_DELTA << 16, 0)
        time.sleep(0.2)
        after = capture(canvas, root / "build" / "initial-after-zoom.png")

        comparison_box = (100, 100, before.width, before.height)
        if ImageChops.difference(before.crop(comparison_box), after.crop(comparison_box)).getbbox() is None:
            raise AssertionError("Mouse wheel did not change the empty startup canvas")
        print("Initial zoom smoke test passed: wheel changed the empty startup canvas.")
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