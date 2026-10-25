#!/usr/bin/env python3
"""Mutasyon kosucusu: run_workplane_ghost.py varsayilan-kapali kuralini
GERCEKTEN yakaliyor mu?

Kullanici sarti: "workplane hayalet goruntusunu simdilik disable edelim".
Hayalet duzlem varsayilan olarak KAPALI olmali ve bu varsayilan TEK
KAYNAKTAN (kWorkPlaneGhostDefaultVisible) gelmeli — iki ayri literal
zamanla ayrisirsa hayalet kendiliginden geri gelir.

Her mutasyon header'lari bozar, testin DUSMESINI bekler, sonra dosyalari
BAYT-BIREBIR geri yazar. Sonda disk == orijinal dogrulanir
(`replace("", orig)` tuzagina dusmemek icin icerik bellekte tutulur).

Kullanim: python3 tests/mutate_workplane_ghost_default.py [build-directory]
"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"

FILES = {
    "renderer.hpp": root / "include/model_maker/renderer.hpp",
    "application.hpp": root / "include/model_maker/application.hpp",
}
ORIG = {name: path.read_text(encoding="utf-8") for name, path in FILES.items()}

PROBE = "kWorkPlaneGhostDefaultVisible"
DRAFT_FIELD = "bool workPlaneGhostVisible{kWorkPlaneGhostDefaultVisible};"
APP_FIELD = "bool workPlaneGhostVisible_{kWorkPlaneGhostDefaultVisible};"

# (etiket, dosya, eski, yeni)
MUTATIONS = [
    ("M1 kill-switch true (hayalet geri acilir)", "renderer.hpp",
     f"{PROBE} = false", f"{PROBE} = true"),
    ("M2 DraftView alani literal true (varsayilanlar ayrisir)", "renderer.hpp",
     DRAFT_FIELD, "bool workPlaneGhostVisible{true};"),
    ("M3 Application bayragi literal true", "application.hpp",
     APP_FIELD, "bool workPlaneGhostVisible_{true};"),
    ("M4 tek kaynak kaldirildi (her iki yer literal false)", "renderer.hpp",
     DRAFT_FIELD, "/* literal */ bool workPlaneGhostVisible{false};"),
]


def restore():
    for name, path in FILES.items():
        path.write_text(ORIG[name], encoding="utf-8")


def run_test():
    return subprocess.run([sys.executable,
                           str(root / "tests/run_workplane_ghost.py"),
                           str(build)], capture_output=True).returncode


caught = 0
for label, fname, old, new in MUTATIONS:
    if ORIG[fname].count(old) != 1:
        print(f"SETUP HATASI: parca bulunamadi/coklu -> {label}")
        restore()
        raise SystemExit(1)
    try:
        FILES[fname].write_text(ORIG[fname].replace(old, new, 1), encoding="utf-8")
        rc = run_test()
    finally:
        restore()
    if rc != 0:
        caught += 1
        print(f"  YAKALANDI  {label}")
    else:
        print(f"  KACIRILDI! {label}")

# Referans: mutasyonsuz halde test GECMELI
ref = run_test()
disk_ok = all(FILES[name].read_text(encoding="utf-8") == ORIG[name] for name in FILES)
print(f"mutasyon: {caught}/{len(MUTATIONS)} yakalandi; "
      f"referans {'GECTI' if ref == 0 else 'DUSTU'}; dosyalar geri yazildi={disk_ok}")
raise SystemExit(0 if (caught == len(MUTATIONS) and ref == 0 and disk_ok) else 1)
