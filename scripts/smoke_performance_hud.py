"""Verify the F11 performance HUD through the native Win32 event path."""
import argparse
import ctypes
import subprocess
import sys
import time
from pathlib import Path

from smoke_snap_style_controls import find_process_window, status_text, user32
from smoke_view_cube import capture

WM_CLOSE = 0x0010
WM_KEYDOWN = 0x0100
VK_F11 = 0x7A
SW_MAXIMIZE = 3


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--keep-open", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    process = subprocess.Popen([str(root / "build" / "model-maker.exe")])
    window = 0
    try:
        window = find_process_window(process.pid)
        user32.ShowWindow(window, SW_MAXIMIZE)
        user32.SetForegroundWindow(window)
        time.sleep(0.4)
        canvas = user32.FindWindowExW(window, 0, "ModelMakerCanvas", None)
        if not canvas:
            raise RuntimeError("Canvas window was not found")

        user32.SendMessageW(canvas, WM_KEYDOWN, VK_F11, 0)
        user32.UpdateWindow(canvas)
        time.sleep(0.25)
        enabled_status = status_text(window)
        if "PERF F11: Açık" not in enabled_status:
            raise AssertionError(f"F11 did not enable the performance HUD: {enabled_status}")
        screenshot = root / "build" / "performance-hud.png"
        capture(window, screenshot)

        user32.SendMessageW(canvas, WM_KEYDOWN, VK_F11, 0)
        user32.UpdateWindow(canvas)
        time.sleep(0.15)
        disabled_status = status_text(window)
        if "PERF F11: Kapalı" not in disabled_status:
            raise AssertionError(f"F11 did not disable the performance HUD: {disabled_status}")

        print(f"Performance HUD smoke passed: screenshot={screenshot}; PID={process.pid}")
        if args.keep_open:
            process.wait()
            return
    finally:
        if process.poll() is None:
            if window:
                user32.PostMessageW(window, WM_CLOSE, 0, 0)
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"Performance HUD smoke failed: {error}", file=sys.stderr)
        raise
