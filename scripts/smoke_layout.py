"""Regression smoke test for the compact top ribbon and functional tab grouping."""
import ctypes
import subprocess
import time
from ctypes import wintypes
from pathlib import Path

from PIL import ImageChops
from smoke_view_cube import capture

user32 = ctypes.windll.user32
GWL_STYLE = -16
WS_CLIPSIBLINGS = 0x04000000
WM_CLOSE = 0x0010
BM_CLICK = 0x00F5
BM_GETCHECK = 0x00F0
BST_CHECKED = 1
SW_MAXIMIZE = 3
RDW_INVALIDATE = 0x0001
RDW_ERASE = 0x0004
RDW_ALLCHILDREN = 0x0080
RDW_UPDATENOW = 0x0100

TABS = {
    600: (100, 101, 102, 103, 104),
    601: (200, 201, 202, 203),
    602: (500, 501),
    603: (300, 301, 302, 303, 304, 305, 306),
    604: (400, 401, 402),
}
ALL_COMMANDS = tuple(command for commands in TABS.values() for command in commands)


def find_window(timeout: float = 5.0) -> int:
    deadline = time.time() + timeout
    while time.time() < deadline:
        hwnd = user32.FindWindowW("ModelMakerWindow", None)
        if hwnd:
            return hwnd
        time.sleep(0.05)
    raise RuntimeError("Model Maker window was not found")


def direct_children(parent: int) -> list[int]:
    children: list[int] = []
    @ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
    def collect(hwnd: int, _lparam: int) -> bool:
        if user32.GetParent(hwnd) == parent:
            children.append(hwnd)
        return True
    user32.EnumChildWindows(parent, collect, 0)
    return children


def relative_rect(hwnd: int, parent: int) -> wintypes.RECT:
    rect = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    user32.MapWindowPoints(0, parent, ctypes.byref(rect), 2)
    return rect


def assert_tab(window: int, tab_id: int) -> None:
    tab = user32.GetDlgItem(window, tab_id)
    if not tab or not user32.IsWindowVisible(tab):
        raise AssertionError(f"Ribbon tab {tab_id} is missing")
    user32.SendMessageW(tab, BM_CLICK, 0, 0)
    time.sleep(0.08)
    if user32.SendMessageW(tab, BM_GETCHECK, 0, 0) != BST_CHECKED:
        raise AssertionError(f"Ribbon tab {tab_id} did not become active")

    expected = set(TABS[tab_id])
    visible = {command for command in ALL_COMMANDS
               if user32.IsWindowVisible(user32.GetDlgItem(window, command))}
    if visible != expected:
        raise AssertionError(f"Tab {tab_id} showed {sorted(visible)}, expected {sorted(expected)}")
    for command in visible:
        rect = relative_rect(user32.GetDlgItem(window, command), window)
        if rect.top < 36 or rect.bottom > 104 or rect.right - rect.left > 74:
            raise AssertionError(f"Command {command} is not a compact top-ribbon button: {tuple(rect)}")


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    executable = root / "build" / "model-maker.exe"
    process = subprocess.Popen([str(executable)])
    hwnd = 0
    try:
        hwnd = find_window()
        user32.ShowWindow(hwnd, SW_MAXIMIZE)
        user32.UpdateWindow(hwnd)
        time.sleep(0.3)

        children = direct_children(hwnd)
        unsafe = [child for child in children
                  if not (user32.GetWindowLongW(child, GWL_STYLE) & WS_CLIPSIBLINGS)]
        if unsafe:
            raise AssertionError("A direct ribbon/canvas child lacks WS_CLIPSIBLINGS")

        for command in (*ALL_COMMANDS, *TABS.keys()):
            if not user32.GetDlgItem(hwnd, command):
                raise AssertionError(f"Ribbon control {command} was not created")

        canvas = user32.FindWindowExW(hwnd, 0, "ModelMakerCanvas", None)
        canvas_rect = relative_rect(canvas, hwnd)
        client = wintypes.RECT(); user32.GetClientRect(hwnd, ctypes.byref(client))
        if canvas_rect.left != 0 or abs(canvas_rect.right - client.right) > 1 or canvas_rect.top != 104:
            raise AssertionError(f"Canvas does not fill width below ribbon: {tuple(canvas_rect)}")

        # The application starts on Drawing; exercise every functional group.
        if user32.SendMessageW(user32.GetDlgItem(hwnd, 601), BM_GETCHECK, 0, 0) != BST_CHECKED:
            raise AssertionError("Drawing tab is not active on startup")
        for tab_id in (600, 601, 602, 603, 604):
            assert_tab(hwnd, tab_id)
            capture(hwnd, root / "build" / f"ribbon-tab-{tab_id}.png")

        screenshot = capture(hwnd, root / "build" / "ribbon-layout.png")
        user32.RedrawWindow(hwnd, None, None,
                            RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW)
        repainted = capture(hwnd, root / "build" / "ribbon-layout-repainted.png")
        ribbon_diff = ImageChops.difference(screenshot.crop((0, 0, screenshot.width, 104)),
                                            repainted.crop((0, 0, screenshot.width, 104)))
        changed = sum(1 for pixel in ribbon_diff.get_flattened_data() if pixel != (0, 0, 0))
        if changed > 100:
            raise AssertionError(f"Top ribbon contains stale/overlapping paint ({changed} pixels)")

        osnap = user32.GetDlgItem(hwnd, 400)
        if user32.SendMessageW(osnap, BM_GETCHECK, 0, 0) != BST_CHECKED:
            raise AssertionError("OSNAP did not start checked")
        user32.SendMessageW(osnap, BM_CLICK, 0, 0)
        if user32.SendMessageW(osnap, BM_GETCHECK, 0, 0) == BST_CHECKED:
            raise AssertionError("Visible ribbon command did not route WM_COMMAND")

        print("Ribbon smoke passed: 5 tabs group all 21 compact icon commands above a full-width canvas.")
    finally:
        if hwnd:
            user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.terminate(); process.wait(timeout=3)


if __name__ == "__main__":
    main()
