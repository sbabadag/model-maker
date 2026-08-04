"""Verify every non-view-toggle command preserves an active 3D view and point phases snap."""
import argparse
import ctypes
import subprocess
import time
from ctypes import wintypes
from pathlib import Path

from smoke_arrays_3d import (SNAP_ENDPOINT, WM_SNAP_TYPE_PROBE, move_physical_mouse,
                             projected_vertex, window_select_one)
from smoke_mirror_snap import click, find_process_window, send_mouse
from smoke_snap_style_controls import capture, command, status_text, user32

WM_CHAR = 0x0102
WM_RBUTTONDOWN = 0x0204


def require_3d(window: int, context: str) -> None:
    status = status_text(window)
    if "3B Paralel" not in status:
        raise AssertionError(f"{context} switched the active 3D view: {status!r}")


def require_endpoint(canvas: int, point: tuple[int, int], context: str) -> None:
    move_physical_mouse(canvas, *point)
    snap_type = int(user32.SendMessageW(canvas, WM_SNAP_TYPE_PROBE, 0, 0))
    if snap_type != SNAP_ENDPOINT:
        raise AssertionError(f"{context} did not resolve Endpoint snap; type={snap_type}")


def enter(canvas: int) -> None:
    user32.SendMessageW(canvas, WM_CHAR, 13, 0)


def type_count(canvas: int, count: int) -> None:
    for character in str(count):
        user32.SendMessageW(canvas, WM_CHAR, ord(character), 0)
    enter(canvas)


def setup_cube(window: int) -> None:
    command(window, 100)
    command(window, 300)
    require_3d(window, "Cube setup")


def select_cube(canvas: int) -> None:
    vertex = projected_vertex(canvas, 0)
    click(canvas, *vertex)
    send_mouse(canvas, WM_RBUTTONDOWN, *vertex)


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

        setup_cube(window)
        command(window, 100)
        require_3d(window, "New")
        command(window, 300)

        for command_id, name in ((200, "Line"), (201, "Polyline"), (202, "Rectangle"), (203, "Circle")):
            command(window, command_id)
            require_3d(window, name)
        for command_id, name in ((500, "Move"), (501, "Copy"), (502, "Offset"), (503, "Mirror"),
                                 (504, "Delete"), (505, "Linear Array"), (506, "Polar Array"),
                                 (507, "Trim"), (508, "Extend")):
            command(window, 602)
            command(window, command_id)
            require_3d(window, name)
        for command_id, name in ((302, "Reset View"), (304, "Work Plane"),
                                 (305, "Zoom Extents"), (306, "Zoom Window")):
            command(window, 603)
            command(window, command_id)
            require_3d(window, name)

        # Drawing tools: both first and second point must expose projected Endpoint snap.
        for command_id, name in ((200, "Line"), (201, "Polyline"), (202, "Rectangle"), (203, "Circle")):
            setup_cube(window)
            first, second = projected_vertex(canvas, 0), projected_vertex(canvas, 6)
            command(window, 601)
            command(window, command_id)
            require_endpoint(canvas, first, f"{name} first point")
            click(canvas, *first)
            require_endpoint(canvas, second, f"{name} second point")
            require_3d(window, f"{name} second point")

        # Two-point modifiers: destination phase uses the same 3D Endpoint result as base phase.
        for command_id, name in ((500, "Move"), (501, "Copy"), (503, "Mirror"), (505, "Linear Array")):
            setup_cube(window)
            first, second = projected_vertex(canvas, 0), projected_vertex(canvas, 6)
            command(window, 602)
            command(window, command_id)
            select_cube(canvas)
            if command_id == 505:
                type_count(canvas, 4)
            require_endpoint(canvas, first, f"{name} first point")
            click(canvas, *first)
            require_endpoint(canvas, second, f"{name} second point")
            require_3d(window, f"{name} second point")
            if name == "Move":
                capture(window, root / "build" / "all-commands-3d-second-snap.png")

        # Single-point Polar Array center also keeps projected object snap in 3D.
        setup_cube(window)
        center = projected_vertex(canvas, 0)
        command(window, 602)
        command(window, 506)
        select_cube(canvas)
        type_count(canvas, 4)
        require_endpoint(canvas, center, "Polar Array center")
        require_3d(window, "Polar Array center")

        capture(window, root / "build" / "all-commands-3d-result.png")
        print("All-command 3D smoke passed: no implicit 3D→2D switch; drawing and modifier "
              f"first/second Endpoint snaps verified; PID={process.pid}.")
        if args.keep_open:
            keep = True
            print("Verified maximized instance left open at 3D Polar Array Endpoint center selection.")
    finally:
        if not keep and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()


if __name__ == "__main__":
    main()
