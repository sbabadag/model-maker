#!/usr/bin/env python3
"""Mutasyon kosucusu: run_grip_slab_views.py kurali GERCEKTEN yakaliyor mu?

Mutasyonlar renderer.cpp'deki tutamak blogunu bozar; testin DUSMESINI bekler;
sonra dosyayi BAYT-BIREBIR geri yazar.

Kullanim: python3 tests/mutate_grip_slab_views.py
"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
REL = "src/renderer.cpp"

NEW_COND = "        if (mode == EditMode::View3D && !draft.snapOnly) {\n"
OLD_COND = "        if (mode == EditMode::View3D && !draft.snapOnly && !draft.viewSlab) {\n"

SLAB_GUARD = ("                if (draft.viewSlab && (gi >= gripBounds.size() ||\n"
              "                                       !boundsInViewSlab(gripBounds[gi], "
              "*draft.viewSlab)))\n"
              "                    continue;\n")
NO_GUARD = ""

MUTATIONS = [
    ("M1 eski `!draft.viewSlab` kosulu geri geldi (plan penceresinde gizlenir)",
     NEW_COND, OLD_COND),
    ("M2 dilim korumasi kaldirildi (dilim disi tutamaklar sizar)",
     SLAB_GUARD, NO_GUARD),
    ("M3 dilim korumasi yanlis kaynaga baglandi",
     SLAB_GUARD,
     "                if (false && (gi >= gripBounds.size() ||\n"
     "                              !boundsInViewSlab(gripBounds[gi], *draft.viewSlab)))\n"
     "                    continue;\n"),
]

orig = (root / REL).read_bytes()
text0 = orig.decode("utf-8")

results = []
try:
    for label, old, new in MUTATIONS:
        assert text0.count(old) == 1, f"desen tekil degil/bulunamadi: {label}"
        (root / REL).write_text(text0.replace(old, new, 1), encoding="utf-8")
        rc = subprocess.run([sys.executable, str(root / "tests/run_grip_slab_views.py")],
                            capture_output=True).returncode
        results.append((label, rc != 0))
        (root / REL).write_bytes(orig)
        assert (root / REL).read_bytes() == orig, "geri yazma bozuk"
finally:
    (root / REL).write_bytes(orig)

print("\n== MUTASYON SONUCLARI ==")
ok = True
for label, caught in results:
    print(f"{'YAKALANDI' if caught else 'KACIRILDI !!'}  {label}")
    ok = ok and caught

ref = subprocess.run([sys.executable, str(root / "tests/run_grip_slab_views.py")],
                     capture_output=True)
print(f"referans: exit={ref.returncode}")
ok = ok and ref.returncode == 0
assert (root / REL).read_bytes() == orig, "temizlik bozuk"
print("temizlik: dosya orijinal baytlarla ayni ✓")
print("SONUC:", "TUM MUTASYONLAR YAKALANDI" if ok else "BOSLUK VAR")
sys.exit(0 if ok else 1)
