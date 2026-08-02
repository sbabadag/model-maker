"""Native verification for F10 acquired Temp Points and object-snap tracking."""
import argparse
import ctypes
import subprocess
import time
from ctypes import wintypes
from pathlib import Path

from smoke_arrays_3d import projected_vertex
from smoke_mirror_snap import find_process_window, send_mouse
from smoke_polar_tracking import input_point
from smoke_snap_style_controls import capture, command, status_text, user32

WM_CLOSE = 0x0010
SW_MAXIMIZE = 3
WM_MOUSEMOVE = 0x0200


def move_tracking_mouse(canvas: int, x: int, y: int) -> None:
    # Do not move the physical cursor: DPI virtualization can emit a later, scaled coordinate.
    send_mouse(canvas, WM_MOUSEMOVE, x, y)


def wait_for_status(window: int, fragment: str, timeout: float = 1.5) -> str:
    deadline = time.time() + timeout
    latest = ""
    while time.time() < deadline:
        latest = status_text(window)
        if fragment in latest:
            return latest
        time.sleep(0.04)
    raise AssertionError(f"Status never contained {fragment!r}: {latest!r}")


def world_to_screen(rect: wintypes.RECT, x: float, y: float) -> tuple[int, int]:
    return round(rect.right / 2 + x * 60.0), round(rect.bottom / 2 - y * 60.0)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--keep-open", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    process = subprocess.Popen([str(root / "build" / "model-maker.exe")])
    keep = False
    try:
        window = find_process_window(process.pid)
        user32.ShowWindow(window, SW_MAXIMIZE)
        user32.SetForegroundWindow(window)
        user32.UpdateWindow(window)
        canvas = user32.FindWindowExW(window, 0, "ModelMakerCanvas", None)
        if not canvas:
            raise RuntimeError("Canvas window was not found")
        rect = wintypes.RECT()
        for _ in range(40):
            user32.GetClientRect(canvas, ctypes.byref(rect))
            if rect.right >= 1000 and rect.bottom >= 600:
                break
            time.sleep(0.05)

        # Create two short, disconnected lines. Their outer endpoints are the Temp Point sources.
        command(window, 601)
        command(window, 200)
        input_point(canvas, "-4,-2")
        input_point(canvas, "-3,-2")
        input_point(canvas, "4,2")
        input_point(canvas, "3,2")

        command(window, 604)
        command(window, 401)  # Grid off: isolate OSNAP and acquired-point tracking.
        command(window, 404)  # F10 Polar on.
        command(window, 601)
        command(window, 200)

        first = projected_vertex(canvas, 0)
        first_line_inner = projected_vertex(canvas, 1)
        unit_x = first_line_inner[0] - first[0]
        if abs(unit_x) < 20:
            raise AssertionError(f"Unexpected 2D projection scale: {first!r}, {first_line_inner!r}")
        scale = abs(unit_x)
        second = (first[0] + 8 * unit_x, first[1] - 4 * scale)
        midpoint = (first[0] + 4 * unit_x, first[1] - 2 * scale)
        perpendicular = (first[0], first[1] - 4 * scale)

        move_tracking_mouse(canvas, *first)
        wait_for_status(window, "TEMP: 1")
        move_tracking_mouse(canvas, *second)
        wait_for_status(window, "TEMP: 2")

        move_tracking_mouse(canvas, *midpoint)
        midpoint_status = wait_for_status(window, "TEMP: 2 (")
        if "POLAR F10: 90° İzleme" not in midpoint_status:
            raise AssertionError(f"Temp midpoint did not lock: {midpoint_status!r}")
        midpoint_image = capture(window, root / "build" / "temp-tracking-midpoint.png").convert("RGB")

        move_tracking_mouse(canvas, *perpendicular)
        # The derived perpendicular crossing is itself promoted to TP3 after the same dwell.
        corner_status = wait_for_status(window, "TEMP: 3")
        if "X -4.000  Y 2.000" not in corner_status:
            raise AssertionError(f"Perpendicular Temp crossing was acquired at the wrong point: {corner_status!r}")
        corner_image = capture(window, root / "build" / "temp-tracking-perpendicular.png").convert("RGB")

        # Acquired TP markers are magenta; guides and derived corners are cyan.
        magenta = sum(1 for r, g, b in midpoint_image.get_flattened_data()
                       if r > 220 and b > 170 and g < 170)
        cyan = sum(1 for r, g, b in corner_image.get_flattened_data()
                    if r < 130 and g > 180 and b > 190)
        if magenta < 25 or cyan < 40:
            raise AssertionError(f"Temp tracking overlays missing: magenta={magenta}, cyan={cyan}")

        # Verify that the same acquired-point axis resolver follows projected work-plane U in 3D.
        command(window, 100)
        command(window, 303)
        command(window, 200)
        input_point(canvas, "0,0,0")
        input_point(canvas, "10,0,0")
        origin = projected_vertex(canvas, 0)
        x_axis = projected_vertex(canvas, 1)
        move_tracking_mouse(canvas, *origin)
        wait_for_status(window, "TEMP: 1")
        command(window, 400)  # Preserve TP1 while disabling direct OSNAP for the tracking probe.
        dx, dy = x_axis[0] - origin[0], x_axis[1] - origin[1]
        length = max(1.0, (dx * dx + dy * dy) ** 0.5)
        target = (origin[0] - round(dx * 0.45) + round(-dy * 5.0 / length),
                  origin[1] - round(dy * 0.45) + round(dx * 5.0 / length))
        move_tracking_mouse(canvas, *target)
        tracking_3d_status = wait_for_status(window, "POLAR F10: 90° İzleme")
        if "TEMP: 1" not in tracking_3d_status or "3B" not in tracking_3d_status:
            raise AssertionError(f"3D Temp Point did not track on work-plane U: {tracking_3d_status!r}")
        capture(window, root / "build" / "temp-tracking-3d.png")

        print("Temporary tracking GUI smoke passed: 450ms endpoint acquisition, two TP markers, "
              f"midpoint lock, perpendicular crossing promoted to TP3, 3D work-plane U lock, guides; "
              f"magenta={magenta}, cyan={cyan}, "
              f"PID={process.pid}.")
        if args.keep_open:
            keep = True
            print("Verified maximized instance left open with TP1 tracking along work-plane U in 3D.")
    finally:
        if not keep and process.poll() is None:
            user32.PostMessageW(window, WM_CLOSE, 0, 0)
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=3)


if __name__ == "__main__":
    main()
