#!/usr/bin/env python3
"""Mutasyon kosucusu: model_maker_grid_snap_types_test GERCEKTEN yakaliyor mu?

KULLANICI SARTI: "ayrica grid e tum snaplar yapismali" + (sonradan) "XZ YZ gibi
2 noktayla alinan goruntulerde grid sadece xy deki alt grid noktalarina snap
oluyor; her grid snap ozelligine kilitlenmeli".

Kok neden (olculdu): snap `grid.axes` (PLAN tabani; tum plan cizgileri
z=origin.z'de) kullaniyordu, cizim ise `gridAxesForView`. Artik snap de cizimle
AYNI gorunus duzlemini kullanir ve kesisim GERCEK cozulur (lineCrossing).

Her mutasyon src/drafting.cpp'yi bozar, testi yeniden derler, testin
DUSMESINI bekler, sonra dosyayi BAYT-BIREBIR geri yazar.

Kullanim: python3 tests/mutate_grid_snap_types.py [build-directory]
"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"

TARGET = "model_maker_grid_snap_types_test"
REL = "src/drafting.cpp"

# --- 2B cagri blogu (tek kopya) ---
CALL_2D = (
    "        std::vector<Candidate> gridCandidates;\n"
    "        addGridLineCandidates(gridCandidates, document, cursor, objectTolerance,\n"
    "                              [&](const Vec3& point) { return distance2D(cursor, point); },\n"
    "                              referencePoint, nullptr);\n"
    "        auto gridResult = choose(std::move(gridCandidates), cursor, enabledTypes);\n"
    "        if (gridResult.type != SnapType::None) return gridResult;\n")

# --- 2B maske kapisi (tek kopya) ---
CALL_2D_MASK = "        auto gridResult = choose(std::move(gridCandidates), cursor, enabledTypes);\n"
CALL_2D_NOMASK = "        auto gridResult = choose(std::move(gridCandidates), cursor, nullptr);\n"

# --- 3B aday uretimi (tek kopya; gridPlane parametresi ile) ---
CALL_3D = (
    "        const ScreenSpaceNearest gridNearest{&camera, viewportWidth, viewportHeight, screenCursor, false};\n"
    "        std::vector<Candidate> gridCandidates;\n"
    "        addGridLineCandidates(gridCandidates, document, *raw, objectTolerancePixels, gridMetric,\n"
    "                              referencePoint, &gridNearest, gridPlane);\n"
    "        auto gridResult = choose(std::move(gridCandidates), *raw, enabledTypes, &camera);\n"
    "        if (gridResult.type != SnapType::None) return gridResult;\n")

ENDPOINTS = (
    "            addCandidate(candidates, a, SnapType::Endpoint, tolerance, metric);\n"
    "            addCandidate(candidates, b, SnapType::Endpoint, tolerance, metric);\n")

MIDPOINT = ("            addCandidate(candidates, (a + b) * 0.5, SnapType::Midpoint, tolerance, metric);\n"
            "            double rawT;\n")

NEAREST = (
    "            const double clampedT = std::clamp(rawT, 0.0, 1.0);\n"
    "            addCandidate(candidates, a + direction * clampedT, SnapType::Nearest, tolerance, metric);\n"
    "            if (referencePoint) {\n")

PERP = (
    "            if (referencePoint) {\n"
    "                const double perpendicularT = dot3(*referencePoint - a, direction) / lengthSquared;\n"
    "                if (perpendicularT >= 0.0 && perpendicularT <= 1.0)\n"
    "                    addCandidate(candidates, a + direction * perpendicularT,\n"
    "                                 SnapType::Perpendicular, tolerance, metric);\n"
    "            }\n")

# --- gorunmez grid kapisi (aday uretimindeki kopya; takip satiri ayirt eder) ---
VISIBLE_GUARD = ("        if (!grid.visible) continue;\n"
                 "        // GORUNUS DUZLEMI: cizimle")
VISIBLE_GUARD_OFF = "        // GORUNUS DUZLEMI: cizimle"

# --- kesisim (capraz) aday uretimi (gorunus duzlemi + GERCEK kesisim) ---
GRID_CROSSING = (
    "        std::vector<const GridAxisLine*> verticalAxes, levelAxes;\n"
    "        for (const auto& axis : viewAxes)\n"
    "            (axis.horizontal ? levelAxes : verticalAxes).push_back(&axis);\n"
    "        for (const auto* va : verticalAxes) {\n"
    "            for (const auto* la : levelAxes) {\n"
    "                Vec3 intersection{};\n"
    "                if (!lineCrossing(*va, *la, intersection)) continue;\n"
    "                addCandidate(candidates, intersection, SnapType::Grid, tolerance, metric);\n"
    "            }\n"
    "        }\n")

# --- KULLANICI HATASI regresyonu: snap taban PLAN turevine geri doner ---
VIEW_AXES_VIEW = "        const std::vector<GridAxisLine> viewAxes = gridAxesForView(grid, plane);\n"
VIEW_AXES_BASE = "        const std::vector<GridAxisLine> viewAxes = grid.axes;\n"

# --- 3B gorunus duzlemi daima XY'ye zorlanir ---
GRID_PLANE_STMT = "    const GridViewPlane gridPlane = gridViewPlaneForDirection(camera.gridViewDirection());\n"
GRID_PLANE_XY = "    const GridViewPlane gridPlane = GridViewPlane::XY;\n"

# --- kesisim eski PLAN formulune doner (elevasyonda z'yi tabana ceker) ---
# NOT: ondeki `Vec3 intersection{};` satiri deseni TEKILLESTIRIR — 3B
# tolerans-disi blok ayni `if (!lineCrossing...)` satirini 4 bosluk daha
# girintili tasir ve tek basina bu satir iki yerde eslesirdi.
CROSSING_CALL = ("                Vec3 intersection{};\n"
                "                if (!lineCrossing(*va, *la, intersection)) continue;\n")
CROSSING_OLD = ("                Vec3 intersection{};\n"
               "                intersection = Vec3{va->from.x, la->from.y, va->from.z};\n")

# --- 3B tolerans-disi kesisim blogu taban turevine doner ---
FALLBACK_VIEW_AXES = "            const std::vector<GridAxisLine> viewAxes = gridAxesForView(grid, gridPlane);\n"
FALLBACK_BASE_AXES = "            const std::vector<GridAxisLine> viewAxes = grid.axes;\n"

MUTATIONS = [
    ("M1 2B: grid aks adaylari hic uretilmez", CALL_2D, ""),
    ("M2 2B: maske kapisi yok sayilir (kapatilan tur yine uretilir)", CALL_2D_MASK, CALL_2D_NOMASK),
    ("M3 3B: grid aks adaylari hic uretilmez", CALL_3D, ""),
    ("M4 aks UCLARI aday degil", ENDPOINTS, ""),
    ("M5 aks ORTA NOKTASI aday degil", MIDPOINT, "            double rawT;\n"),
    ("M6 aks uzerinde EN YAKIN NOKTA aday degil", NEAREST,
     "            const double clampedT = std::clamp(rawT, 0.0, 1.0); (void)clampedT;\n"
     "            if (referencePoint) {\n"),
    ("M7 DIKME ayagi aday degil", PERP, ""),
    ("M8 gorunmez grid yok sayilir", VISIBLE_GUARD, VISIBLE_GUARD_OFF),
    ("M9 kesisim (capraz) adayi uretilmez", GRID_CROSSING, ""),
    ("M10 KULLANICI HATASI: snap taban PLAN turevine doner (elevasyonda z=0)",
     VIEW_AXES_VIEW, VIEW_AXES_BASE),
    ("M11 3B gorunus duzlemi daima XY'ye zorlanir", GRID_PLANE_STMT, GRID_PLANE_XY),
    ("M12 kesisim eski PLAN formulune doner (z'yi taban kotuna ceker)",
     CROSSING_CALL, CROSSING_OLD),
    ("M13 3B tolerans-disi kesisim blogu taban turevine doner",
     FALLBACK_VIEW_AXES, FALLBACK_BASE_AXES),
]

orig = (root / REL).read_bytes()
text0 = orig.decode("utf-8")


def build_all():
    return subprocess.run(["cmake", "--build", str(build), "-j", "4"],
                          capture_output=True, text=True)


def run_target():
    return subprocess.run([str(build / TARGET)], capture_output=True, text=True)


results = []
try:
    for label, old, new in MUTATIONS:
        assert text0.count(old) == 1, f"desen tekil degil/bulunamadi: {label}\n{old!r}"
        (root / REL).write_text(text0.replace(old, new, 1), encoding="utf-8")
        log = build_all()
        compiled = log.returncode == 0
        ran = run_target() if compiled else None
        caught = bool(compiled and ran is not None and ran.returncode != 0)
        detail = "" if compiled else "DERLENMEDI (mutasyon kusurlu)"
        if compiled and ran is not None:
            detail = "; ".join(l for l in ran.stdout.splitlines() if l.startswith("FAIL"))[:140]
        results.append((label, caught, detail))
        (root / REL).write_bytes(orig)
        build_all()
        assert (root / REL).read_bytes() == orig, f"geri yazma bozuk: {REL}"
finally:
    (root / REL).write_bytes(orig)
    build_all()

print("\n== MUTASYON SONUCLARI ==")
ok = True
for label, caught, detail in results:
    print(f"{'YAKALANDI' if caught else 'KACIRILDI !!'}  {label}  {detail}")
    ok = ok and caught

ref = run_target()
print(f"referans {TARGET}: exit={ref.returncode}")
ok = ok and ref.returncode == 0

assert (root / REL).read_bytes() == orig, "temizlik bozuk"
print("temizlik: dosya orijinal baytlarla ayni ✓")
print("SONUC:", "TUM MUTASYONLAR YAKALANDI" if ok else "BOSLUK VAR")
sys.exit(0 if ok else 1)
