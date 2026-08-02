"""Native Win32 smoke test for Trim and Extend modifier workflows."""
import argparse
import ctypes
import re
import subprocess
import time
from ctypes import wintypes
from pathlib import Path

from smoke_snap_style_controls import (
    CB_SETCURSEL, WM_CHAR, capture, click, command, find_process_window, status_text, user32,
)
from smoke_mirror_snap import send_mouse

CBN_SELCHANGE = 1
WM_KEYDOWN = 0x0100
WM_MOUSEMOVE = 0x0200
VK_F9 = 0x78


def count_models(window: int) -> int:
    match = re.search(r"Nesne:\s*(\d+)", status_text(window))
    if not match:
        raise AssertionError(f"Model count unavailable: {status_text(window)!r}")
    return int(match.group(1))


def red_count(image, box: tuple[int, int, int, int]) -> int:
    return sum(1 for red, green, blue in image.crop(box).convert("RGB").get_flattened_data()
               if red > 210 and green < 120 and blue < 120)


def require_status(window: int, phrase: str) -> None:
    status = status_text(window)
    if phrase not in status:
        raise AssertionError(f"Expected {phrase!r} in status, got {status!r}")


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
        time.sleep(0.2)
        canvas = user32.FindWindowExW(window, 0, "ModelMakerCanvas", None)
        if not canvas:
            raise RuntimeError("Canvas window was not found")
        command(window, 602)
        buttons = [user32.GetDlgItem(window, control_id) for control_id in range(500, 509)]
        if not all(buttons) or not all(user32.IsWindowVisible(button) for button in buttons):
            raise AssertionError("All nine Modify controls are not visible")

        color = user32.GetDlgItem(window, 801)
        user32.SendMessageW(color, CB_SETCURSEL, 2, 0)
        command(window, 801, CBN_SELCHANGE)
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
        user32.SendMessageW(canvas, WM_KEYDOWN, VK_F9, 0)  # exact unquantized test geometry

        # Trim scene: one vertical cutting edge crossing two horizontal targets.
        command(window, 601); command(window, 200)
        trim_boundary_x = cx
        trim_target_ys = [cy - 180, cy - 80]
        click(canvas, trim_boundary_x, cy - 260); click(canvas, trim_boundary_x, cy - 20)
        for target_y in trim_target_ys:
            click(canvas, cx - 180, target_y); click(canvas, cx + 180, target_y)
        if count_models(window) != 3:
            raise AssertionError("Multi-Trim source geometry was not created")
        command(window, 602); command(window, 507)
        # Crossing-select the boundary in the first phase (right-to-left green window).
        click(canvas, trim_boundary_x + 35, cy - 215)
        send_mouse(canvas, WM_MOUSEMOVE, trim_boundary_x - 35, cy - 245)
        capture(canvas, root / "build" / "trim-boundary-crossing.png")
        click(canvas, trim_boundary_x - 35, cy - 245)
        user32.SendMessageW(canvas, WM_CHAR, 13, 0)
        require_status(window, "Kesilecek çizgi")
        # A second right-to-left crossing window trims both target portions in one gesture.
        click(canvas, cx - 60, trim_target_ys[1] + 20)
        send_mouse(canvas, WM_MOUSEMOVE, cx - 130, trim_target_ys[0] - 20)
        capture(canvas, root / "build" / "trim-target-crossing.png")
        click(canvas, cx - 130, trim_target_ys[0] - 20)
        require_status(window, "Kesilecek çizgi")
        user32.SendMessageW(canvas, WM_CHAR, 13, 0)
        if count_models(window) != 3:
            raise AssertionError("Multi-Trim unexpectedly changed model count for end cuts")
        trimmed = capture(canvas, root / "build" / "trim-result.png")
        trim_counts = []
        for target_y in trim_target_ys:
            removed = red_count(trimmed, (cx - 170, target_y - 4, cx - 10, target_y + 5))
            retained = red_count(trimmed, (cx + 10, target_y - 4, cx + 170, target_y + 5))
            trim_counts.append((removed, retained))
            if removed > 8 or retained < 120:
                raise AssertionError(
                    f"Multi-Trim result mismatch at y={target_y}: removed={removed}, retained={retained}")

        # Extend scene: one boundary and two independently extended horizontal targets.
        command(window, 601); command(window, 200)
        extend_target_ys = [cy + 90, cy + 190]
        extend_boundary_x = cx + 300
        click(canvas, extend_boundary_x, cy + 20); click(canvas, extend_boundary_x, cy + 260)
        for target_y in extend_target_ys:
            click(canvas, cx - 180, target_y); click(canvas, cx + 60, target_y)
        if count_models(window) != 6:
            raise AssertionError("Multi-Extend source geometry was not created")
        command(window, 602); command(window, 508)
        # Crossing-select the shared Extend boundary.
        click(canvas, extend_boundary_x + 35, cy + 55)
        send_mouse(canvas, WM_MOUSEMOVE, extend_boundary_x - 35, cy + 25)
        capture(canvas, root / "build" / "extend-boundary-crossing.png")
        click(canvas, extend_boundary_x - 35, cy + 25)
        user32.SendMessageW(canvas, WM_CHAR, 13, 0)
        require_status(window, "Uzatılacak çizginin")
        # Crossing-select both target endpoints and extend them in one gesture.
        click(canvas, cx + 100, extend_target_ys[1] + 20)
        send_mouse(canvas, WM_MOUSEMOVE, cx + 20, extend_target_ys[0] - 20)
        capture(canvas, root / "build" / "extend-target-crossing.png")
        click(canvas, cx + 20, extend_target_ys[0] - 20)
        require_status(window, "Uzatılacak çizginin")
        user32.SendMessageW(canvas, WM_CHAR, 13, 0)
        if count_models(window) != 6:
            raise AssertionError("Multi-Extend must replace, not duplicate, both target lines")
        extended = capture(canvas, root / "build" / "extend-result.png")
        extension_counts = []
        for target_y in extend_target_ys:
            extension = red_count(extended, (cx + 70, target_y - 4,
                                              extend_boundary_x - 10, target_y + 5))
            extension_counts.append(extension)
            if extension < 180:
                raise AssertionError(
                    f"Multi-Extend did not reach the boundary at y={target_y} ({extension} pixels)")
        command(window, 602)
        capture(window, root / "build" / "trim-extend-modifiers.png")
        print(f"Trim/Extend multi-target GUI smoke passed: trims={trim_counts}; "
              f"extensions={extension_counts}; models=6; PID={process.pid}.")
        if args.keep_open:
            keep = True
            print("Verified maximized instance left running on the Modify tab.")
    finally:
        if not keep and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()


if __name__ == "__main__":
    main()
