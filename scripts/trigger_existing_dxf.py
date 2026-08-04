import time
from pathlib import Path
from smoke_dxf import find_window, find_owned_dialog, set_dialog_path, wait_closed, user32, BM_CLICK, CMD_TAB_FILE, CMD_IMPORT_DXF

root = Path(__file__).resolve().parents[1]
source = root / "build-debug" / "debug-import.dxf"
source.write_text(
    "0\nSECTION\n2\nENTITIES\n"
    "0\nLINE\n8\n0\n10\n0\n20\n0\n30\n0\n11\n10\n21\n0\n31\n0\n"
    "0\nCIRCLE\n8\n0\n10\n4\n20\n4\n30\n0\n40\n2\n"
    "0\nLWPOLYLINE\n8\n0\n90\n3\n70\n1\n10\n-2\n20\n-2\n10\n2\n20\n-2\n10\n0\n20\n2\n"
    "0\nENDSEC\n0\nEOF\n", encoding="ascii")
window = find_window("ModelMakerWindow")
user32.SendMessageW(user32.GetDlgItem(window, CMD_TAB_FILE), BM_CLICK, 0, 0)
user32.PostMessageW(user32.GetDlgItem(window, CMD_IMPORT_DXF), BM_CLICK, 0, 0)
dialog = find_owned_dialog(window)
set_dialog_path(dialog, source)
wait_closed(dialog)
time.sleep(1)
print(f"window_alive={bool(user32.IsWindow(window))}")
