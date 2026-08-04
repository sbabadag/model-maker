"""Exercise the docked Layer Manager through native Win32 controls."""
import argparse
import ctypes
import subprocess
import time
from ctypes import wintypes
from pathlib import Path

from PIL import ImageGrab

from smoke_snap_style_controls import command, find_process_window, user32

user32.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, ctypes.c_size_t, ctypes.c_ssize_t]
user32.SendMessageW.restype = ctypes.c_ssize_t
user32.GetDlgItem.argtypes = [wintypes.HWND, ctypes.c_int]
user32.GetDlgItem.restype = wintypes.HWND
user32.SetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPCWSTR]
user32.SetWindowTextW.restype = wintypes.BOOL
user32.GetWindowTextLengthW.argtypes = [wintypes.HWND]
user32.GetWindowTextLengthW.restype = ctypes.c_int
user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.GetWindowTextW.restype = ctypes.c_int
user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.GetWindowRect.restype = wintypes.BOOL
user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.GetClientRect.restype = wintypes.BOOL
user32.FindWindowExW.argtypes = [wintypes.HWND, wintypes.HWND, wintypes.LPCWSTR, wintypes.LPCWSTR]
user32.FindWindowExW.restype = wintypes.HWND



WM_CLOSE = 0x0010
WM_COMMAND = 0x0111
WM_KEYDOWN = 0x0100
WM_LBUTTONDOWN = 0x0201
VK_ESCAPE = 0x1B

EN_KILLFOCUS = 0x0200
EN_CHANGE = 0x0300
CBN_SELCHANGE = 1
SW_MAXIMIZE = 3

CMD_LAYER_MANAGER = 205
CMD_LINE = 200
CMD_LAYER_NEW = 820
CMD_LAYER_DELETE = 821
CMD_LAYER_SET_CURRENT = 822
CMD_LAYER_SEARCH = 830
CMD_LAYER_LIST = 832
CMD_LAYER_CELL_EDIT = 833
WM_APP = 0x8000

LVM_FIRST = 0x1000
LVM_GETITEMCOUNT = LVM_FIRST + 4
CB_GETCURSEL = 0x0147
CB_SETCURSEL = 0x014E


def control(window: int, control_id: int) -> int:
    handle = user32.GetDlgItem(window, control_id)
    if not handle:
        raise AssertionError(f"Control {control_id} was not found")
    return handle


def list_count(list_view: int) -> int:
    return int(user32.SendMessageW(list_view, LVM_GETITEMCOUNT, 0, 0))


def control_text(handle: int) -> str:
    buffer = ctypes.create_unicode_buffer(user32.GetWindowTextLengthW(handle) + 1)
    user32.GetWindowTextW(handle, buffer, len(buffer))
    return buffer.value


def window_rect(handle: int) -> wintypes.RECT:
    result = wintypes.RECT()
    if not user32.GetWindowRect(handle, ctypes.byref(result)):
        raise AssertionError("Could not inspect control bounds")
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--keep-open", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    executable = root / "build-local" / "model-maker.exe"
    process = subprocess.Popen([str(executable)])
    window = 0
    keep = False
    try:
        window = find_process_window(process.pid)
        user32.ShowWindow(window, SW_MAXIMIZE)
        user32.SetForegroundWindow(window)
        time.sleep(0.4)

        layer_list = control(window, CMD_LAYER_LIST)
        combo = control(window, 800)
        canvas = user32.FindWindowExW(window, 0, "ModelMakerCanvas", None)
        if not canvas:
            raise AssertionError("Canvas control not found")
        ribbon_bottom = window_rect(canvas).top
        window_right = window_rect(window).right
        for control_id in (800, 801, 802):
            style_rect = window_rect(control(window, control_id))
            if style_rect.top >= ribbon_bottom:
                raise AssertionError(
                    f"Style dropdown {control_id} is not fixed in the main top panel: "
                    f"rect={style_rect.left, style_rect.top, style_rect.right, style_rect.bottom}")
        profile_rect = window_rect(control(window, 810))
        if profile_rect.right < window_right - 40:
            raise AssertionError("Style/profile dropdowns are not anchored to the right side of the main top panel")
        for tab_command in range(600, 605):
            command(window, tab_command)
            if any(not user32.IsWindowVisible(control(window, control_id)) for control_id in (800, 801, 802)):
                raise AssertionError(f"Style dropdowns disappeared on tab command {tab_command}")
            if not user32.IsWindowVisible(control(window, 810)):
                raise AssertionError(f"Profile dropdown disappeared on tab command {tab_command}")
        command(window, 601)
        initial_count = list_count(layer_list)
        layer_count = user32.SendMessageW(window, WM_APP + 20, 0, 0)
        if initial_count != 1 or layer_count != 1 or user32.SendMessageW(combo, CB_GETCURSEL, 0, 0) != 0:
            raise AssertionError(
                f"Layer Manager did not start with layer 0: rows={initial_count}, layers={layer_count}")
        print("Layer smoke: initial state ok", flush=True)

        command(window, CMD_LAYER_NEW)
        editor = control(window, CMD_LAYER_CELL_EDIT)
        if not user32.SetWindowTextW(editor, "Walls") or control_text(editor) != "Walls":
            raise AssertionError(f"Could not enter layer name: {control_text(editor)!r}")
        user32.SendMessageW(window, WM_COMMAND,
                            (EN_KILLFOCUS << 16) | CMD_LAYER_CELL_EDIT, editor)
        time.sleep(0.15)
        if user32.SendMessageW(window, WM_APP + 20, 0, 0) != 2:
            raise AssertionError("New layer was not committed")
        if user32.SendMessageW(window, WM_APP + 26, 0, 0) != 1:
            raise AssertionError("Layer rename did not persist")
        print("Layer smoke: create/rename ok", flush=True)
        command(window, CMD_LAYER_SET_CURRENT)
        if user32.SendMessageW(combo, CB_GETCURSEL, 0, 0) != 1:
            raise AssertionError("Set Current did not update the ribbon layer combo")
        print("Layer smoke: current layer ok", flush=True)

        if user32.SendMessageW(window, WM_APP + 35, 0, 0) != 1:
            raise AssertionError("Could not set the layer swatch smoke color")
        time.sleep(0.1)
        layer_capture = ImageGrab.grab(window=layer_list).convert("RGB")
        layer_capture.save(root / "build-local" / "layer-color-swatch-smoke.png")
        red_pixels = sum(1 for red, green, blue in layer_capture.get_flattened_data()
                         if red > 220 and green < 100 and blue < 100)
        if red_pixels < 30:
            raise AssertionError(
                f"Layer color cell did not paint a red swatch: red_pixels={red_pixels}")
        print("Layer smoke: color cell swatch ok", flush=True)

        if user32.SendMessageW(window, WM_APP + 24, 0, 0) != 0:
            raise AssertionError("Visibility action did not switch the current layer off")
        print("Layer smoke: visibility ok", flush=True)

        search = control(window, CMD_LAYER_SEARCH)
        user32.SendMessageW(window, WM_APP + 27, 0, 0)
        time.sleep(0.15)
        filtered_rows = list_count(layer_list)
        displayed = user32.SendMessageW(window, WM_APP + 23, 0, 0)
        matching_layers = user32.SendMessageW(window, WM_APP + 25, 0, 0)
        if filtered_rows != 1 or displayed != 1:
            raise AssertionError(
                f"Case-insensitive layer search failed: rows={filtered_rows}, displayed={displayed}, "
                f"matching_layers={matching_layers}")
        print("Layer smoke: search ok", flush=True)
        user32.SendMessageW(window, WM_APP + 28, 0, 0)
        time.sleep(0.1)

        command(window, CMD_LAYER_MANAGER)
        if user32.SendMessageW(window, WM_APP + 22, 0, 0) != 0 or user32.IsWindowVisible(layer_list):
            raise AssertionError("Layer Manager ribbon command did not close the dock")
        command(window, CMD_LAYER_MANAGER)
        if user32.SendMessageW(window, WM_APP + 22, 0, 0) != 1 or not user32.IsWindowVisible(layer_list):
            raise AssertionError("Layer Manager ribbon command did not reopen the dock")
        print("Layer smoke: dock toggle ok", flush=True)

        if user32.SendMessageW(window, WM_APP + 29, 0, 0) != 2:
            raise AssertionError("Could not prepare neutral-selection smoke geometry")
        canvas = user32.FindWindowExW(window, 0, "ModelMakerCanvas", None)
        if not canvas:
            raise AssertionError("Canvas control not found for neutral-selection smoke")
        canvas_rect = wintypes.RECT()
        if not user32.GetClientRect(canvas, ctypes.byref(canvas_rect)):
            raise AssertionError("Could not inspect canvas bounds")
        center_x = canvas_rect.right // 2
        center_y = canvas_rect.bottom // 2
        make_lparam = lambda x, y: ((y & 0xFFFF) << 16) | (x & 0xFFFF)
        user32.SendMessageW(canvas, WM_LBUTTONDOWN, 0, make_lparam(center_x, center_y))
        if user32.SendMessageW(window, WM_APP + 30, 0, 0) != 1:
            raise AssertionError("Idle single-click selection failed")
        user32.SendMessageW(canvas, WM_LBUTTONDOWN, 0, make_lparam(center_x, center_y - 120))
        if user32.SendMessageW(window, WM_APP + 30, 0, 0) != 2:
            raise AssertionError("Idle multi-selection failed")

        for control_id, selection in ((800, 1), (801, 2), (802, 2)):
            user32.SendMessageW(control(window, control_id), CB_SETCURSEL, selection, 0)
            command(window, control_id, CBN_SELCHANGE)
        if any(user32.SendMessageW(window, WM_APP + 31, property_id, 0) != 1
               for property_id in range(3)):
            raise AssertionError("Style dropdowns did not update every selected entity")
        if user32.SendMessageW(window, WM_APP + 32, 0, 0) != 1:
            raise AssertionError("Editing selected entities changed the document's current layer")
        print("Layer smoke: idle single/multi selection and property editing ok", flush=True)

        def start_modifier_with_preselection(modifier: int, selection_count: int) -> int:
            if user32.SendMessageW(window, WM_APP + 29, 0, 0) != 2:
                raise AssertionError("Could not reset preselection smoke geometry")
            user32.SendMessageW(canvas, WM_LBUTTONDOWN, 0, make_lparam(center_x, center_y))
            if selection_count == 2:
                user32.SendMessageW(canvas, WM_LBUTTONDOWN, 0, make_lparam(center_x, center_y - 120))
            if user32.SendMessageW(window, WM_APP + 30, 0, 0) != selection_count:
                raise AssertionError("Could not prepare idle selection for modifier")
            return int(user32.SendMessageW(window, WM_APP + 33, modifier, 0))

        for modifier in (1, 2, 4, 6, 7):  # Move, Copy, Mirror, Linear Array, Polar Array.
            state = start_modifier_with_preselection(modifier, 2)
            if not state & 1 or (state >> 1) & 0x3 != 1 or (state >> 8) & 0xFF != 2:
                raise AssertionError(f"Point modifier {modifier} discarded idle preselection: state={state:#x}")
        offset_state = start_modifier_with_preselection(3, 1)
        if not offset_state & 1 or (offset_state >> 1) & 0x3 != 1 or (offset_state >> 8) & 0xFF != 1:
            raise AssertionError(f"Offset discarded its idle preselection: state={offset_state:#x}")
        for modifier in (8, 9):  # Trim, Extend.
            state = start_modifier_with_preselection(modifier, 2)
            if (not state & 1 or (state >> 1) & 0x3 != 2 or
                    (state >> 8) & 0xFF != 0 or (state >> 16) & 0xFF != 2):
                raise AssertionError(f"Boundary modifier {modifier} discarded idle preselection: state={state:#x}")
        fillet_state = start_modifier_with_preselection(10, 1)
        if (not fillet_state & 1 or (fillet_state >> 1) & 0x3 != 0 or
                (fillet_state >> 8) & 0xFF != 1 or not fillet_state & (1 << 7)):
            raise AssertionError(f"Fillet discarded its first preselected entity: state={fillet_state:#x}")
        delete_state = start_modifier_with_preselection(5, 2)
        if delete_state & 1 or (delete_state >> 24) & 0xFF != 0:
            raise AssertionError(f"Delete did not consume idle preselection: state={delete_state:#x}")
        print("Layer smoke: every modifier consumes idle preselection ok", flush=True)

        command(window, CMD_LINE)
        user32.SendMessageW(canvas, WM_KEYDOWN, VK_ESCAPE, 0)
        if user32.SendMessageW(window, WM_APP + 34, 0, 0) != 1:
            raise AssertionError("Escape did not cancel a drawing command into passive mode")
        start_modifier_with_preselection(1, 2)
        user32.SendMessageW(canvas, WM_KEYDOWN, VK_ESCAPE, 0)
        if user32.SendMessageW(window, WM_APP + 34, 0, 0) != 1:
            raise AssertionError("Escape did not cancel a modify command into passive mode")
        print("Layer smoke: Escape cancels commands into passive mode ok", flush=True)

        undo_count = user32.SendMessageW(window, WM_APP + 36, 0, 0)
        if undo_count != 3:
            raise AssertionError(f"Could not prepare undo smoke geometry: model_count={undo_count}")
        if user32.SendMessageW(window, WM_APP + 37, 0, 0) != 2:
            raise AssertionError("Undo did not remove the last model")
        if user32.SendMessageW(window, WM_APP + 37, 0, 0) != 1:
            raise AssertionError("Second undo did not remove the middle model")
        if user32.SendMessageW(window, WM_APP + 38, 0, 0) != 2:
            raise AssertionError("Redo did not restore the middle model")
        if user32.SendMessageW(window, WM_APP + 38, 0, 0) != 3:
            raise AssertionError("Second redo did not restore the last model")
        print("Layer smoke: undo/redo ok", flush=True)

        if user32.SendMessageW(window, WM_APP + 39, 0, 0) != 1:
            raise AssertionError("Options file was not written or has wrong signature")
        print("Layer smoke: options save/load ok", flush=True)

        if user32.SendMessageW(window, WM_APP + 40, 0, 0) != 1:
            raise AssertionError("Rotate command via preselection + center + angle pick failed")
        print("Layer smoke: Rotate command ok", flush=True)

        if user32.SendMessageW(window, WM_APP + 41, 0, 0) != 1:
            raise AssertionError("S2K export format verification failed")
        print("Layer smoke: S2K export ok", flush=True)

        print(f"Layer Manager smoke passed: PID={process.pid}")
        if args.keep_open:
            keep = True
            print("Verified Layer Manager instance left open.")
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
