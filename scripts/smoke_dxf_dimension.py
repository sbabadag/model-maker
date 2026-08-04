"""Native smoke test for DXF DIMENSION generated-block display."""
import os
import subprocess
import time
from pathlib import Path

from smoke_dxf import user32, find_window, descendants, class_name, text
from smoke_view_cube import capture

WM_CLOSE = 0x0010


def main():
    root = Path(__file__).resolve().parents[1]
    exe = Path(os.environ.get("MODEL_MAKER_EXE", root / "build" / "model-maker.exe"))
    fixture = root / "build" / "dimension-display.dxf"
    fixture.write_text(
        "0\nSECTION\n2\nBLOCKS\n"
        "0\nBLOCK\n2\n*D1\n10\n0\n20\n0\n"
        "0\nLINE\n8\nDIM\n62\n2\n10\n2\n20\n3\n11\n12\n21\n3\n"
        "0\nSOLID\n8\nDIM\n62\n1\n10\n2\n20\n3\n11\n3\n21\n3.5\n12\n3\n22\n2.5\n13\n2\n23\n3\n"
        "0\nMTEXT\n8\nDIM\n62\n3\n10\n7\n20\n4\n40\n1\n71\n5\n1\n10.0\n"
        "0\nENDBLK\n0\nENDSEC\n"
        "0\nSECTION\n2\nENTITIES\n"
        "0\nDIMENSION\n2\n*D1\n8\nDIM\n10\n100\n20\n200\n11\n7\n21\n4\n70\n0\n"
        "0\nENDSEC\n0\nEOF\n",
        encoding="ascii",
    )
    process = subprocess.Popen([str(exe), str(fixture)], cwd=root)
    window = 0
    try:
        deadline = time.time() + 30
        while time.time() < deadline:
            window = find_window("ModelMakerWindow")
            if window:
                statuses = [text(child) for child in descendants(window)
                            if class_name(child).lower() == "static"]
                if any("Nesne: 3" in value for value in statuses):
                    break
            time.sleep(0.1)
        else:
            raise AssertionError("DIMENSION fixture did not load three generated display models")
        user32.ShowWindow(window, 3)
        user32.UpdateWindow(window)
        image = capture(window, root / "build" / "dxf-dimension-visible.png").convert("RGB")
        colored = sum(1 for red, green, blue in image.get_flattened_data()
                      if max(red, green, blue) > 170 and max(red, green, blue) - min(red, green, blue) > 50)
        if colored < 150:
            raise AssertionError(f"DIMENSION graphics were not visibly rendered: {colored} colored pixels")
        print(f"DXF DIMENSION smoke passed: 3 display models, {colored} colored pixels.")
    finally:
        if window and user32.IsWindow(window):
            user32.PostMessageW(window, WM_CLOSE, 0, 0)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()


if __name__ == "__main__":
    main()
