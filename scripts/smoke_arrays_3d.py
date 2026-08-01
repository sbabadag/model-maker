"""Native verification for Linear and Polar Array while the application remains in 3D view."""
import argparse
import ctypes
import re
import subprocess
import time
from ctypes import wintypes
from pathlib import Path

from smoke_mirror_snap import WM_MOUSEMOVE, click, find_process_window, send_mouse
from smoke_snap_style_controls import capture, command, status_text, user32

WM_CHAR = 0x0102
WM_RBUTTONDOWN = 0x0204
WM_MOUSEWHEEL = 0x020A
WM_SNAP_TYPE_PROBE = 0x8003
WM_FIRST_VERTEX_PROBE = 0x8004
SNAP_ENDPOINT = 2


def model_count(window: int) -> int:
    match = re.search(r"Nesne:\s*(\d+)", status_text(window))
    if not match:
        raise AssertionError(f"Could not parse model count from {status_text(window)!r}")
    return int(match.group(1))


def require_3d(window: int, phase: str) -> None:
    status = status_text(window)
    if "3B Paralel" not in status:
        raise AssertionError(f"{phase} left 3D view: {status!r}")


def enter(window: int) -> None:
    canvas = user32.FindWindowExW(window, 0, "ModelMakerCanvas", None)
    user32.SendMessageW(canvas, WM_CHAR, 13, 0)


def type_count(canvas: int, count: int) -> None:
    for character in str(count):
        user32.SendMessageW(canvas, WM_CHAR, ord(character), 0)
    user32.SendMessageW(canvas, WM_CHAR, 13, 0)


def window_select_one(canvas: int, cx: int, cy: int) -> None:
    click(canvas, cx - 230, cy - 230)
    click(canvas, cx + 230, cy + 230)


def projected_vertex(canvas: int, index: int) -> tuple[int, int]:
    projected = int(user32.SendMessageW(canvas, WM_FIRST_VERTEX_PROBE, index, 0))
    return projected & 0xFFFF, (projected >> 16) & 0xFFFF


def move_physical_mouse(canvas: int, x: int, y: int) -> None:
    point = wintypes.POINT(x, y)
    user32.ClientToScreen(canvas, ctypes.byref(point))
    user32.SetCursorPos(point.x, point.y)
    send_mouse(canvas, WM_MOUSEMOVE, x, y)


def wheel_at(canvas: int, x: int, y: int) -> None:
    point = wintypes.POINT(x, y)
    user32.ClientToScreen(canvas, ctypes.byref(point))
    user32.SendMessageW(canvas, WM_MOUSEWHEEL, 120 << 16,
                        (point.y << 16) | (point.x & 0xFFFF))


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
        if not canvas:
            raise RuntimeError("Canvas window was not found")
        rect = wintypes.RECT()
        screen_width = user32.GetSystemMetrics(0)
        for _ in range(40):
            user32.GetClientRect(canvas, ctypes.byref(rect))
            if 1000 <= rect.right <= screen_width and rect.bottom >= 600:
                break
            time.sleep(0.05)
        else:
            raise AssertionError(f"Maximized canvas did not stabilize: {rect.right}x{rect.bottom}")
        cx, cy = rect.right // 2, rect.bottom // 2

        # Linear Array on an actual 3D cube.
        command(window, 300)  # Cube also enters 3D view.
        if model_count(window) != 1:
            raise AssertionError("3D Linear Array source cube was not created")
        require_3d(window, "Cube creation")
        command(window, 602)
        command(window, 505)
        require_3d(window, "Linear Array activation")
        window_select_one(canvas, cx, cy)
        send_mouse(canvas, WM_RBUTTONDOWN, cx, cy)  # selection confirmation as Enter
        if "Öğe sayısını" not in status_text(window):
            raise AssertionError(f"Linear Array did not accept projected 3D selection: {status_text(window)!r}")
        type_count(canvas, 4)
        cube_endpoint = projected_vertex(canvas, 0)
        move_physical_mouse(canvas, *cube_endpoint)
        time.sleep(0.1)
        snap_type = int(user32.SendMessageW(canvas, WM_SNAP_TYPE_PROBE, 0, 0))
        if snap_type != SNAP_ENDPOINT:
            capture(canvas, root / "build" / "array-3d-snap-failure.png")
            raise AssertionError(
                f"3D Linear Array did not acquire the cube Endpoint snap ({snap_type}) at "
                f"{cube_endpoint}; status={status_text(window)!r}")
        capture(canvas, root / "build" / "array-3d-endpoint-snap.png")
        click(canvas, *cube_endpoint)
        second_endpoint = projected_vertex(canvas, 6)
        move_physical_mouse(canvas, *second_endpoint)
        time.sleep(0.1)
        second_snap_type = int(user32.SendMessageW(canvas, WM_SNAP_TYPE_PROBE, 0, 0))
        if second_snap_type != SNAP_ENDPOINT:
            capture(canvas, root / "build" / "array-3d-second-snap-failure.png")
            raise AssertionError(
                f"3D Linear Array second point did not acquire Endpoint ({second_snap_type}) at "
                f"{second_endpoint}; status={status_text(window)!r}")
        wheel_at(canvas, *second_endpoint)
        time.sleep(0.45)
        settled_snap_type = int(user32.SendMessageW(canvas, WM_SNAP_TYPE_PROBE, 0, 0))
        if settled_snap_type != SNAP_ENDPOINT:
            raise AssertionError(
                f"3D Linear Array second-point snap was not restored after wheel navigation "
                f"settled ({settled_snap_type})")
        capture(canvas, root / "build" / "linear-array-3d-second-endpoint.png")
        click(canvas, *second_endpoint)
        if model_count(window) != 4:
            raise AssertionError("3D Linear Array did not create four total cubes")
        require_3d(window, "Linear Array completion")
        capture(window, root / "build" / "linear-array-3d.png")

        # Fresh cube for an independently measured 3D Polar Array.
        command(window, 100)
        command(window, 300)
        if model_count(window) != 1:
            raise AssertionError("3D Polar Array source cube was not created")
        require_3d(window, "Polar source creation")
        command(window, 602)
        command(window, 506)
        require_3d(window, "Polar Array activation")
        window_select_one(canvas, cx, cy)
        send_mouse(canvas, WM_RBUTTONDOWN, cx, cy)
        if "Öğe sayısını" not in status_text(window):
            raise AssertionError(f"Polar Array did not accept projected 3D selection: {status_text(window)!r}")
        type_count(canvas, 4)
        move_physical_mouse(canvas, *cube_endpoint)
        time.sleep(0.1)
        snap_type = int(user32.SendMessageW(canvas, WM_SNAP_TYPE_PROBE, 0, 0))
        if snap_type != SNAP_ENDPOINT:
            raise AssertionError(f"3D Polar Array did not acquire the cube Endpoint snap ({snap_type})")
        capture(canvas, root / "build" / "polar-array-3d-endpoint-snap.png")
        send_mouse(canvas, WM_MOUSEMOVE, cx + 210, cy + 80)
        capture(canvas, root / "build" / "polar-array-3d-preview.png")
        click(canvas, cx + 210, cy + 80)
        if model_count(window) != 4:
            raise AssertionError("3D Polar Array did not create four total cubes")
        require_3d(window, "Polar Array completion")
        command(window, 602)
        capture(window, root / "build" / "arrays-3d.png")

        print("3D Array GUI smoke passed: Linear 1→4, Polar 1→4, 3D view preserved "
              f"through activation/preview/commit, PID={process.pid}.")
        if args.keep_open:
            # Leave a directly inspectable 3D Endpoint snap active in Linear Array base-point phase.
            command(window, 100)
            command(window, 300)
            command(window, 602)
            command(window, 505)
            window_select_one(canvas, cx, cy)
            send_mouse(canvas, WM_RBUTTONDOWN, cx, cy)
            type_count(canvas, 4)
            move_physical_mouse(canvas, *cube_endpoint)
            time.sleep(0.1)
            if int(user32.SendMessageW(canvas, WM_SNAP_TYPE_PROBE, 0, 0)) != SNAP_ENDPOINT:
                raise AssertionError("Final live 3D first-point Endpoint snap could not be established")
            click(canvas, *cube_endpoint)
            live_second_endpoint = projected_vertex(canvas, 6)
            move_physical_mouse(canvas, *live_second_endpoint)
            time.sleep(0.1)
            if int(user32.SendMessageW(canvas, WM_SNAP_TYPE_PROBE, 0, 0)) != SNAP_ENDPOINT:
                raise AssertionError("Final live 3D second-point Endpoint snap could not be established")
            capture(window, root / "build" / "snap-3d-live.png")
            keep = True
            print("Verified maximized instance left at 3D Linear Array's SECOND point with a live Endpoint snap.")
    finally:
        if not keep and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()


if __name__ == "__main__":
    main()
