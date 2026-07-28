"""Verify DXF layer/entity color, lineweight, and linetype are visibly rendered."""
import subprocess
import time
from pathlib import Path
from smoke_dxf import user32, find_window, descendants, class_name, text
from smoke_view_cube import capture

WM_CLOSE = 0x0010


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    fixture = root / "build" / "dxf-properties-render.dxf"
    fixture.write_text(
        "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n"
        "0\nLAYER\n2\nHEAVY_RED\n70\n0\n62\n1\n6\nCONTINUOUS\n370\n100\n"
        "0\nENDTAB\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"
        "0\nLINE\n8\nHEAVY_RED\n10\n0\n20\n0\n11\n20\n21\n0\n"
        "0\nLINE\n8\nDETAIL\n420\n65280\n370\n25\n6\nDASHED\n10\n0\n20\n4\n11\n20\n21\n4\n"
        "0\nENDSEC\n0\nEOF\n", encoding="ascii")
    process = subprocess.Popen([str(root / "build" / "model-maker.exe"), str(fixture)])
    window = 0
    try:
        window = find_window("ModelMakerWindow")
        user32.ShowWindow(window, 3)
        deadline = time.time() + 10
        while time.time() < deadline:
            statuses = [text(child) for child in descendants(window) if class_name(child).lower() == "static"]
            if any("Nesne: 2" in value for value in statuses): break
            time.sleep(0.02)
        else: raise AssertionError("Styled DXF did not load")
        canvas = user32.FindWindowExW(window, 0, "ModelMakerCanvas", None)
        image = capture(canvas, root / "build" / "dxf-properties-render.png").convert("RGB")
        red = [(x, y) for y in range(image.height) for x in range(image.width)
               if (lambda c: c[0] > 220 and c[1] < 40 and c[2] < 40)(image.getpixel((x, y)))]
        green = [(x, y) for y in range(image.height) for x in range(image.width)
                 if (lambda c: c[1] > 220 and c[0] < 40 and c[2] < 80)(image.getpixel((x, y)))]
        if len(red) < 1000 or len(green) < 300:
            raise AssertionError(f"DXF colors were not rendered: red={len(red)} green={len(green)}")
        red_rows = len({y for _, y in red}); green_rows = len({y for _, y in green})
        if red_rows <= green_rows:
            raise AssertionError(f"DXF lineweight was not rendered: red_rows={red_rows}, green_rows={green_rows}")
        green_by_row = {}
        for x, y in green: green_by_row.setdefault(y, []).append(x)
        xs = max(green_by_row.values(), key=len)
        gaps = sum(1 for x in range(min(xs), max(xs) + 1) if x not in set(xs))
        if gaps < 20:
            raise AssertionError("DXF DASHED linetype was rendered as continuous")
        print(f"DXF property rendering passed: red={len(red)}, green={len(green)}, widths={red_rows}/{green_rows}, gaps={gaps}.")
    finally:
        if window and user32.IsWindow(window): user32.PostMessageW(window, WM_CLOSE, 0, 0)
        try: process.wait(timeout=5)
        except subprocess.TimeoutExpired: process.terminate(); process.wait(timeout=5)


if __name__ == "__main__": main()
