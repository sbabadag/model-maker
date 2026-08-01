"""Native GUI smoke test for Delete, Linear Array, and Polar Array modifiers."""
import argparse
import re
import subprocess
import time
from ctypes import wintypes
from pathlib import Path

from smoke_snap_style_controls import (
    CB_SETCURSEL, WM_CHAR, capture, click, command, find_process_window, status_text, user32,
)

CBN_SELCHANGE = 1


def model_count(window: int) -> int:
    match = re.search(r"Nesne:\s*(\d+)", status_text(window))
    if not match:
        raise AssertionError(f"Model count unavailable: {status_text(window)!r}")
    return int(match.group(1))


def char(canvas: int, value: str) -> None:
    for character in value:
        user32.SendMessageW(canvas, WM_CHAR, ord(character), 0)


def require_phase(window: int, phrase: str) -> None:
    status = status_text(window)
    if phrase not in status:
        raise AssertionError(f"Expected phase {phrase!r}, got {status!r}")


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
        time.sleep(0.4)
        canvas = user32.FindWindowExW(window, 0, "ModelMakerCanvas", None)
        if not canvas:
            raise RuntimeError("Canvas window was not found")
        buttons = {control_id: user32.GetDlgItem(window, control_id) for control_id in range(500, 507)}
        command(window, 602)
        if not all(user32.IsWindowVisible(button) for button in buttons.values()):
            raise AssertionError("All seven Modify buttons are not visible")

        # Draw two explicit-red source lines on exact grid coordinates.
        color = user32.GetDlgItem(window, 801)
        user32.SendMessageW(color, CB_SETCURSEL, 2, 0)
        command(window, 801, CBN_SELCHANGE)
        rect = wintypes.RECT()
        user32.GetClientRect(canvas, rect)
        cx, cy = rect.right // 2, rect.bottom // 2
        x0, x1 = cx - 180, cx - 60
        y_delete, y_array = cy - 120, cy + 120
        command(window, 601)
        command(window, 200)
        click(canvas, x0, y_delete); click(canvas, x1, y_delete)
        click(canvas, x0, y_array); click(canvas, x1, y_array)
        if model_count(window) != 2:
            raise AssertionError("Two source lines were not created")

        # Delete exactly the upper source and retain the lower source.
        command(window, 602)
        command(window, 504)
        click(canvas, x0 - 30, y_delete - 30)
        click(canvas, x1 + 30, y_delete + 30)
        user32.SendMessageW(canvas, WM_CHAR, 13, 0)
        if model_count(window) != 1:
            raise AssertionError("Delete did not remove exactly one selected entity")

        # Four-item linear array: source plus three copies at a 3-unit X spacing.
        command(window, 505)
        click(canvas, x0 - 30, y_array - 30)
        click(canvas, x1 + 30, y_array + 30)
        user32.SendMessageW(canvas, WM_CHAR, 13, 0)
        require_phase(window, "Öğe sayısını")
        char(canvas, "4"); user32.SendMessageW(canvas, WM_CHAR, 13, 0)
        require_phase(window, "Baz noktayı")
        click(canvas, x0, y_array)
        require_phase(window, "ikinci noktayı")
        user32.SendMessageW(canvas, 0x0200, 0, ((y_array & 0xFFFF) << 16) | ((x0 + 180) & 0xFFFF))
        time.sleep(0.1)
        capture(canvas, root / "build" / "linear-array-preview.png")
        click(canvas, x0 + 180, y_array)
        if model_count(window) != 4:
            raise AssertionError("Linear Array did not create the requested four total items")

        # Four-item full-circle polar array around an explicitly picked center.
        command(window, 506)
        click(canvas, x0 - 30, y_array - 30)
        click(canvas, x1 + 30, y_array + 30)
        user32.SendMessageW(canvas, WM_CHAR, 13, 0)
        require_phase(window, "Öğe sayısını")
        char(canvas, "4"); user32.SendMessageW(canvas, WM_CHAR, 13, 0)
        require_phase(window, "Merkez noktasını")
        polar_center_x = cx + 60
        user32.SendMessageW(canvas, 0x0200, 0,
                            ((y_array & 0xFFFF) << 16) | (polar_center_x & 0xFFFF))
        time.sleep(0.1)
        capture(canvas, root / "build" / "polar-array-preview.png")
        click(canvas, polar_center_x, y_array)
        if model_count(window) != 7:
            raise AssertionError("Polar Array did not retain the four existing items and add three copies")

        command(window, 602)
        capture(window, root / "build" / "delete-linear-polar-array.png")
        print(f"Delete/array GUI smoke passed: delete 2→1, linear 1→4, polar 4→7, PID={process.pid}.")
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
