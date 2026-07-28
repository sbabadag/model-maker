"""Load the real DXF and report the completed imported model count."""
import re
import subprocess
import time
from pathlib import Path
from smoke_dxf import user32, find_window, descendants, class_name, text

WM_CLOSE = 0x0010

root = Path(__file__).resolve().parents[1]
source = Path(r"C:\Users\Asus\Documents\MEGA\Mustafa_Hatipoglu\Balik_tesisi_seydisehir\BORU_ILK_BINA\makina_dairesi_borular.dxf")
process = subprocess.Popen([str(root / "build" / "model-maker.exe"), str(source)])
window = find_window("ModelMakerWindow")
try:
    deadline = time.time() + 120
    while time.time() < deadline:
        statuses = [text(child) for child in descendants(window) if class_name(child).lower() == "static"]
        for status in statuses:
            match = re.search(r"Nesne:\s*(\d+)", status)
            if match and int(match.group(1)) > 0:
                print(f"models={match.group(1)} status={status.strip()}")
                raise SystemExit(0)
        if process.poll() is not None:
            raise RuntimeError(f"Application exited with {process.returncode}")
        time.sleep(0.05)
    raise RuntimeError("DXF did not complete within 120 seconds")
finally:
    if user32.IsWindow(window):
        user32.PostMessageW(window, WM_CLOSE, 0, 0)
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
