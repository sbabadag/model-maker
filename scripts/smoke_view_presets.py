"""GUI regression for the View ribbon preset dropdown and New-document XY plan state."""
import ctypes
import subprocess
import threading
import time
from ctypes import wintypes
from pathlib import Path

from PIL import ImageChops
from smoke_view_cube import capture

user32 = ctypes.windll.user32
WM_CANCELMODE = 0x001F
WM_CLOSE = 0x0010
WM_COMMAND = 0x0111
WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
VK_ESCAPE = 0x1B
BM_CLICK = 0x00F5
BM_GETCHECK = 0x00F0
BST_UNCHECKED = 0
MN_GETHMENU = 0x01E1
SW_MAXIMIZE = 3

user32.FindWindowW.restype = wintypes.HWND
user32.FindWindowExW.restype = wintypes.HWND
user32.GetDlgItem.restype = wintypes.HWND
user32.SendMessageW.restype = ctypes.c_ssize_t
user32.GetMenuStringW.argtypes = [wintypes.HMENU, wintypes.UINT, wintypes.LPWSTR,
                                  ctypes.c_int, wintypes.UINT]


def find_window(class_name: str, timeout: float = 5.0) -> int:
    deadline = time.time() + timeout
    while time.time() < deadline:
        hwnd = user32.FindWindowW(class_name, None)
        if hwnd:
            return hwnd
        time.sleep(0.05)
    raise RuntimeError(f"Window class {class_name!r} was not found")


def child_texts(parent: int) -> list[str]:
    texts: list[str] = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
    def collect(hwnd: int, _lparam: int) -> bool:
        if user32.GetParent(hwnd) == parent:
            length = user32.GetWindowTextLengthW(hwnd)
            if length:
                text = ctypes.create_unicode_buffer(length + 1)
                user32.GetWindowTextW(hwnd, text, len(text))
                texts.append(text.value)
        return True

    user32.EnumChildWindows(parent, collect, 0)
    return texts


def inspect_dropdown(window: int) -> None:
    dropdown = user32.GetDlgItem(window, 308)
    if not dropdown or not user32.IsWindowVisible(dropdown):
        raise AssertionError("Görünüş dropdown button is not visible on the View ribbon")

    worker = threading.Thread(target=lambda: user32.SendMessageW(dropdown, BM_CLICK, 0, 0), daemon=True)
    worker.start()
    menu_window = find_window("#32768")
    menu = user32.SendMessageW(menu_window, MN_GETHMENU, 0, 0)
    if not menu:
        raise AssertionError("Görünüş button did not open a native popup menu")

    labels: list[str] = []
    for index in range(user32.GetMenuItemCount(menu)):
        buffer = ctypes.create_unicode_buffer(128)
        user32.GetMenuStringW(menu, index, buffer, len(buffer), 0x0400)  # MF_BYPOSITION
        if buffer.value:
            labels.append(buffer.value)
    expected = ["Önden Görünüş", "Arkadan Görünüş", "Sol Görünüş", "Sağ Görünüş", "ISO Görünüş"]
    if labels != expected:
        raise AssertionError(f"Preset menu labels are {labels}, expected {expected}")

    user32.PostMessageW(menu_window, WM_KEYDOWN, VK_ESCAPE, 0)
    user32.PostMessageW(menu_window, WM_KEYUP, VK_ESCAPE, 0)
    user32.PostMessageW(window, WM_CANCELMODE, 0, 0)
    worker.join(timeout=2)
    if worker.is_alive():
        raise RuntimeError("Preset popup menu did not close")


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    executable = root / "build" / "model-maker.exe"
    process = subprocess.Popen([str(executable)], cwd=root)
    hwnd = 0
    try:
        hwnd = find_window("ModelMakerWindow")
        user32.ShowWindow(hwnd, SW_MAXIMIZE)
        user32.UpdateWindow(hwnd)
        user32.SendMessageW(user32.GetDlgItem(hwnd, 603), BM_CLICK, 0, 0)  # Görünüm tab
        time.sleep(0.2)
        inspect_dropdown(hwnd)

        canvas = user32.FindWindowExW(hwnd, 0, "ModelMakerCanvas", None)
        if not canvas:
            raise RuntimeError("Drawing canvas was not found")
        user32.SendMessageW(hwnd, WM_COMMAND, 300, 0)  # Add cube for visible camera changes.
        snapshots = []
        for command, name in ((320, "front"), (321, "back"), (322, "left"),
                              (323, "right"), (324, "iso")):
            user32.SendMessageW(hwnd, WM_COMMAND, command, 0)
            time.sleep(0.12)
            snapshots.append(capture(canvas, root / "build" / f"view-preset-{name}.png"))
        for before, after in zip(snapshots, snapshots[1:]):
            if ImageChops.difference(before, after).getbbox() is None:
                raise AssertionError("Two consecutive standard-view commands rendered identically")

        user32.SendMessageW(hwnd, WM_COMMAND, 100, 0)  # New
        time.sleep(0.15)
        if user32.SendMessageW(user32.GetDlgItem(hwnd, 303), BM_GETCHECK, 0, 0) != BST_UNCHECKED:
            raise AssertionError("New document did not leave 3D mode")
        if not any("2B Plan XY" in text for text in child_texts(hwnd)):
            raise AssertionError("New document status does not identify the default 2D XY plan")
        capture(hwnd, root / "build" / "new-document-xy-plan.png")
        print("View preset smoke passed: dropdown labels, five camera presets, and New -> 2B Plan XY verified.")
    finally:
        if hwnd:
            user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=3)


if __name__ == "__main__":
    main()
