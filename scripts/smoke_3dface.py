"""Verify the four-corner 3DFACE drawing command in the native Win32 GUI."""
import argparse
import ctypes
import subprocess
import time
from ctypes import wintypes
from pathlib import Path

from smoke_keyboard_input import input_point, model_count
from smoke_mirror_snap import find_process_window
from smoke_snap_style_controls import capture, command, status_text, user32


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

        command(window, 100)  # blank document
        command(window, 303)  # preserve XYZ in a visible 3D view
        command(window, 601)  # Drawing tab
        command(window, 204)  # 3DFACE
        if "3DFACE" not in status_text(window) or "1. köşeyi" not in status_text(window):
            raise AssertionError(f"3DFACE command did not enter first-corner phase: {status_text(window)!r}")

        corners = ["-3,-2,0", "3,-2,1", "3,2,3", "-3,2,1"]
        for index, corner in enumerate(corners):
            input_point(canvas, corner)
            if index < 3:
                expected = f"{index + 2}. köşeyi"
                if model_count(window) != 0 or expected not in status_text(window):
                    raise AssertionError(
                        f"3DFACE committed before four corners or lost phase {expected}: {status_text(window)!r}")

        if model_count(window) != 1 or "1. köşeyi" not in status_text(window):
            raise AssertionError("Four XYZ corners did not commit one repeat-ready 3DFACE model")
        capture(window, root / "build" / "3dface-four-corner-result.png")
        print(f"3DFACE GUI smoke passed: four XYZ corners -> one model; command reset for next face; PID={process.pid}.")
        if args.keep_open:
            keep = True
            print("Verified maximized 3DFACE instance left running on the Drawing tab.")
    finally:
        if not keep and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                process.kill()


if __name__ == "__main__":
    main()
