"""Regression: passive mouse motion after a large DXF import must not repaint/freeze 3D view."""
import ctypes
import os
import subprocess
import time
from ctypes import wintypes
from pathlib import Path
from smoke_dxf import user32, find_window, descendants, class_name, text
from smoke_view_cube import capture

WM_MOUSEMOVE = 0x0200
WM_MBUTTONDOWN = 0x0207
WM_MBUTTONUP = 0x0208
WM_MOUSEWHEEL = 0x020A
WM_KEYDOWN = 0x0100
MK_MBUTTON = 0x0010
WM_CLOSE = 0x0010


def lparam(x: int, y: int) -> int:
    return ((y & 0xFFFF) << 16) | (x & 0xFFFF)


class Point(ctypes.Structure):
    _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]


user32.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(Point)]
user32.ClientToScreen.restype = wintypes.BOOL


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    source = Path(r"C:\Users\Asus\Documents\MEGA\Mustafa_Hatipoglu\Balik_tesisi_seydisehir\BORU_ILK_BINA\makina_dairesi_borular.dxf")
    executable = Path(os.environ.get("MODEL_MAKER_EXE", root / "build" / "model-maker.exe"))
    launch_started = time.perf_counter()
    process = subprocess.Popen([str(executable), str(source)])
    window = 0
    try:
        window = find_window("ModelMakerWindow")
        canvas = user32.FindWindowExW(window, 0, "ModelMakerCanvas", None)
        deadline = time.time() + 90.0
        while time.time() < deadline:
            statuses = [text(child) for child in descendants(window)
                        if class_name(child).lower() == "static"]
            if any("Nesne: 23777" in value for value in statuses):
                break
            if process.poll() is not None:
                raise AssertionError(f"Application exited during import: {process.returncode}")
            time.sleep(0.02)
        else:
            raise AssertionError("Large DXF did not complete")
        load_elapsed = time.perf_counter() - launch_started
        user32.UpdateWindow(canvas)
        repaint_started = time.perf_counter()
        user32.InvalidateRect(canvas, None, False)
        user32.UpdateWindow(canvas)
        repaint_elapsed = time.perf_counter() - repaint_started
        started = time.perf_counter()
        for index in range(12):
            user32.SendMessageW(canvas, WM_MOUSEMOVE, 0, lparam(300 + index, 300))
            user32.UpdateWindow(canvas)
        elapsed = time.perf_counter() - started
        if elapsed > 1.5:
            raise AssertionError(f"Passive mouse motion repainted the entire large drawing: {elapsed:.3f}s")
        user32.SendMessageW(canvas, WM_KEYDOWN, ord('L'), 0)
        snap_started = time.perf_counter()
        for index in range(12):
            user32.SendMessageW(canvas, WM_MOUSEMOVE, 0, lparam(500 + index, 320))
        snap_elapsed = time.perf_counter() - snap_started
        user32.SendMessageW(canvas, WM_KEYDOWN, 0x1B, 0)
        if snap_elapsed > 0.5:
            raise AssertionError(f"Large DXF OSNAP cursor tracking is too slow: {snap_elapsed:.3f}s")
        user32.SendMessageW(canvas, WM_MBUTTONDOWN, MK_MBUTTON, lparam(400, 320))
        orbit_started = time.perf_counter()
        for index in range(12):
            user32.SendMessageW(canvas, WM_MOUSEMOVE, MK_MBUTTON, lparam(404 + index * 4, 322 + index * 2))
            user32.UpdateWindow(canvas)
        orbit_elapsed = time.perf_counter() - orbit_started
        interactive_image = capture(canvas, root / "build" / "dxf-large-interactive.png")
        interactive_geometry = sum(
            1 for red, green, blue in interactive_image.convert("RGB").get_flattened_data()
            if blue > 180 and green > 120 and red < 130
        )
        user32.SendMessageW(canvas, WM_MBUTTONUP, 0, lparam(452, 344))
        user32.UpdateWindow(canvas)
        settled_image = capture(canvas, root / "build" / "dxf-large-settled.png")
        def occupied_tiles(image, tile_size=32):
            rgb = image.convert("RGB")
            pixels = rgb.load()
            occupied = set()
            for y in range(rgb.height):
                for x in range(rgb.width):
                    red, green, blue = pixels[x, y]
                    if blue > 180 and green > 120 and red < 130:
                        occupied.add((x // tile_size, y // tile_size))
            return occupied
        active_tiles = occupied_tiles(interactive_image)
        settled_tiles = occupied_tiles(settled_image)
        expanded_active_tiles = {
            (tile_x + dx, tile_y + dy)
            for tile_x, tile_y in active_tiles
            for dx in (-1, 0, 1) for dy in (-1, 0, 1)
        }
        tile_coverage = len(expanded_active_tiles & settled_tiles) / max(1, len(settled_tiles))
        if orbit_elapsed > 2.5:
            raise AssertionError(f"Large DXF orbit is not interactive: {orbit_elapsed:.3f}s")
        if interactive_geometry < 1000:
            raise AssertionError(f"Interactive preview flickered blank: {interactive_geometry} geometry pixels")
        if tile_coverage < 0.90:
            raise AssertionError(f"Interactive preview hid drawing regions: {tile_coverage:.1%} tile coverage")
        wheel_started = time.perf_counter()
        wheel_point = Point(600, 300)
        if not user32.ClientToScreen(canvas, ctypes.byref(wheel_point)):
            raise AssertionError("Could not convert wheel cursor to screen coordinates")
        for _ in range(12):
            user32.SendMessageW(canvas, WM_MOUSEWHEEL, 120 << 16, lparam(wheel_point.x, wheel_point.y))
            user32.UpdateWindow(canvas)
        wheel_elapsed = time.perf_counter() - wheel_started
        wheel_active = capture(canvas, root / "build" / "dxf-large-wheel-active.png")
        time.sleep(0.45)
        user32.UpdateWindow(canvas)
        wheel_settled = capture(canvas, root / "build" / "dxf-large-wheel-settled.png")
        wheel_active_tiles = occupied_tiles(wheel_active)
        wheel_settled_tiles = occupied_tiles(wheel_settled)
        expanded_wheel_tiles = {
            (tile_x + dx, tile_y + dy)
            for tile_x, tile_y in wheel_active_tiles
            for dx in (-1, 0, 1) for dy in (-1, 0, 1)
        }
        wheel_coverage = len(expanded_wheel_tiles & wheel_settled_tiles) / max(1, len(wheel_settled_tiles))
        if wheel_elapsed > 0.75:
            raise AssertionError(f"Large DXF wheel zoom is not interactive: {wheel_elapsed:.3f}s")
        if wheel_coverage < 0.90:
            raise AssertionError(f"Wheel preview hid drawing regions: {wheel_coverage:.1%} tile coverage")
        print(f"Large DXF responsiveness passed: load={load_elapsed:.3f}s, repaint={repaint_elapsed:.3f}s, passive={elapsed:.3f}s, snap={snap_elapsed:.3f}s, orbit={orbit_elapsed:.3f}s, wheel={wheel_elapsed:.3f}s, interactive_pixels={interactive_geometry}, orbit_coverage={tile_coverage:.1%}, wheel_coverage={wheel_coverage:.1%}.")
    finally:
        if window and user32.IsWindow(window):
            user32.PostMessageW(window, WM_CLOSE, 0, 0)
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.terminate(); process.wait(timeout=5)


if __name__ == "__main__":
    main()
