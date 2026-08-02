"""Native Win32 smoke test for radius input, Fillet preview, and commit."""
import argparse
import ctypes
import re
import subprocess
import time
from ctypes import wintypes
from pathlib import Path

from smoke_snap_style_controls import CB_SETCURSEL, WM_CHAR, capture, click, command, find_process_window, status_text, user32
from smoke_mirror_snap import send_mouse


CBN_SELCHANGE = 1
WM_MOUSEMOVE = 0x0200
WM_KEYDOWN = 0x0100
VK_F9 = 0x78
WM_MODEL_VERTEX_PROBE = 0x800A


def projected_vertex(canvas: int, model_index: int, vertex_index: int) -> tuple[int, int]:
    packed = int(user32.SendMessageW(canvas, WM_MODEL_VERTEX_PROBE, model_index, vertex_index))
    return packed & 0xFFFF, (packed >> 16) & 0xFFFF


def model_count(window: int) -> int:
    match = re.search(r"Nesne:\s*(\d+)", status_text(window))
    if not match:
        raise AssertionError(f"No model count in status: {status_text(window)!r}")
    return int(match.group(1))


def color_count(image, box, predicate) -> int:
    return sum(1 for pixel in image.crop(box).convert("RGB").get_flattened_data() if predicate(*pixel))


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
        time.sleep(0.25)
        canvas = user32.FindWindowExW(window, 0, "ModelMakerCanvas", None)
        if not canvas:
            raise RuntimeError("Canvas was not found")
        command(window, 602)
        fillet_button = user32.GetDlgItem(window, 510)
        if not fillet_button or not user32.IsWindowVisible(fillet_button):
            raise AssertionError("Fillet button is not visible on Modify tab")

        rect = wintypes.RECT()
        user32.GetClientRect(canvas, ctypes.byref(rect))
        cx, cy = rect.right // 2, rect.bottom // 2
        user32.SendMessageW(canvas, WM_KEYDOWN, VK_F9, 0)
        color = user32.GetDlgItem(window, 801)
        user32.SendMessageW(color, CB_SETCURSEL, 2, 0)
        command(window, 801, CBN_SELCHANGE)

        command(window, 601)
        command(window, 200)
        click(canvas, cx - 200, cy); click(canvas, cx + 200, cy)
        click(canvas, cx, cy + 200); click(canvas, cx, cy - 200)
        if model_count(window) != 2:
            raise AssertionError("Fillet source lines were not created")

        command(window, 602)
        command(window, 510)
        user32.SendMessageW(canvas, WM_CHAR, ord("2"), 0)
        user32.SendMessageW(canvas, WM_CHAR, 13, 0)
        if "R=2.000" not in status_text(window):
            raise AssertionError(f"Fillet radius was not accepted: {status_text(window)!r}")
        click(canvas, cx + 150, cy)
        if "İkinci çizginin" not in status_text(window):
            raise AssertionError(f"Fillet did not accept first line: {status_text(window)!r}")
        send_mouse(canvas, WM_MOUSEMOVE, cx, cy - 150)
        time.sleep(0.1)
        preview = capture(canvas, root / "build" / "fillet-preview.png")
        yellow = color_count(preview, (cx - 10, cy - 135, cx + 135, cy + 10),
                             lambda r, g, b: r > 190 and g > 150 and b < 130)
        if yellow < 60:
            raise AssertionError(f"Fillet preview was not visible ({yellow} yellow pixels)")

        click(canvas, cx, cy - 150)
        if model_count(window) != 3:
            raise AssertionError(f"Fillet must replace two lines and add one arc: {status_text(window)!r}")
        result = capture(canvas, root / "build" / "fillet-result.png")
        red = color_count(result, (cx - 8, cy - 135, cx + 135, cy + 8),
                          lambda r, g, b: r > 190 and g < 130 and b < 130)
        if red < 150:
            raise AssertionError(f"Committed tangent arc/segments were not visible ({red} red pixels)")
        if "FILLET" in status_text(window):
            raise AssertionError("Fillet must complete as a one-shot modifier")

        # Repeat through the real 3D hit-test/unprojection path on the active default XY work plane.
        command(window, 100)
        command(window, 601); command(window, 200)
        click(canvas, cx - 200, cy); click(canvas, cx + 200, cy)
        click(canvas, cx, cy + 200); click(canvas, cx, cy - 200)
        command(window, 303)  # preserve the work plane, switch to 3D
        command(window, 320)  # exact front camera looks normal to default XY
        time.sleep(0.1)
        h0 = projected_vertex(canvas, 0, 0)
        v0 = projected_vertex(canvas, 1, 0)
        first_pick = (round(cx + (cx - h0[0]) * 0.75), round(cy + (cy - h0[1]) * 0.75))
        second_pick = (round(cx + (cx - v0[0]) * 0.75), round(cy + (cy - v0[1]) * 0.75))
        command(window, 602); command(window, 510)
        for character in "1.5":
            user32.SendMessageW(canvas, WM_CHAR, ord(character), 0)
        user32.SendMessageW(canvas, WM_CHAR, 13, 0)
        click(canvas, *first_pick); click(canvas, *second_pick)
        if model_count(window) != 3 or "3B Paralel" not in status_text(window):
            raise AssertionError(f"3D/work-plane Fillet failed or reset 3D mode: {status_text(window)!r}")
        capture(canvas, root / "build" / "fillet-work-plane-3d.png")
        print(f"Fillet GUI smoke passed: 2D radius=2 preview={yellow} result={red}; "
              f"3D work-plane radius=1.5 models=3; PID={process.pid}.")
        if args.keep_open:
            keep = True
            command(window, 602)
            print("Verified maximized instance left open on the Modify tab.")
    finally:
        if not keep and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()


if __name__ == "__main__":
    main()
