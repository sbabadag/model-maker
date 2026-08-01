"""Verify the neutral arrow deactivates commands and suppresses effective snap."""
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
WM_SETCURSOR = 0x0020
WM_CURSOR_PROBE = 0x8001
WM_HOVER_PROBE = 0x8002
BM_GETCHECK = 0x00F0
BST_CHECKED = 1
HTCLIENT = 1
VK_F9 = 0x78


def cursor_handle(canvas: int) -> int:
    user32.SendMessageW(canvas, WM_SETCURSOR, canvas, HTCLIENT)
    return int(user32.SendMessageW(canvas, WM_CURSOR_PROBE, 0, 0))


def hover_active(canvas: int) -> bool:
    return bool(user32.SendMessageW(canvas, WM_HOVER_PROBE, 0, 0))


def model_count(window: int) -> int:
    match = re.search(r"Nesne:\s*(\d+)", status_text(window))
    if not match:
        raise AssertionError(f"Could not parse model count from {status_text(window)!r}")
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
        neutral = user32.GetDlgItem(window, 509)
        move = user32.GetDlgItem(window, 500)
        line = user32.GetDlgItem(window, 200)
        if not canvas or not neutral or not move or not line:
            raise RuntimeError("Neutral-arrow test controls were not found")

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

        # Active Line command: crosshair and effective snapping are available.
        command(window, 601)
        command(window, 200)
        click(canvas, cx - 160, cy)
        click(canvas, cx + 160, cy)
        if model_count(window) != 1:
            raise AssertionError("Neutral-arrow source geometry was not created")
        drawing_crosshair = cursor_handle(canvas)
        send_mouse(canvas, WM_MOUSEMOVE, cx - 160, cy)
        if not hover_active(canvas):
            raise AssertionError("Active drawing command did not evaluate snap/hover")

        # Start a modifier, then neutralize every command using the arrow button.
        command(window, 602)
        command(window, 500)
        selection_pickbox = cursor_handle(canvas)
        if selection_pickbox == drawing_crosshair:
            raise AssertionError("Move entity selection did not use the pickbox")
        command(window, 509)
        if user32.SendMessageW(neutral, BM_GETCHECK, 0, 0) != BST_CHECKED:
            raise AssertionError("Neutral arrow did not become active")
        if user32.SendMessageW(move, BM_GETCHECK, 0, 0) == BST_CHECKED or \
           user32.SendMessageW(line, BM_GETCHECK, 0, 0) == BST_CHECKED:
            raise AssertionError("Neutral arrow did not deactivate drawing/modifier buttons")
        neutral_arrow = cursor_handle(canvas)
        if neutral_arrow in (drawing_crosshair, selection_pickbox):
            raise AssertionError("Neutral state did not use the standard arrow cursor")

        send_mouse(canvas, WM_MOUSEMOVE, cx - 160, cy)
        if hover_active(canvas):
            raise AssertionError("Snap/hover remained active with no command")
        status = status_text(window)
        if "PASİF" not in status or "OSNAP: Pasif (komut yok)" not in status:
            raise AssertionError(f"Neutral status did not report disabled snap: {status!r}")

        # Canvas clicks and Enter must remain inert after the explicit neutral reset.
        click(canvas, cx - 100, cy + 120)
        click(canvas, cx + 100, cy + 120)
        user32.SendMessageW(canvas, WM_CHAR, 13, 0)
        if model_count(window) != 1 or user32.SendMessageW(neutral, BM_GETCHECK, 0, 0) != BST_CHECKED:
            raise AssertionError("Neutral mode allowed drawing or repeated a cleared command")

        command(window, 602)
        capture(window, root / "build" / "neutral-arrow.png")
        print("Neutral-arrow GUI smoke passed: all commands inactive, arrow cursor active, "
              f"effective snap off, canvas clicks inert, models=1, PID={process.pid}.")
        if args.keep_open:
            keep = True
            print("Verified maximized instance left running on the Modify tab in neutral mode.")
    finally:
        if not keep and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()


if __name__ == "__main__":
    main()
