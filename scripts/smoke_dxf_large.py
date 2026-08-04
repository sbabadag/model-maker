"""Load the real 739 MB DXF, verify native progress and visible 3D geometry."""
import ctypes
import subprocess
import time
from pathlib import Path
from smoke_dxf import user32, find_window, descendants, class_name, text
from smoke_view_cube import capture

SW_MAXIMIZE = 3
PBM_GETPOS = 0x0408
WM_CLOSE = 0x0010
DXF_PROGRESS_ID = 700


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    source = Path(r"C:\Users\Asus\Documents\MEGA\Mustafa_Hatipoglu\Balik_tesisi_seydisehir\BORU_ILK_BINA\makina_dairesi_borular.dxf")
    if not source.exists():
        raise RuntimeError(f"Large DXF fixture is missing: {source}")
    process = subprocess.Popen([str(root / "build" / "model-maker.exe"), str(source)])
    window = 0
    try:
        window = find_window("ModelMakerWindow")
        user32.ShowWindow(window, SW_MAXIMIZE)
        progress = user32.GetDlgItem(window, DXF_PROGRESS_ID)
        if not progress:
            raise AssertionError("Native DXF progress bar was not created")
        saw_visible = False
        saw_progress = False
        progress_capture = None
        deadline = time.time() + 30
        while time.time() < deadline and process.poll() is None:
            visible = bool(user32.IsWindowVisible(progress))
            position = int(user32.SendMessageW(progress, PBM_GETPOS, 0, 0))
            saw_visible = saw_visible or visible
            saw_progress = saw_progress or position > 0
            if visible and position > 0 and progress_capture is None:
                progress_capture = capture(window, root / "build" / "dxf-progress.png")
            statuses = [text(child) for child in descendants(window) if class_name(child).lower() == "static"]
            if any("Nesne: 23777" in value for value in statuses):
                break
            time.sleep(0.02)
        else:
            raise AssertionError("Large DXF did not finish within 30 seconds")
        if not saw_visible or not saw_progress:
            raise AssertionError(f"Progress bar feedback missing: visible={saw_visible}, advanced={saw_progress}")
        if user32.IsWindowVisible(progress):
            raise AssertionError("Progress bar remained visible after import completed")

        canvas = user32.FindWindowExW(window, 0, "ModelMakerCanvas", None)
        time.sleep(1.0)
        image = capture(canvas, root / "build" / "dxf-large-visible.png").convert("RGB")
        cyan = sum(1 for r, g, b in image.get_flattened_data() if b > 180 and g > 130 and r < 170)
        if cyan < 1000:
            raise AssertionError(f"Loaded DXF did not visibly render ({cyan} model-colored pixels)")
        statuses = [text(child) for child in descendants(window) if class_name(child).lower() == "static"]
        if not any("3B Paralel" in value and "Nesne: 23777" in value for value in statuses):
            raise AssertionError(f"Non-planar DXF did not open in fitted 3D mode: {statuses}")
        print(f"Large DXF smoke passed: progress advanced, 23777 models loaded, {cyan} visible geometry pixels.")
    finally:
        if window and user32.IsWindow(window):
            user32.PostMessageW(window, WM_CLOSE, 0, 0)
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.terminate(); process.wait(timeout=5)


if __name__ == "__main__":
    main()
