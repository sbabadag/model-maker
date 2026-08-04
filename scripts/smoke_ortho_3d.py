"""Verify F8 Ortho locks real 3D drafting previews to global X, Y, and Z."""
import ctypes
import math
import re
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
VK_F3 = 0x72
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


def send_key(hwnd: int, key: int) -> None:
    user32.SendMessageW(hwnd, WM_KEYDOWN, key, 0)


def send_mouse(hwnd: int, message: int, x: int, y: int, buttons: int = 0) -> None:
    user32.SendMessageW(hwnd, message, buttons, (y << 16) | (x & 0xFFFF))


def click(hwnd: int, x: int, y: int) -> None:
    send_mouse(hwnd, WM_LBUTTONDOWN, x, y, MK_LBUTTON)
    send_mouse(hwnd, WM_LBUTTONUP, x, y)


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
            if "ORTHO F8:" in buffer.value:
                result = buffer.value
        return True

    user32.EnumChildWindows(parent, collect, 0)
    return result


def coordinates(text: str) -> tuple[float, float, float]:
    match = re.search(r"X\s+(-?\d+\.\d+)\s+Y\s+(-?\d+\.\d+)\s+Z\s+(-?\d+\.\d+)", text)
    if not match:
        raise AssertionError(f"XYZ coordinates were not found in status: {text!r}")
    return tuple(float(value) for value in match.groups())


def projected_delta(axis: str, distance: float) -> tuple[int, int]:
    yaw, pitch, scale = -0.55, 0.45, 65.0
    cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    vectors = {
        "X": (cy, -sy * sp),
        "Y": (0.0, -cp),
        "Z": (sy, cy * sp),
    }
    dx, dy = vectors[axis]
    return round(dx * scale * distance), round(dy * scale * distance)


def close_to(actual: tuple[float, float, float], expected: tuple[float, float, float], tolerance: float = 0.03) -> bool:
    return all(abs(a - e) <= tolerance for a, e in zip(actual, expected))


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    process = subprocess.Popen([str(root / "build" / "model-maker.exe")])
    window = 0
    try:
        window = find_window("ModelMakerWindow")
        canvas = user32.FindWindowExW(window, 0, "ModelMakerCanvas", None)
        if not canvas:
            raise RuntimeError("Canvas window was not found")

        send_key(canvas, ord("B"))  # Enter 3D view.
        send_key(canvas, ord("L"))  # Draft a line without leaving 3D.
        send_key(canvas, VK_F3)      # Disable OSNAP for a free Ortho preview.
        send_key(canvas, VK_F9)      # Disable grid quantization.
        send_key(canvas, VK_F8)      # Enable three-axis Ortho.

        rect = wintypes.RECT()
        user32.GetClientRect(canvas, ctypes.byref(rect))
        cx, cy = rect.right // 2, rect.bottom // 2
        click(canvas, cx, cy)
        anchor = coordinates(status_text(window))

        results: dict[str, tuple[float, float, float]] = {}
        for index, axis in enumerate(("X", "Y", "Z")):
            dx, dy = projected_delta(axis, 3.0)
            send_mouse(canvas, WM_MOUSEMOVE, cx + dx, cy + dy)
            # Keyboard routing refreshes both the hover constraint and the native status bar.
            send_key(canvas, VK_F8)
            send_key(canvas, VK_F8)
            time.sleep(0.08)
            point = coordinates(status_text(window))
            expected = list(anchor)
            expected[index] += 3.0
            if not close_to(point, tuple(expected)):
                raise AssertionError(f"3D F8 did not lock to {axis}: expected {tuple(expected)}, got {point}")
            results[axis] = point

        text = status_text(window)
        if "ORTHO F8: Açık (X/Y/Z)" not in text:
            raise AssertionError(f"Three-axis Ortho state is not visible in status: {text!r}")
        capture(canvas, root / "build" / "ortho-f8-3d-xyz.png")
        print(f"3D F8 Ortho smoke test passed for global X/Y/Z: {results}")
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
