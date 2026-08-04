"""Verify Üstten/Alttan entries and exact top/bottom camera projection."""
import argparse
import ctypes
import subprocess
import time
from ctypes import wintypes
from pathlib import Path

from smoke_arrays_3d import projected_vertex
from smoke_mirror_snap import find_process_window
from smoke_snap_style_controls import capture, command, user32

WM_CLOSE = 0x0010
WM_COMMAND = 0x0111
WM_KEYDOWN = 0x0100
VK_ESCAPE = 0x1B
MN_GETHMENU = 0x01E1
SW_MAXIMIZE = 3
CMD_STANDARD_VIEW = 308
CMD_VIEW_TOP = 325
CMD_VIEW_BOTTOM = 326


def popup_labels(window: int) -> list[str]:
    user32.PostMessageW(window, WM_COMMAND, CMD_STANDARD_VIEW, 0)
    popup = 0
    for _ in range(50):
        popup = user32.FindWindowW("#32768", None)
        if popup and user32.IsWindowVisible(popup):
            break
        time.sleep(0.04)
    if not popup:
        raise AssertionError("Görünüş dropdown menu did not open")
    menu = user32.SendMessageW(popup, MN_GETHMENU, 0, 0)
    if not menu:
        raise AssertionError("Could not inspect Görünüş popup menu")
    count = user32.GetMenuItemCount(menu)
    labels: list[str] = []
    for index in range(count):
        buffer = ctypes.create_unicode_buffer(128)
        user32.GetMenuStringW(menu, index, buffer, len(buffer), 0x400)
        if buffer.value:
            labels.append(buffer.value)
    user32.PostMessageW(popup, WM_KEYDOWN, VK_ESCAPE, 0)
    return labels


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--keep-open", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    process = subprocess.Popen([str(root / "build" / "model-maker.exe")])
    window = 0
    keep = False
    try:
        window = find_process_window(process.pid)
        user32.ShowWindow(window, SW_MAXIMIZE)
        user32.SetForegroundWindow(window)
        time.sleep(0.25)
        canvas = user32.FindWindowExW(window, 0, "ModelMakerCanvas", None)
        if not canvas:
            raise RuntimeError("Canvas not found")
        command(window, 603)
        labels = popup_labels(window)
        for expected in ("Üstten Görünüş", "Alttan Görünüş"):
            if expected not in labels:
                raise AssertionError(f"Missing {expected!r} in Görünüş menu: {labels!r}")

        command(window, 300)  # cube
        command(window, CMD_VIEW_TOP)
        top0, top4 = projected_vertex(canvas, 0), projected_vertex(canvas, 4)
        if not top0[1] < top4[1]:
            raise AssertionError(f"Top projection did not look down local/world +Y: {top0=}, {top4=}")
        capture(window, root / "build" / "standard-view-top.png")

        command(window, CMD_VIEW_BOTTOM)
        bottom0, bottom4 = projected_vertex(canvas, 0), projected_vertex(canvas, 4)
        if not bottom0[1] > bottom4[1]:
            raise AssertionError(f"Bottom projection did not reverse depth orientation: {bottom0=}, {bottom4=}")
        capture(window, root / "build" / "standard-view-bottom.png")
        print(f"Top/Bottom standard-view smoke passed: menu={labels}; top={top0}/{top4}; bottom={bottom0}/{bottom4}; PID={process.pid}.")
        if args.keep_open:
            keep = True
            print("Verified maximized instance left open in Alttan Görünüş.")
    finally:
        if window and not keep:
            user32.PostMessageW(window, WM_CLOSE, 0, 0)
        if not keep:
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=3)


if __name__ == "__main__":
    main()
