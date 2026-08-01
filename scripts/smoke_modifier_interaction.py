"""Verify modifier right-click Enter, one-shot completion, Enter repeat, and phase cursors."""
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
WM_KEYDOWN = 0x0100
WM_RBUTTONDOWN = 0x0204
WM_SETCURSOR = 0x0020
WM_CURSOR_PROBE = 0x8001
BM_GETCHECK = 0x00F0
BST_CHECKED = 1
HTCLIENT = 1
VK_F9 = 0x78


def cursor_handle(canvas: int) -> int:
    user32.SendMessageW(canvas, WM_SETCURSOR, canvas, HTCLIENT)
    return int(user32.SendMessageW(canvas, WM_CURSOR_PROBE, 0, 0))


def model_count(window: int) -> int:
    match = re.search(r"Nesne:\s*(\d+)", status_text(window))
    if not match:
        raise AssertionError(f"Could not parse model count from {status_text(window)!r}")
    return int(match.group(1))


def assert_copy_selecting(window: int, copy_button: int) -> None:
    if user32.SendMessageW(copy_button, BM_GETCHECK, 0, 0) != BST_CHECKED:
        raise AssertionError("Copy command is not active")
    status = status_text(window)
    if "COPY" not in status or "Nesneleri seçin" not in status:
        raise AssertionError(f"Copy did not restart in entity-selection phase: {status!r}")


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
        copy_button = user32.GetDlgItem(window, 501)
        if not canvas or not copy_button:
            raise RuntimeError("Required native controls were not found")

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
        user32.SendMessageW(canvas, WM_KEYDOWN, VK_F9, 0)

        # Draw one isolated source line.
        command(window, 601)
        command(window, 200)
        click(canvas, cx - 150, cy)
        click(canvas, cx + 150, cy)
        if model_count(window) != 1:
            raise AssertionError("Copy source line was not created")
        normal_crosshair = cursor_handle(canvas)

        # Start Copy: selection uses pickbox; right-click confirms selection exactly like Enter.
        command(window, 602)
        command(window, 501)
        selection_pickbox = cursor_handle(canvas)
        if not normal_crosshair or not selection_pickbox or selection_pickbox == normal_crosshair:
            raise AssertionError("Modifier entity selection did not use the square pickbox")
        click(canvas, cx, cy)
        send_mouse(canvas, WM_RBUTTONDOWN, cx, cy)
        status = status_text(window)
        if "Baz noktayı" not in status:
            raise AssertionError(f"Right mouse did not act as Enter for selection confirmation: {status!r}")
        point_crosshair = cursor_handle(canvas)
        if point_crosshair != normal_crosshair:
            raise AssertionError("Modifier base-point phase did not switch to the + crosshair")

        # One committed Copy must finish immediately instead of staying active.
        click(canvas, cx - 150, cy)
        if cursor_handle(canvas) != normal_crosshair:
            raise AssertionError("Modifier destination-point phase did not retain the + crosshair")
        click(canvas, cx - 150, cy + 100)
        if model_count(window) != 2:
            raise AssertionError("First Copy operation did not create exactly one copy")
        if user32.SendMessageW(copy_button, BM_GETCHECK, 0, 0) == BST_CHECKED:
            raise AssertionError("Copy remained active after its first committed operation")

        # Enter while idle must restart the most recently completed modifier.
        user32.SendMessageW(canvas, WM_CHAR, 13, 0)
        assert_copy_selecting(window, copy_button)
        if cursor_handle(canvas) != selection_pickbox:
            raise AssertionError("Repeated Copy did not restart with the selection pickbox")

        # Run the repeated instance and confirm it also terminates after one execution.
        click(canvas, cx, cy)
        send_mouse(canvas, WM_RBUTTONDOWN, cx, cy)
        if cursor_handle(canvas) != normal_crosshair:
            raise AssertionError("Right-click-confirmed repeated Copy did not enter point picking")
        click(canvas, cx + 150, cy)
        send_mouse(canvas, WM_MOUSEMOVE, cx + 150, cy - 100)
        click(canvas, cx + 150, cy - 100)
        if model_count(window) != 3:
            raise AssertionError("Enter-repeated Copy did not execute exactly once")
        if user32.SendMessageW(copy_button, BM_GETCHECK, 0, 0) == BST_CHECKED:
            raise AssertionError("Repeated Copy remained active after completion")

        command(window, 602)
        capture(window, root / "build" / "modifier-interaction.png")
        print("Modifier interaction smoke passed: right-click=Enter, Copy one-shot, "
              f"idle Enter repeat, pickbox→crosshair cursor, models=1→2→3, PID={process.pid}.")
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
