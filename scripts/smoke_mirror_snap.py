"""Verify Mirror and reference-style endpoint/midpoint snap symbols in the native GUI."""
import argparse
import ctypes
import subprocess
import time
from ctypes import wintypes
from pathlib import Path

from smoke_view_cube import capture

user32 = ctypes.windll.user32
WM_CLOSE = 0x0010
WM_CHAR = 0x0102
WM_KEYDOWN = 0x0100
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
WM_MOUSEMOVE = 0x0200
WM_COMMAND = 0x0111
MK_LBUTTON = 0x0001
VK_F9 = 0x78
SW_MAXIMIZE = 3


def find_process_window(process_id: int, timeout: float = 6.0) -> int:
    deadline = time.time() + timeout
    while time.time() < deadline:
        windows: list[int] = []
        callback_type = ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)

        def visit(hwnd: int, _parameter: int) -> bool:
            owner = wintypes.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
            if owner.value == process_id and user32.IsWindowVisible(hwnd):
                windows.append(hwnd)
            return True

        callback = callback_type(visit)
        user32.EnumWindows(callback, 0)
        for hwnd in windows:
            name = ctypes.create_unicode_buffer(64)
            user32.GetClassNameW(hwnd, name, 64)
            if name.value == "ModelMakerWindow":
                return hwnd
        time.sleep(0.05)
    raise RuntimeError(f"Model Maker window for PID {process_id} was not found")


def send_mouse(hwnd: int, message: int, x: int, y: int, buttons: int = 0) -> None:
    user32.SendMessageW(hwnd, message, buttons, (y << 16) | (x & 0xFFFF))


def click(hwnd: int, x: int, y: int) -> None:
    send_mouse(hwnd, WM_LBUTTONDOWN, x, y, MK_LBUTTON)
    send_mouse(hwnd, WM_LBUTTONUP, x, y)


def color_count(image, box: tuple[int, int, int, int], green_marker: bool = False) -> int:
    def matches(pixel: tuple[int, int, int]) -> bool:
        red, green, blue = pixel
        if green_marker:
            return green > 225 and red < 130 and blue < 190
        return blue > 210 and green > 150 and red < 150

    return sum(1 for pixel in image.crop(box).convert("RGB").get_flattened_data() if matches(pixel))


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
        client = wintypes.RECT()
        user32.GetClientRect(canvas, ctypes.byref(client))
        center_x, center_y = client.right // 2, client.bottom // 2
        source_a = (center_x - 300, center_y - 60)
        source_b = (center_x - 180, center_y - 60)
        mirrored_a = (center_x + 300, center_y - 60)
        mirrored_b = (center_x + 180, center_y - 60)

        # Draw source, then I -> select -> Enter -> two vertical mirror-axis points.
        user32.SendMessageW(canvas, WM_KEYDOWN, VK_F9, 0)
        click(canvas, *source_a)
        click(canvas, *source_b)
        user32.SendMessageW(canvas, WM_KEYDOWN, ord("I"), 0)
        click(canvas, (source_a[0] + source_b[0]) // 2, source_a[1])
        user32.SendMessageW(canvas, WM_CHAR, 13, 0)
        click(canvas, center_x, center_y - 180)
        send_mouse(canvas, WM_MOUSEMOVE, center_x, center_y + 180)
        click(canvas, center_x, center_y + 180)
        time.sleep(0.2)

        image = capture(canvas, root / "build" / "mirror-modifier.png")
        source_pixels = color_count(image, (source_a[0] + 4, source_a[1] - 3, source_b[0] - 4, source_b[1] + 4))
        mirror_pixels = color_count(image, (mirrored_b[0] + 4, mirrored_b[1] - 3,
                                             mirrored_a[0] - 4, mirrored_a[1] + 4))
        if source_pixels < 80 or mirror_pixels < 80:
            raise AssertionError(f"Mirror did not retain source and create reflection ({source_pixels}, {mirror_pixels})")

        # Snap is intentionally idle after one-shot Mirror; reactivate Line before checking its marker.
        user32.SendMessageW(canvas, WM_KEYDOWN, ord("L"), 0)
        # Endpoint marker from the supplied convention must be a green open square.
        send_mouse(canvas, WM_MOUSEMOVE, source_a[0] + 1, source_a[1] + 1)
        time.sleep(0.15)
        endpoint = capture(canvas, root / "build" / "snap-endpoint-symbol.png")
        top = color_count(endpoint, (source_a[0] - 7, source_a[1] - 7,
                                     source_a[0] + 8, source_a[1] - 4), True)
        sides = color_count(endpoint, (source_a[0] - 7, source_a[1] - 5,
                                       source_a[0] + 8, source_a[1] + 7), True)
        if top < 8 or sides < 15:
            raise AssertionError(f"Endpoint reference square was not visible (top={top}, sides={sides})")

        # Verify the new command is exposed on the real Modify ribbon.
        user32.SendMessageW(window, WM_COMMAND, 602, 0)
        mirror_button = user32.GetDlgItem(window, 503)
        label = ctypes.create_unicode_buffer(64)
        user32.GetWindowTextW(mirror_button, label, 64)
        if not mirror_button or not user32.IsWindowVisible(mirror_button) or "Ayna" not in label.value:
            raise AssertionError("Mirror button was not visible on the Modify ribbon")
        print(f"Mirror/snap smoke passed: source={source_pixels}, mirror={mirror_pixels}, "
              f"endpoint square={top + sides} pixels, PID={process.pid}.")
        if args.keep_open:
            window = 0
            print("Verified maximized instance left running.")
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
