"""Verify the single Wireframe/Solid/Transparent dropdown in the native GUI."""
import argparse
import ctypes
import subprocess
import time
from pathlib import Path

from PIL import ImageChops

from smoke_keyboard_input import model_count
from smoke_mirror_snap import find_process_window
from smoke_snap_style_controls import command, status_text, user32
from smoke_view_cube import capture

SW_MAXIMIZE = 3
STYLE_BUTTON_ID = 307
STYLE_COMMANDS = {310: "Wireframe", 311: "Solid", 312: "Saydam"}
WM_COMMAND = 0x0111
WM_KEYDOWN = 0x0100
VK_ESCAPE = 0x1B


def changed_pixels(left, right) -> int:
    difference = ImageChops.difference(left, right)
    return sum(1 for pixel in difference.get_flattened_data() if pixel != (0, 0, 0))


def button_text(window: int) -> str:
    button = user32.GetDlgItem(window, STYLE_BUTTON_ID)
    text = ctypes.create_unicode_buffer(64)
    user32.GetWindowTextW(button, text, len(text))
    return text.value


def assert_style(window: int, command_id: int, label: str) -> None:
    command(window, command_id)
    if label not in button_text(window):
        raise AssertionError(f"Visual-style dropdown did not update to {label}: {button_text(window)!r}")
    if f"Görünüm: {label}" not in status_text(window):
        raise AssertionError(f"Status did not report {label}: {status_text(window)!r}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--keep-open", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    process = subprocess.Popen([str(root / "build" / "model-maker.exe")])
    keep = False
    try:
        window = find_process_window(process.pid)
        user32.ShowWindow(window, SW_MAXIMIZE)
        user32.SetForegroundWindow(window)
        time.sleep(0.35)
        canvas = user32.FindWindowExW(window, 0, "ModelMakerCanvas", None)
        if not canvas:
            raise RuntimeError("Canvas window was not found")

        command(window, 603)  # View tab
        dropdown = user32.GetDlgItem(window, STYLE_BUTTON_ID)
        if not dropdown or not user32.IsWindowVisible(dropdown):
            raise AssertionError("The single visual-style dropdown button is not visible")
        if any(user32.GetDlgItem(window, command_id) for command_id in (310, 311, 312)):
            raise AssertionError("Legacy separate visual-style buttons still exist")

        # Exercise the actual button route and confirm its native popup menu opens.
        user32.PostMessageW(window, WM_COMMAND, STYLE_BUTTON_ID, 0)
        popup = 0
        for _ in range(40):
            popup = user32.FindWindowW("#32768", None)
            if popup and user32.IsWindowVisible(popup):
                break
            time.sleep(0.05)
        if not popup or not user32.IsWindowVisible(popup):
            raise AssertionError("Clicking the visual-style button did not open its dropdown menu")
        user32.PostMessageW(popup, WM_KEYDOWN, VK_ESCAPE, 0)
        time.sleep(0.1)

        command(window, 300)  # Cube with six fillable faces
        if model_count(window) != 1:
            raise AssertionError("Cube command did not create exactly one filled-capable object")

        command(window, 603)
        assert_style(window, 310, "Wireframe")
        wireframe = capture(canvas, root / "build" / "visual-style-wireframe.png")
        assert_style(window, 311, "Solid")
        solid = capture(canvas, root / "build" / "visual-style-solid.png")
        assert_style(window, 312, "Saydam")
        transparent = capture(canvas, root / "build" / "visual-style-transparent.png")

        wire_solid = changed_pixels(wireframe, solid)
        wire_transparent = changed_pixels(wireframe, transparent)
        solid_transparent = changed_pixels(solid, transparent)
        if min(wire_solid, wire_transparent, solid_transparent) < 500:
            raise AssertionError(
                "Visual modes did not produce materially different object interiors: "
                f"wire-solid={wire_solid}, wire-transparent={wire_transparent}, "
                f"solid-transparent={solid_transparent}")
        if model_count(window) != 1:
            raise AssertionError("Changing global visual style mutated the document")

        assert_style(window, 311, "Solid")
        print("Visual-style GUI smoke passed: one Wireframe/Solid/Saydam dropdown button; "
              f"canvas deltas={wire_solid}/{wire_transparent}/{solid_transparent}; PID={process.pid}.")
        if args.keep_open:
            keep = True
            print("Verified maximized instance left open in Solid mode on the View tab.")
    finally:
        if not keep and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                process.kill()


if __name__ == "__main__":
    main()
