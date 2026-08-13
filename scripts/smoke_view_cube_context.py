"""Verify the ViewCube has an independent child HWND, paint lifecycle, and input routing."""
import argparse
import subprocess
import time
from ctypes import byref, wintypes
from pathlib import Path

from PIL import ImageChops

from smoke_arrays_3d import projected_vertex
from smoke_mirror_snap import find_process_window
from smoke_snap_style_controls import capture, command, user32

WM_CLOSE = 0x0010
WM_MOUSEMOVE = 0x0200
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
MK_LBUTTON = 0x0001
SW_MAXIMIZE = 3
CMD_CUBE = 300
CMD_VIEW_FRONT = 320
CMD_LAYER_MANAGER = 205


def mouse_lparam(x: int, y: int) -> int:
    return (y & 0xFFFF) << 16 | (x & 0xFFFF)


def window_rect(window: int) -> tuple[int, int, int, int]:
    rect = wintypes.RECT()
    if not user32.GetWindowRect(window, byref(rect)):
        raise RuntimeError("GetWindowRect failed")
    return rect.left, rect.top, rect.right, rect.bottom


def assert_top_right_anchor(canvas: int, cube_rect: tuple[int, int, int, int]) -> None:
    client = wintypes.RECT()
    origin = wintypes.POINT(0, 0)
    if not user32.GetClientRect(canvas, byref(client)) or not user32.ClientToScreen(canvas, byref(origin)):
        raise RuntimeError("Could not resolve the canvas client bounds")
    if cube_rect[2] != origin.x + client.right - 12 or cube_rect[1] != origin.y + 12:
        raise AssertionError(
            f"ViewCube is not anchored to the canvas client top-right: {cube_rect=}, "
            f"canvas_origin=({origin.x}, {origin.y}), canvas_size=({client.right}, {client.bottom})")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--keep-open", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    process = subprocess.Popen([str(root / "build" / "model-maker.exe")])
    window = 0
    keep = False
    try:
        window = find_process_window(process.pid)
        user32.ShowWindow(window, SW_MAXIMIZE)
        user32.SetForegroundWindow(window)
        time.sleep(0.25)
        canvas = user32.FindWindowExW(window, 0, "ModelMakerCanvas", None)
        if not canvas:
            raise AssertionError("Canvas child HWND was not found")
        cube = user32.FindWindowExW(canvas, 0, "ModelMakerViewCube", None)
        if not cube:
            raise AssertionError("Independent ModelMakerViewCube child HWND was not found")
        if not user32.IsWindowVisible(cube):
            raise AssertionError("ViewCube child must be visible in the initial 2D workspace")

        initial_rect = window_rect(cube)
        assert_top_right_anchor(canvas, initial_rect)
        before = capture(cube, root / "build" / "view-cube-context-before.png")
        for _ in range(20):
            user32.InvalidateRect(canvas, None, False)
            user32.UpdateWindow(canvas)
        after = capture(cube, root / "build" / "view-cube-context-after-redraw.png")
        if not user32.IsWindow(cube) or window_rect(cube) != initial_rect:
            raise AssertionError("Canvas redraw moved or recreated the ViewCube child")
        if ImageChops.difference(before, after).getbbox() is not None:
            raise AssertionError("Canvas-only redraw changed the independent ViewCube surface")

        command(window, CMD_LAYER_MANAGER)
        time.sleep(0.1)
        expanded_rect = window_rect(cube)
        assert_top_right_anchor(canvas, expanded_rect)
        if expanded_rect == initial_rect or user32.FindWindowExW(canvas, 0, "ModelMakerViewCube", None) != cube:
            raise AssertionError("ViewCube was recreated or failed to follow the resized canvas")
        command(window, CMD_LAYER_MANAGER)
        time.sleep(0.1)
        initial_rect = window_rect(cube)
        assert_top_right_anchor(canvas, initial_rect)

        command(window, CMD_CUBE)
        command(window, CMD_VIEW_FRONT)
        front_vertex = projected_vertex(canvas, 4)
        cube_width = initial_rect[2] - initial_rect[0]
        home_x = max(96, cube_width - 96) + 42
        home_y = 215
        user32.SetCursorPos(initial_rect[0] + home_x, initial_rect[1] + home_y)
        user32.SendMessageW(cube, WM_MOUSEMOVE, 0, mouse_lparam(home_x, home_y))
        user32.UpdateWindow(cube)
        time.sleep(0.05)
        hover = capture(cube, root / "build" / "view-cube-context-hover.png")
        blue = sum(1 for r, g, b in hover.convert("RGB").get_flattened_data()
                   if b > 160 and g > 110 and r < 90)
        if blue < 80:
            raise AssertionError(f"ViewCube HOME hover feedback was not visible ({blue} blue pixels)")
        user32.SendMessageW(cube, WM_LBUTTONDOWN, MK_LBUTTON, mouse_lparam(home_x, home_y))
        user32.SendMessageW(cube, WM_LBUTTONUP, 0, mouse_lparam(home_x, home_y))
        time.sleep(0.1)
        iso_vertex = projected_vertex(canvas, 4)
        if iso_vertex == front_vertex:
            raise AssertionError("ViewCube child click did not route the isometric camera command")
        drag_start_x, drag_start_y = 96, 119
        drag_end_x, drag_end_y = 122, 97
        user32.SendMessageW(cube, WM_LBUTTONDOWN, MK_LBUTTON,
                            mouse_lparam(drag_start_x, drag_start_y))
        user32.SendMessageW(cube, WM_MOUSEMOVE, MK_LBUTTON,
                            mouse_lparam(drag_end_x, drag_end_y))
        user32.SendMessageW(cube, WM_LBUTTONUP, 0,
                            mouse_lparam(drag_end_x, drag_end_y))
        time.sleep(0.1)
        orbit_vertex = projected_vertex(canvas, 4)
        if orbit_vertex == iso_vertex:
            raise AssertionError("Dragging the independent ViewCube did not orbit the camera")
        capture(window, root / "build" / "view-cube-independent-window.png")
        print(f"Independent ViewCube smoke passed: hwnd={cube}, rect={initial_rect}, "
              f"front={front_vertex}, iso={iso_vertex}, orbit={orbit_vertex}, PID={process.pid}.")
        if args.keep_open:
            keep = True
            print("Verified maximized instance left open with independent ViewCube context.")
    finally:
        if window and not keep:
            user32.PostMessageW(window, WM_CLOSE, 0, 0)
        if not keep:
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=3)


if __name__ == "__main__":
    main()
