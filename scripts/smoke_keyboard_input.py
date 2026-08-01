"""Verify keyboard coordinates and directional distances in native 2D/3D command flows."""
import argparse
import ctypes
import re
import subprocess
import time
from ctypes import wintypes
from pathlib import Path

from smoke_arrays_3d import move_physical_mouse, projected_vertex, window_select_one
from smoke_mirror_snap import WM_MOUSEMOVE, click, find_process_window, send_mouse
from smoke_snap_style_controls import capture, command, status_text, user32

WM_CHAR = 0x0102
WM_RBUTTONDOWN = 0x0204


def model_count(window: int) -> int:
    match = re.search(r"Nesne:\s*(\d+)", status_text(window))
    if not match:
        raise AssertionError(f"Could not parse model count from {status_text(window)!r}")
    return int(match.group(1))


def send_text(canvas: int, text: str) -> None:
    for character in text:
        user32.SendMessageW(canvas, WM_CHAR, ord(character), 0)


def enter(canvas: int) -> None:
    user32.SendMessageW(canvas, WM_CHAR, 13, 0)


def input_point(canvas: int, text: str) -> None:
    send_text(canvas, text)
    enter(canvas)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--keep-open", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    process = subprocess.Popen([str(root / "build" / "model-maker.exe")])
    keep = False
    try:
        window = find_process_window(process.pid)
        user32.ShowWindow(window, 3)
        user32.SetForegroundWindow(window)
        user32.UpdateWindow(window)
        canvas = user32.FindWindowExW(window, 0, "ModelMakerCanvas", None)
        rect = wintypes.RECT()
        for _ in range(40):
            user32.GetClientRect(canvas, ctypes.byref(rect))
            if rect.right >= 1000 and rect.bottom >= 600:
                break
            time.sleep(0.05)
        cx, cy = rect.right // 2, rect.bottom // 2

        # Establish a remembered modifier, then prove Enter still commits drawing coordinates.
        click(canvas, cx - 120, cy)
        click(canvas, cx + 120, cy)
        command(window, 602)
        command(window, 504)
        click(canvas, cx, cy)
        send_mouse(canvas, WM_RBUTTONDOWN, cx, cy)
        if model_count(window) != 0:
            raise AssertionError("Delete setup did not leave an idle remembered modifier")
        command(window, 601)
        command(window, 200)
        input_point(canvas, "-2,-2,0")
        input_point(canvas, "2,2,0")
        if model_count(window) != 1:
            raise AssertionError(
                "Remembered modifier stole Enter instead of committing absolute keyboard coordinates")

        # Single numeric input after the first point must mean exact distance along cursor direction.
        input_point(canvas, "-4,0,0")
        move_physical_mouse(canvas, cx + 220, cy)
        send_text(canvas, "5")
        capture(canvas, root / "build" / "keyboard-distance-input.png")
        enter(canvas)
        if model_count(window) != 2:
            raise AssertionError("Directional keyboard distance did not create the second line")

        # Absolute XYZ coordinates must create a real non-planar line in 3D view.
        command(window, 100)
        command(window, 303)
        command(window, 200)
        input_point(canvas, "0,0,0")
        send_text(canvas, "@1,2,3")
        capture(canvas, root / "build" / "keyboard-xyz-input.png")
        enter(canvas)
        if model_count(window) != 1 or "3B Paralel" not in status_text(window):
            raise AssertionError("Relative @dX,dY,dZ keyboard coordinates did not create a 3D line")

        # Linear Array: keyboard item count + XYZ base point + directional distance.
        command(window, 100)
        command(window, 300)
        command(window, 602)
        command(window, 505)
        window_select_one(canvas, cx, cy)
        send_mouse(canvas, WM_RBUTTONDOWN, cx, cy)
        input_point(canvas, "4")
        input_point(canvas, "0,0,0")
        direction_endpoint = projected_vertex(canvas, 6)
        move_physical_mouse(canvas, *direction_endpoint)
        send_text(canvas, "5")
        capture(canvas, root / "build" / "keyboard-array-distance-input.png")
        enter(canvas)
        if model_count(window) != 4 or "3B Paralel" not in status_text(window):
            raise AssertionError("3D Linear Array did not accept keyboard base coordinates and distance")
        command(window, 602)
        capture(window, root / "build" / "keyboard-input-result.png")

        print("Keyboard-input GUI smoke passed: stale modifier did not steal Enter; "
              "2D XYZ coordinates, directional distance, 3D XYZ, and 3D Array distance committed; "
              f"PID={process.pid}.")
        if args.keep_open:
            keep = True
            print("Verified maximized instance left in 3D view with keyboard-created array geometry.")
    finally:
        if not keep and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()


if __name__ == "__main__":
    main()
