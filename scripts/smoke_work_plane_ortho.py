"""Define a tilted work plane and verify F8 locks to its projected U axis."""
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
WM_RBUTTONDOWN = 0x0204
VK_F8 = 0x77


def type_point(canvas: int, text: str) -> None:
    for character in text:
        user32.SendMessageW(canvas, WM_CHAR, ord(character), 0)
    user32.SendMessageW(canvas, WM_CHAR, 13, 0)
    time.sleep(0.08)


def parse_xyz(status: str) -> tuple[float, float, float]:
    match = re.search(r"X\s+(-?\d+\.\d+)\s+Y\s+(-?\d+\.\d+)\s+Z\s+(-?\d+\.\d+)", status)
    if not match:
        raise AssertionError(f"Could not parse work-plane F8 coordinates: {status!r}")
    return tuple(float(value) for value in match.groups())


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

        # Reference edge along the future tilted U direction (world X=Z).
        command(window, 603)
        command(window, 303)
        command(window, 601)
        command(window, 200)
        type_point(canvas, "0,0,0")
        type_point(canvas, "4,0,4")
        user32.SendMessageW(canvas, WM_RBUTTONDOWN, 0, 0)
        origin = projected_vertex(canvas, 0)
        u_endpoint = projected_vertex(canvas, 1)

        # New plane: O=(0,0,0), U toward (4,0,4), V toward world +Y.
        command(window, 603)
        command(window, 304)
        type_point(canvas, "0,0,0")
        type_point(canvas, "4,0,4")
        type_point(canvas, "0,4,0")
        capture(window, root / "build" / "work-plane-defined.png")
        if "3B Paralel" not in status_text(window):
            raise AssertionError("Work Plane command did not retain the 3D view")

        command(window, 604)
        command(window, 400)  # Object snap off: verify F8 rather than Endpoint acquisition.
        command(window, 401)  # Grid off, so the cursor ray—not grid rounding—is constrained.
        user32.SendMessageW(canvas, WM_KEYDOWN, VK_F8, 0)
        command(window, 601)
        command(window, 200)
        type_point(canvas, "0,0,0")

        dx, dy = u_endpoint[0] - origin[0], u_endpoint[1] - origin[1]
        length = max(1.0, math.hypot(dx, dy))
        target = (round(u_endpoint[0] - dy / length * 24.0),
                  round(u_endpoint[1] + dx / length * 24.0))
        move_physical_mouse(canvas, *target)
        status = status_text(window)
        x, y, z = parse_xyz(status)
        if "ORTHO F8: Açık (U/V)" not in status:
            raise AssertionError(f"3D status does not identify active work-plane U/V Ortho: {status!r}")
        if abs(x - z) > 0.03 or abs(x) < 1.0 or abs(y) > 0.03:
            raise AssertionError(
                f"F8 did not lock to tilted plane U=(X=Z,Y=0); resolved {(x, y, z)}, status={status!r}")

        capture(window, root / "build" / "work-plane-f8-uv.png")
        print(f"Work-plane F8 GUI smoke passed: tilted U lock resolved X={x:.3f}, Y={y:.3f}, Z={z:.3f}; "
              f"PID={process.pid}.")

        # Modify commands use local U/V plus the plane normal (local Z/N).
        user32.SendMessageW(canvas, WM_RBUTTONDOWN, *target)
        command(window, 100)
        command(window, 601)
        command(window, 200)
        type_point(canvas, "0,0,0")
        type_point(canvas, "-4,0,4")
        user32.SendMessageW(canvas, WM_RBUTTONDOWN, 0, 0)
        normal_origin = projected_vertex(canvas, 0)
        normal_endpoint = projected_vertex(canvas, 1)

        command(window, 602)
        command(window, 500)
        click(canvas, *normal_endpoint)
        user32.SendMessageW(canvas, WM_RBUTTONDOWN, *normal_endpoint)
        type_point(canvas, "0,0,0")

        ndx, ndy = normal_endpoint[0] - normal_origin[0], normal_endpoint[1] - normal_origin[1]
        normal_length = max(1.0, math.hypot(ndx, ndy))
        normal_target = (round(normal_endpoint[0] - ndy / normal_length * 18.0),
                         round(normal_endpoint[1] + ndx / normal_length * 18.0))
        move_physical_mouse(canvas, *normal_target)
        modify_status = status_text(window)
        mx, my, mz = parse_xyz(modify_status)
        if "ORTHO F8: Açık (U/V/N)" not in modify_status:
            raise AssertionError(f"Modify status does not identify local U/V/N Ortho: {modify_status!r}")
        if abs(mx + mz) > 0.03 or abs(mx) < 1.0 or abs(my) > 0.03:
            raise AssertionError(
                f"Move F8 did not lock to plane normal N=(-X,+Z); resolved {(mx, my, mz)}, "
                f"status={modify_status!r}")
        capture(window, root / "build" / "work-plane-f8-modify-normal.png")
        print(f"Work-plane Modify F8 normal lock passed: X={mx:.3f}, Y={my:.3f}, Z={mz:.3f}.")
        if args.keep_open:
            keep = True
            print("Verified maximized instance left open on Move preview along work-plane local Z/normal.")
    finally:
        if not keep and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()


if __name__ == "__main__":
    main()
