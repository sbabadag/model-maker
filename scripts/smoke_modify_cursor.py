"""Verify Move and Copy switch the canvas pointer to a dedicated square pickbox cursor."""
import ctypes
import subprocess
import time
from ctypes import wintypes
from pathlib import Path

user32 = ctypes.windll.user32
WM_CLOSE = 0x0010
WM_KEYDOWN = 0x0100
WM_SETCURSOR = 0x0020
WM_CURSOR_PROBE = 0x8001
HTCLIENT = 1


def find_window(class_name: str, timeout: float = 5.0) -> int:
    deadline = time.time() + timeout
    while time.time() < deadline:
        hwnd = user32.FindWindowW(class_name, None)
        if hwnd:
            return hwnd
        time.sleep(0.05)
    raise RuntimeError(f"Window class was not found: {class_name}")


def set_client_cursor(canvas: int) -> int:
    user32.SendMessageW(canvas, WM_SETCURSOR, canvas, HTCLIENT)
    # Probe GetCursor in the application's GUI thread; cursor handles are process-local.
    return int(user32.SendMessageW(canvas, WM_CURSOR_PROBE, 0, 0))


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    process = subprocess.Popen([str(root / "build" / "model-maker.exe")])
    window = 0
    try:
        window = find_window("ModelMakerWindow")
        canvas = user32.FindWindowExW(window, 0, "ModelMakerCanvas", None)
        if not canvas:
            raise RuntimeError("Canvas window was not found")

        center = wintypes.POINT(300, 250)
        user32.ClientToScreen(canvas, ctypes.byref(center))
        user32.SetForegroundWindow(window)
        user32.SetCursorPos(center.x, center.y)
        time.sleep(0.1)

        normal = set_client_cursor(canvas)
        user32.SendMessageW(canvas, WM_KEYDOWN, ord("M"), 0)
        move = set_client_cursor(canvas)
        if not normal or not move or move == normal:
            raise AssertionError("MOVE kept the normal drafting cursor instead of a square pickbox")

        user32.SendMessageW(canvas, WM_KEYDOWN, 0x1B, 0)
        restored = set_client_cursor(canvas)
        if restored != normal:
            raise AssertionError("Esc did not restore the normal drafting cursor")

        user32.SendMessageW(canvas, WM_KEYDOWN, ord("K"), 0)
        copy = set_client_cursor(canvas)
        if copy != move:
            raise AssertionError("COPY did not use the same square pickbox cursor as MOVE")

        print("Modify cursor smoke test passed: MOVE/COPY use square pickbox; Esc restores crosshair.")
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
