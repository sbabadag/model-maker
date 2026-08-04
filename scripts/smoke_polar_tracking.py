"""Native 2D/3D verification for 90-degree F10 Polar Tracking."""
import argparse
import ctypes
import math
import re
import subprocess
import time
from ctypes import wintypes
from pathlib import Path

from smoke_arrays_3d import move_physical_mouse, projected_vertex
from smoke_mirror_snap import click, find_process_window
from smoke_snap_style_controls import capture, command, status_text, user32

WM_CHAR = 0x0102
WM_KEYDOWN = 0x0100
BM_GETCHECK = 0x00F0
BST_CHECKED = 1
VK_F8 = 0x77
VK_F10 = 0x79


def send_key(canvas: int, key: int) -> None:
    user32.SendMessageW(canvas, WM_KEYDOWN, key, 0)
    time.sleep(0.08)


def input_point(canvas: int, text: str) -> None:
    for character in text:
        user32.SendMessageW(canvas, WM_CHAR, ord(character), 0)
    user32.SendMessageW(canvas, WM_CHAR, 13, 0)
    time.sleep(0.05)


def checked(window: int, command_id: int) -> bool:
    control = user32.GetDlgItem(window, command_id)
    if not control:
        raise AssertionError(f"Control {command_id} was not created")
    return int(user32.SendMessageW(control, BM_GETCHECK, 0, 0)) == BST_CHECKED


def require_tracking(window: int, expected: bool, context: str) -> None:
    status = status_text(window)
    tracked = "POLAR F10: 90° İzleme" in status
    if tracked != expected:
        raise AssertionError(f"{context}: expected tracking={expected}, status={status!r}")


def model_count(window: int) -> int:
    match = re.search(r"Nesne:\s*(\d+)", status_text(window))
    if not match:
        raise AssertionError(f"Could not parse model count: {status_text(window)!r}")
    return int(match.group(1))


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

        # Aids ribbon button and F10 state.
        command(window, 604)
        if checked(window, 404):
            raise AssertionError("Polar Tracking must start disabled")
        command(window, 404)
        if not checked(window, 404) or "POLAR F10: 90° Açık" not in status_text(window):
            raise AssertionError("Polar F10 button did not enable 90-degree tracking")
        command(window, 401)  # Use the raw cursor ray; object snaps remain enabled for precedence checks.

        # In 2D, a cursor within the 12-degree aperture locks; 30 degrees remains free.
        command(window, 601)
        command(window, 200)
        click(canvas, cx, cy)
        move_physical_mouse(canvas, cx + 240, cy - 36)
        require_tracking(window, True, "2D near-horizontal ray")
        capture(window, root / "build" / "polar-tracking-2d.png")
        move_physical_mouse(canvas, cx + 210, cy - 121)
        require_tracking(window, False, "2D free 30-degree movement")
        move_physical_mouse(canvas, cx + 240, cy - 36)
        require_tracking(window, True, "2D lock restored")
        click(canvas, cx + 240, cy - 36)
        if model_count(window) != 1:
            raise AssertionError("2D Polar Tracking did not commit the tracked line")

        # F8 and F10 are mutually exclusive, while each remains independently usable.
        send_key(canvas, VK_F8)
        if checked(window, 404) or "ORTHO F8: Açık" not in status_text(window):
            raise AssertionError("F8 must disable Polar Tracking and enable strict Ortho")
        send_key(canvas, VK_F10)
        if not checked(window, 404) or "ORTHO F8: Kapalı" not in status_text(window):
            raise AssertionError("F10 must enable Polar Tracking and disable Ortho")

        # In 3D, tracking follows the active work-plane U/V axes, not screen horizontal/vertical.
        command(window, 100)
        command(window, 303)
        command(window, 200)
        input_point(canvas, "0,0,0")
        input_point(canvas, "10,0,0")
        input_point(canvas, "0,0,0")
        origin = projected_vertex(canvas, 0)
        x_axis = projected_vertex(canvas, 1)
        dx, dy = x_axis[0] - origin[0], x_axis[1] - origin[1]
        length = math.hypot(dx, dy)
        if length < 40:
            raise AssertionError("Projected 3D X axis is unexpectedly short")
        # Stay outside the 10px endpoint snap aperture but within the 12-degree polar aperture.
        target = (x_axis[0] + round(-dy * 20.0 / length),
                  x_axis[1] + round(dx * 20.0 / length))
        move_physical_mouse(canvas, *target)
        require_tracking(window, True, "3D active-work-plane X ray")
        capture(window, root / "build" / "polar-tracking-3d.png")
        click(canvas, *target)
        if model_count(window) != 2 or "3B Paralel" not in status_text(window):
            raise AssertionError("3D Polar Tracking did not commit on the active work plane")

        command(window, 604)
        capture(window, root / "build" / "polar-tracking-result.png")
        print("Polar Tracking GUI smoke passed: F10/90°, 12° aperture, free off-angle motion, "
              "F8 mutual exclusion, 2D lock, and 3D work-plane lock verified; "
              f"PID={process.pid}.")
        if args.keep_open:
            keep = True
            print("Verified maximized instance left open with F10 Polar active in 3D.")
    finally:
        if not keep and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()


if __name__ == "__main__":
    main()
