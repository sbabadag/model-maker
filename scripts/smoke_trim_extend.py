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

        # Trim scene: a vertical cutting edge crossing a horizontal target.
        command(window, 601); command(window, 200)
        trim_boundary_x = cx
        trim_target_y = cy - 150
        click(canvas, trim_boundary_x, cy - 240); click(canvas, trim_boundary_x, cy - 60)
        click(canvas, cx - 180, trim_target_y); click(canvas, cx + 180, trim_target_y)
        if count_models(window) != 2:
            raise AssertionError("Trim source geometry was not created")
        command(window, 602); command(window, 507)
        click(canvas, trim_boundary_x, cy - 210)
        user32.SendMessageW(canvas, WM_CHAR, 13, 0)
        require_status(window, "Kesilecek çizgi")
        send_mouse(canvas, WM_MOUSEMOVE, cx - 90, trim_target_y)
        time.sleep(0.1)
        capture(canvas, root / "build" / "trim-preview.png")
        click(canvas, cx - 90, trim_target_y)
        if count_models(window) != 2:
            raise AssertionError("Trim unexpectedly changed the model count for a single cut")
        trimmed = capture(canvas, root / "build" / "trim-result.png")
        removed = red_count(trimmed, (cx - 170, trim_target_y - 4, cx - 10, trim_target_y + 5))
        retained = red_count(trimmed, (cx + 10, trim_target_y - 4, cx + 170, trim_target_y + 5))
        if removed > 8 or retained < 120:
            raise AssertionError(f"Trim result mismatch: removed-side={removed}, retained-side={retained}")
        # Extend scene: horizontal target stops short of a separate vertical boundary.
        command(window, 601); command(window, 200)
        extend_target_y = cy + 120
        extend_boundary_x = cx + 300
        click(canvas, extend_boundary_x, cy + 30); click(canvas, extend_boundary_x, cy + 210)
        click(canvas, cx - 180, extend_target_y); click(canvas, cx + 60, extend_target_y)
        if count_models(window) != 4:
            raise AssertionError("Extend source geometry was not created")
        command(window, 602); command(window, 508)
        click(canvas, extend_boundary_x, cy + 60)
        user32.SendMessageW(canvas, WM_CHAR, 13, 0)
        require_status(window, "Uzatılacak çizginin")
        send_mouse(canvas, WM_MOUSEMOVE, cx + 40, extend_target_y)
        time.sleep(0.1)
        capture(canvas, root / "build" / "extend-preview.png")
        click(canvas, cx + 40, extend_target_y)
        if count_models(window) != 4:
            raise AssertionError("Extend must replace, not duplicate, the target line")
        extended = capture(canvas, root / "build" / "extend-result.png")
        extension = red_count(extended, (cx + 70, extend_target_y - 4,
                                         extend_boundary_x - 10, extend_target_y + 5))
        if extension < 180:
            raise AssertionError(f"Extend did not reach the selected boundary ({extension} pixels)")
        command(window, 602)
        capture(window, root / "build" / "trim-extend-modifiers.png")
        print(f"Trim/Extend GUI smoke passed: trim retained={retained}, removed={removed}; "
              f"extend pixels={extension}; models=4; PID={process.pid}.")
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
