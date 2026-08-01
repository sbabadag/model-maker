"""Verify command-safe snap checkboxes and style dropdowns in the native Win32 UI."""
import argparse
import ctypes
import subprocess
import time
from ctypes import wintypes
from pathlib import Path

from smoke_mirror_snap import click, find_process_window
from smoke_view_cube import capture

user32 = ctypes.windll.user32
WM_CLOSE = 0x0010
WM_COMMAND = 0x0111
WM_CHAR = 0x0102
WM_KEYDOWN = 0x0100
BM_GETCHECK = 0x00F0
BM_SETCHECK = 0x00F1
BST_CHECKED = 1
CB_GETCURSEL = 0x0147
CB_SETCURSEL = 0x014E
SW_MAXIMIZE = 3
VK_ESCAPE = 0x1B
VK_F8 = 0x77


def command(hwnd: int, control_id: int, notification: int = 0) -> None:
    user32.SendMessageW(hwnd, WM_COMMAND, control_id | (notification << 16), 0)


def status_text(window: int) -> str:
    texts: list[str] = []
    callback_type = ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
    def visit(hwnd: int, _parameter: int) -> bool:
        name = ctypes.create_unicode_buffer(32)
        user32.GetClassNameW(hwnd, name, 32)
        if name.value == "Static":
            length = user32.GetWindowTextLengthW(hwnd)
            if length > 30:
                value = ctypes.create_unicode_buffer(length + 1)
                user32.GetWindowTextW(hwnd, value, length + 1)
                texts.append(value.value)
        return True
    user32.EnumChildWindows(window, callback_type(visit), 0)
    return max(texts, key=len, default="")


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
        time.sleep(0.5)
        canvas = user32.FindWindowExW(window, 0, "ModelMakerCanvas", None)
        if not canvas:
            raise RuntimeError("Canvas window was not found")

        layer = user32.GetDlgItem(window, 800)
        color = user32.GetDlgItem(window, 801)
        line_type = user32.GetDlgItem(window, 802)
        if not all((layer, color, line_type)):
            raise AssertionError("Layer/color/linetype dropdowns were not created")
        if not all(user32.IsWindowVisible(control) for control in (layer, color, line_type)):
            raise AssertionError("A style dropdown is not visible")

        # Start Move, then open the modeless snap panel and change a checkbox.
        command(window, 602)  # Modify tab
        command(window, 500)  # Move
        command(window, 604)  # Aids tab; must not cancel Move
        command(window, 403)  # Snap settings panel
        endpoint = user32.GetDlgItem(window, 720)
        move = user32.GetDlgItem(window, 500)
        if not endpoint or not user32.IsWindowVisible(endpoint):
            raise AssertionError("Endpoint checkbox is not visible in the snap panel")
        for control_id in range(720, 734):
            checkbox = user32.GetDlgItem(window, control_id)
            if not checkbox or not user32.IsWindowVisible(checkbox):
                raise AssertionError(f"Snap checkbox {control_id} is missing or hidden")

        user32.SendMessageW(endpoint, BM_SETCHECK, 0, 0)
        command(window, 720)
        if user32.SendMessageW(move, BM_GETCHECK, 0, 0) != BST_CHECKED:
            raise AssertionError("Changing a snap checkbox cancelled the active Move command")
        if user32.SendMessageW(endpoint, BM_GETCHECK, 0, 0) != 0:
            raise AssertionError("Endpoint checkbox did not switch off")
        user32.SendMessageW(endpoint, BM_SETCHECK, BST_CHECKED, 0)
        command(window, 720)
        user32.SendMessageW(canvas, WM_KEYDOWN, VK_ESCAPE, 0)
        command(window, 403)  # Close the panel during canvas geometry verification.

        # Select explicit red + dashed style and draw through the real canvas path.
        user32.SendMessageW(color, CB_SETCURSEL, 2, 0)
        command(window, 801, 1)  # CBN_SELCHANGE
        user32.SendMessageW(line_type, CB_SETCURSEL, 2, 0)
        command(window, 802, 1)
        command(window, 601)  # Drawing tab
        command(window, 200)  # Line
        rect = wintypes.RECT()
        user32.GetClientRect(canvas, ctypes.byref(rect))
        cy = rect.bottom // 2 - 90
        click(canvas, rect.right // 2 - 220, cy)
        click(canvas, rect.right // 2 + 220, cy)
        time.sleep(0.2)
        image = capture(canvas, root / "build" / "style-dropdown-line.png").convert("RGB")
        red_pixels = sum(1 for r, g, b in image.get_flattened_data()
                         if r > 210 and g < 120 and b < 120)
        if red_pixels < 80:
            raise AssertionError(f"Explicit red style did not render ({red_pixels} pixels)")
        red_by_row: dict[int, list[int]] = {}
        for py in range(image.height):
            for px in range(image.width):
                r, g, b = image.getpixel((px, py))
                if r > 210 and g < 120 and b < 120:
                    red_by_row.setdefault(py, []).append(px)
        line_y, line_xs = max(
            ((row, xs) for row, xs in red_by_row.items() if len(xs) < image.width // 2),
            key=lambda item: len(item[1]))
        line_left, line_right = min(line_xs), max(line_xs)

        # Exercise strict F8 through the actual Move modifier destination phase.
        command(window, 602)
        command(window, 500)
        click(canvas, line_left - 20, line_y - 20)
        click(canvas, line_right + 20, line_y + 20)
        user32.SendMessageW(canvas, WM_CHAR, 13, 0)
        phase_after_enter = status_text(window)
        click(canvas, line_left, line_y)
        phase_after_base = status_text(window)
        user32.SendMessageW(canvas, WM_KEYDOWN, VK_F8, 0)
        click(canvas, line_left + 180, line_y + 70)  # Dominant X movement must keep Y unchanged.
        final_phase = status_text(window)
        if "Komut: MOVE" in final_phase:
            raise AssertionError("Move did not complete after the F8-constrained destination pick; "
                                 f"after Enter={phase_after_enter!r}; after base={phase_after_base!r}; "
                                 f"final={final_phase!r}")
        time.sleep(0.15)
        moved = capture(canvas, root / "build" / "modifier-f8.png").convert("RGB")
        expected = moved.crop((line_left + 174, line_y - 4, line_right + 186, line_y + 5))
        moved_red = sum(1 for r, g, b in expected.get_flattened_data()
                        if r > 210 and g < 120 and b < 120)
        if moved_red < 80:
            raise AssertionError(f"F8 Move destination was not horizontally constrained ({moved_red} pixels)")
        user32.SendMessageW(canvas, WM_KEYDOWN, VK_F8, 0)  # Restore Ortho off.

        # Leave the requested snap controls open for inspection.
        command(window, 604)
        command(window, 403)
        capture(window, root / "build" / "snap-style-controls.png")
        print(f"Snap/style GUI smoke passed: 14 checkboxes, command-safe toggle, "
              f"red styled pixels={red_pixels}, F8 moved pixels={moved_red}, PID={process.pid}.")
        if args.keep_open:
            window = 0
            print("Verified maximized instance left running with snap panel open.")
    finally:
        if window:
            user32.PostMessageW(window, WM_CLOSE, 0, 0)
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=3)


if __name__ == "__main__":
    main()
