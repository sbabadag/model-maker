#!/usr/bin/env python3
"""Kesit referans normali testlerinin GERCEKTEN yakaladigini mutasyonla kanitla.

Her mutasyon: kaynakta tek bir degisiklik -> ilgili test DUSMELI -> dosya AYNEN
geri yazilir (skill uyarisi: replace("", orig) YAZMA). Sonda tum dosyalarin
orijinal baytlarla ayni oldugu dogrulanir.
Kullanim: python3 tests/mutate_section_normal.py build-hermes
"""
from pathlib import Path
import hashlib
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build-hermes"

files = ["src/geometry.cpp", "include/model_maker/view_definition.hpp", "src/document.cpp"]
orig = {f: (root / f).read_bytes() for f in files}
digest = {f: hashlib.sha256(orig[f]).hexdigest() for f in files}

MUTATIONS = [
    # (aciklama, dosya, eski, yeni, test hedefi)
    ("M1 referans normal YOK SAYILIR (hep dunya Z)",
     "src/geometry.cpp",
     "    auto width = normalized(cross3(reference, *axis));\n",
     "    auto width = normalized(cross3(worldUp, *axis));\n",
     "model_maker_section_frame_test"),
    ("M2 gorunus duzlemi kapisi KALDIRILIR (hep UCS normali)",
     "include/model_maker/view_definition.hpp",
     "            if (d > 1.0 - 1e-9) return worldUp; // duzlem = otomatik gorunus duzlemi\n",
     "            (void)d;\n",
     "model_maker_section_normal_test"),
    ("M3 transform'da referans normal DONMEZ",
     "src/document.cpp",
     "            const Vec3 n = transformSectionNormal3(\n"
     "                linearInverse, Vec3{props.sectionNormalX, props.sectionNormalY,\n"
     "                                    props.sectionNormalZ});\n"
     "            props.sectionNormalX = n.x; props.sectionNormalY = n.y; props.sectionNormalZ = n.z;\n",
     "",
     "model_maker_section_normal_test"),
    ("M4 MMW6 alani HER ZAMAN okunur (eski dosya bozulur)",
     "src/document.cpp",
     "            if (version6 &&\n",
     "            if (true &&\n",
     "model_maker_section_normal_test"),
    ("M5 MMW6 alani YAZILMAZ (round-trip bozulur)",
     "src/document.cpp",
     "               << props.sectionNormalX << ' ' << props.sectionNormalY << ' '\n"
     "               << props.sectionNormalZ << ' ';\n",
     "               ;\n",
     "model_maker_section_normal_test"),
    ("M6 polar dizide normal donmez",
     "src/drafting.cpp",
     "            props.sectionNormalX = n.x * c - n.y * s;\n",
     "            props.sectionNormalX = n.x;\n",
     "model_maker_section_normal_test"),
]

mutation_files = sorted({m[1] for m in MUTATIONS} | set(files))
orig = {f: (root / f).read_bytes() for f in mutation_files}

results = []
try:
    for label, rel, old, new, target in MUTATIONS:
        path = root / rel
        text = orig[rel].decode("utf-8")
        assert old in text, f"mutasyon deseni bulunamadi: {rel}\n{old!r}"
        assert text.count(old) == 1, f"desen tekil degil: {rel}"
        path.write_text(text.replace(old, new), encoding="utf-8")
        buildlog = subprocess.run(["cmake", "--build", str(build), "-j", "4"],
                                  capture_output=True, text=True)
        compiled = buildlog.returncode == 0
        ran = subprocess.run([str(build / target)], capture_output=True, text=True) \
            if compiled else None
        caught = bool(compiled and ran is not None and ran.returncode != 0)
        detail = "" if compiled else "DERLENMEDI (mutasyon kusurlu olabilir)"
        if compiled and ran is not None:
            tail = [l for l in ran.stdout.splitlines() if l.startswith("FAIL")]
            detail = "; ".join(tail[:2])
        results.append((label, caught, detail))
        path.write_bytes(orig[rel])  # AYNEN geri
        subprocess.run(["cmake", "--build", str(build), "-j", "4"], capture_output=True)
        assert (root / rel).read_bytes() == orig[rel], f"geri yazma basarisiz: {rel}"
finally:
    for rel in mutation_files:
        (root / rel).write_bytes(orig[rel])

print("\n== MUTASYON SONUCLARI ==")
ok = True
for label, caught, detail in results:
    print(f"{'YAKALANDI' if caught else 'KACIRILDI !!'}  {label}  {detail}")
    ok = ok and caught

# Referans kosum: mutasyonsuz halde tum ilgili testler GECMELI.
subprocess.run(["cmake", "--build", str(build), "-j", "4"], check=True, capture_output=True)
for target in sorted({m[4] for m in MUTATIONS}):
    dash = subprocess.run([str(build / target)], capture_output=True, text=True)
    print(f"referans {target}: exit={dash.returncode}")
    ok = ok and dash.returncode == 0

for rel in mutation_files:
    assert (root / rel).read_bytes() == orig[rel], f"temizlik bozuk: {rel}"
print("temizlik: tum dosyalar orijinal baytlarla ayni ✓")
print("SONUC:", "TUM MUTASYONLAR YAKALANDI" if ok else "BOSLUK VAR")
sys.exit(0 if ok else 1)
