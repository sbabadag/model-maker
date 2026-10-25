#!/usr/bin/env python3
"""Plan/gorunus-duzlemi pencerelerinde PROFIL UC TUTAMAKLARI.

Kullanici sikayeti: "plan goruntulerinde profil uzerindeki tasima noktalari
gozukmuyor". Kok neden: tutamak blogu `!draft.viewSlab` istiyordu; plan
(Z kotu) ve iki noktali gorunus pencereleri bir gorunus duzlemi (viewSlab)
tasiyor, bu yuzden tutamaklar ORADA hic cizilmiyordu.

Kural: tutamaklar View3D'de viewSlab'den BAGIMSIZ cizilir; ANCAK dilim
disindaki (o pencerede gorunmeyen) secili katilar icin cizilmez.

Katman: kaynak sekli (kural gercekten kodda mi). Native GDI cizimi test
edilmez. Kullanim: python3 tests/run_grip_slab_views.py
"""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
source = (root / "src/renderer.cpp").read_text()

failures = []


def check(ok, label):
    print(("PASS " if ok else "FAIL ") + label)
    if not ok:
        failures.append(label)


marker = "// PROFIL UC TUTAMAKLARI (grip edit)"
start = source.index(marker)
# Blok sonu: tutamaklar cizildikten sonraki snapshot/blit blogu.
end = source.index("// Snapshot vurgudan SONRA", start)
block = source[start:end]
# Yorumlari ayikla: kural KODDA aranir (yorumda eski kosuldan soz edilebilir).
code_block = "\n".join(re.sub(r"//.*", "", line) for line in block.splitlines())

# 1) Tutamak blogu gorunus duzlemi pencerelerinde de calisir: `!draft.viewSlab`
#    kosulu KALMAMALI.
check("!draft.viewSlab" not in code_block,
      "grip block no longer requires the ABSENCE of a view slab (plan/2-point views)")
check(re.search(r"if \(mode == EditMode::View3D && !draft\.snapOnly\)", code_block) is not None,
      "grips drawn for View3D whenever not snap-only")

# 2) Gorunus dilimi DISINDAKI secili katilar atlanir (o pencerede gorunmezler).
check("draft.viewSlab &&" in block,
      "grip block guards models by the view slab when one is set")
check("boundsInViewSlab(gripBounds[gi], *draft.viewSlab)" in block,
      "slab guard uses boundsInViewSlab (single source with the rest of the renderer)")
check("document.modelBounds()" in block, "slab guard uses model bounds")

# 3) Mevcut niyet korunur: yalniz profilli KATILAR ve secili olanlar.
check("gm.faces().empty() || gm.properties().profileName.empty()" in block,
      "grips still only for solid profile members")
check("draft.selectedModels" in block, "grips still only for the current selection")
check(block.count("drawGripMarker(") == 2, "both ends drawn (from + to)")

# 4) Dilim kontrolu TUTAMAK blogunda olmali; baska bir yerde gizlenmis olmamali.
check(source.count("boundsInViewSlab(") >= 4,
      "slab filtering stays a shared renderer concern (not a special case)")

if failures:
    print(f"Grip slab views: {len(failures)} source-shape check(s) failed")
    sys.exit(1)
print("Grip slab views: source shape verified")
