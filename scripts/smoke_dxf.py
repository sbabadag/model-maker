"""Native GUI smoke test for DXF import/export through the File ribbon dialogs."""
import ctypes
import subprocess
import time
from ctypes import wintypes
from pathlib import Path

from smoke_view_cube import capture

user32 = ctypes.windll.user32
user32.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
user32.FindWindowW.restype = wintypes.HWND
user32.FindWindowExW.argtypes = [wintypes.HWND, wintypes.HWND, wintypes.LPCWSTR, wintypes.LPCWSTR]
user32.FindWindowExW.restype = wintypes.HWND
user32.GetDlgItem.argtypes = [wintypes.HWND, ctypes.c_int]
user32.GetDlgItem.restype = wintypes.HWND
user32.GetWindow.argtypes = [wintypes.HWND, ctypes.c_uint]
user32.GetWindow.restype = wintypes.HWND
user32.SendMessageW.argtypes = [wintypes.HWND, ctypes.c_uint, wintypes.WPARAM, wintypes.LPARAM]
user32.SendMessageW.restype = wintypes.LPARAM
user32.PostMessageW.argtypes = [wintypes.HWND, ctypes.c_uint, wintypes.WPARAM, wintypes.LPARAM]
user32.PostMessageW.restype = wintypes.BOOL
WM_CLOSE = 0x0010
WM_SETTEXT = 0x000C
BM_CLICK = 0x00F5
SW_MAXIMIZE = 3
GW_OWNER = 4
IDOK = 1
CMD_TAB_FILE = 600
CMD_IMPORT_DXF = 103
CMD_EXPORT_DXF = 104


def find_window(class_name: str, timeout: float = 5.0) -> int:
    deadline = time.time() + timeout
    while time.time() < deadline:
        hwnd = user32.FindWindowW(class_name, None)
        if hwnd:
            return hwnd
        time.sleep(0.05)
    raise RuntimeError(f"Window {class_name} was not found")


def find_owned_dialog(owner: int, timeout: float = 5.0) -> int:
    deadline = time.time() + timeout
    while time.time() < deadline:
        result = []
        @ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
        def collect(hwnd: int, _param: int) -> bool:
            name = ctypes.create_unicode_buffer(64)
            user32.GetClassNameW(hwnd, name, 64)
            if name.value == "#32770" and user32.GetWindow(hwnd, GW_OWNER) == owner:
                result.append(hwnd)
            return True
        user32.EnumWindows(collect, 0)
        if result:
            return result[0]
        time.sleep(0.05)
    raise RuntimeError("DXF file dialog was not found")


def descendants(parent: int) -> list[int]:
    result = []
    @ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
    def collect(hwnd: int, _param: int) -> bool:
        result.append(hwnd)
        return True
    user32.EnumChildWindows(parent, collect, 0)
    return result


def class_name(hwnd: int) -> str:
    value = ctypes.create_unicode_buffer(64)
    user32.GetClassNameW(hwnd, value, 64)
    return value.value


def set_dialog_path(dialog: int, path: Path) -> None:
    edits = [child for child in descendants(dialog) if class_name(child) == "Edit"]
    if not edits:
        details = [(class_name(child), text(child)) for child in descendants(dialog)]
        raise AssertionError(f"Common file dialog has no visible filename edit: {details}")
    filename = ctypes.c_wchar_p(str(path))
    user32.SendMessageW(edits[-1], WM_SETTEXT, 0, ctypes.cast(filename, ctypes.c_void_p).value)
    user32.SendMessageW(user32.GetDlgItem(dialog, IDOK), BM_CLICK, 0, 0)


def wait_closed(hwnd: int, timeout: float = 5.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline and user32.IsWindow(hwnd):
        time.sleep(0.05)
    if user32.IsWindow(hwnd):
        raise AssertionError("File dialog did not close")


def text(hwnd: int) -> str:
    length = user32.GetWindowTextLengthW(hwnd)
    value = ctypes.create_unicode_buffer(length + 1)
    user32.GetWindowTextW(hwnd, value, len(value))
    return value.value


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    source = root / "build" / "gui-import.dxf"
    exported = root / "build" / "gui-export.dxf"
    exported.unlink(missing_ok=True)
    source.write_text(
        "0\nSECTION\n2\nENTITIES\n"
        "0\nLINE\n8\n0\n10\n0\n20\n0\n30\n0\n11\n10\n21\n0\n31\n0\n"
        "0\nCIRCLE\n8\n0\n10\n4\n20\n4\n30\n0\n40\n2\n"
        "0\nLWPOLYLINE\n8\n0\n90\n3\n70\n1\n10\n-2\n20\n-2\n10\n2\n20\n-2\n10\n0\n20\n2\n"
        "0\nENDSEC\n0\nEOF\n", encoding="ascii")

    process = subprocess.Popen([str(root / "build" / "model-maker.exe")])
    window = 0
    try:
        window = find_window("ModelMakerWindow")
        user32.ShowWindow(window, SW_MAXIMIZE)
        user32.SendMessageW(user32.GetDlgItem(window, CMD_TAB_FILE), BM_CLICK, 0, 0)

        user32.PostMessageW(user32.GetDlgItem(window, CMD_IMPORT_DXF), BM_CLICK, 0, 0)
        dialog = find_owned_dialog(window)
        set_dialog_path(dialog, source)
        wait_closed(dialog)
        deadline = time.time() + 15.0
        status_texts = []
        while time.time() < deadline and user32.IsWindow(window):
            all_children = descendants(window)
            status_texts = [text(child) for child in all_children if class_name(child).lower() == "static"]
            if any("Nesne: 3" in value for value in status_texts):
                break
            time.sleep(0.05)
        if not any("Nesne: 3" in value for value in status_texts):
            details = [(class_name(child), text(child)) for child in descendants(window)] if user32.IsWindow(window) else []
            raise AssertionError(f"DXF import did not load three entities; alive={user32.IsWindow(window)}, poll={process.poll()}: {details}")
        canvas = user32.FindWindowExW(window, 0, "ModelMakerCanvas", None)
        image = capture(canvas, root / "build" / "dxf-imported.png").convert("RGB")
        cyan = sum(1 for r, g, b in image.get_flattened_data() if b > 210 and g > 150 and r < 150)
        if cyan < 300:
            raise AssertionError(f"Imported DXF geometry was not visibly rendered ({cyan} cyan pixels)")

        user32.SendMessageW(user32.GetDlgItem(window, CMD_TAB_FILE), BM_CLICK, 0, 0)
        user32.PostMessageW(user32.GetDlgItem(window, CMD_EXPORT_DXF), BM_CLICK, 0, 0)
        dialog = find_owned_dialog(window)
        set_dialog_path(dialog, exported)
        wait_closed(dialog)
        time.sleep(0.2)
        if not exported.exists():
            raise AssertionError("DXF export did not create a file")
        content = exported.read_text(encoding="ascii")
        if "\nCIRCLE\n" not in content or "\nLWPOLYLINE\n" not in content or not content.endswith("0\nEOF\n"):
            raise AssertionError("Exported DXF is missing expected entities or EOF")

        print(f"DXF GUI smoke passed: imported/rendered 3 entities and exported {exported.stat().st_size} bytes.")
    finally:
        if window:
            user32.PostMessageW(window, WM_CLOSE, 0, 0)
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.terminate(); process.wait(timeout=3)
        source.unlink(missing_ok=True)
        exported.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
