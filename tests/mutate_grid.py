#!/usr/bin/env python3
"""TEKLA-TARZI YAPI GRIDI testlerinin GERCEKTEN yakaladigini mutasyonla kanitla.

Kullanici sarti: "grid objemiz birebir Tekla'nin calisma sistemiyle ayni
ayarlansin". Bu betik, `tests/grid_test.cpp`'deki iddialarin her birini
tek tek bozar ve testin DUSTUGUNU dogrular; sonra dosyalari BAYT-BIREBIR
geri yazar (skill uyarisi: `replace("", orig)` YAZMA — Python bos string'i
her karakter arasina ekler ve dosyayi bozar).

Kullanim: python3 tests/mutate_grid.py [build-dizini]
"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = sys.argv[1] if len(sys.argv) > 1 else "build-hermes"

FILES = {
    "src/grid.cpp": root / "src/grid.cpp",
    "src/document.cpp": root / "src/document.cpp",
}
ORIG = {name: path.read_text(encoding="utf-8") for name, path in FILES.items()}

# (etiket, dosya, eski, yeni)
MUTATIONS = [
    ("M1 kumulatif parse kaldirildi (X/Y artimsal DEGIL)",
     "src/grid.cpp",
     "    if (!cumulative) return raw;",
     "    if (!cumulative || true) return raw;"),

    ("M2 'N*deger' tekrari destegi kaldirildi",
     "src/grid.cpp",
     "        if (star != std::string::npos && star > 0 && star + 1 < tok.size()) {",
     "        if (false && star != std::string::npos && star > 0 && star + 1 < tok.size()) {"),

    ("M3 uzanti IKI YONDE ayni (Left/Below - Right/Above ayriligi bozuldu)",
     "src/grid.cpp",
     "        line.to = Vec3{x, o.y + spanY + above, o.z};",
     "        line.to = Vec3{x, o.y + spanY + below, o.z};"),

    ("M4 X etiketleri yanlis eksene baglandi (X kutusu -> Y cizgileri)",
     "src/grid.cpp",
     "        line.label = (i < grid.xLabels.size()) ? grid.xLabels[i] : std::string{};\n"
     "        grid.axes.push_back(std::move(line));\n"
     "    }\n"
     "    // Y konumlari",
     "        line.label = (i < grid.yLabels.size()) ? grid.yLabels[i] : std::string{};\n"
     "        grid.axes.push_back(std::move(line));\n"
     "    }\n"
     "    // Y konumlari"),

    ("M5 origin offset yok sayildi",
     "src/grid.cpp",
     "    const Vec3 o = grid.origin;",
     "    const Vec3 o{};"),

    ("M6 etiket tamamlama BOS kutuyu da doldurur (Tekla: bos kalabilir)",
     "src/grid.cpp",
     "    if (!grid.xLabels.empty())\n"
     "        for (std::size_t i = grid.xLabels.size(); i < grid.xCoords.size(); ++i)\n"
     "            grid.xLabels.push_back(gridAutoNumberLabel(i));",
     "    for (std::size_t i = grid.xLabels.size(); i < grid.xCoords.size(); ++i)\n"
     "        grid.xLabels.push_back(gridAutoNumberLabel(i));"),

    ("M7 grid ekleme UNDO op'u kaydedilmiyor (eski sessiz kusur)",
     "src/document.cpp",
     "    op.kind = UndoOp::Kind::GridAdd;\n"
     "    op.gridIndex = grids_.size();\n"
     "    op.gridAfter = grid;\n"
     "    recordUndoOp(std::move(op));\n"
     "    grids_.push_back(std::move(grid));",
     "    op.kind = UndoOp::Kind::GridAdd;\n"
     "    op.gridAfter = grid;\n"
     "    grids_.push_back(std::move(grid));"),

    ("M8 grid spesifikasyonu KAYDEDILMIYOR (MMW7 yerine bos spec yazilir)",
     "src/document.cpp",
     "        numRow(grid.xCoords);\n"
     "        numRow(grid.yCoords);\n"
     "        numRow(grid.zCoords);",
     "        numRow({});\n"
     "        numRow({});\n"
     "        numRow({});"),
]

CMD = ["cmake", "--build", build, "--target", "model_maker_grid_test"]


def run_test():
    """Testi kosar; True = GECTI, False = DUSTU."""
    buildp = subprocess.run(CMD, cwd=root, capture_output=True, text=True)
    if buildp.returncode != 0:
        return None, "DERLENMEDI"
    p = subprocess.run([str(root / build / "model_maker_grid_test")],
                       cwd=root, capture_output=True, text=True)
    return p.returncode == 0, p.stdout.strip().splitlines()[-1] if p.stdout else ""


def restore():
    for name, path in FILES.items():
        path.write_text(ORIG[name], encoding="utf-8")


failures = 0
try:
    ok, summary = run_test()
    print(f"REFERANS (mutasyonsuz): {'GECTI' if ok else 'DUSTU'} — {summary}")
    if ok is not True:
        print("HATA: referans kosu gecmeliydi; mutasyon testi anlamsiz.")
        raise SystemExit(2)

    for label, fname, old, new in MUTATIONS:
        path = FILES[fname]
        src = ORIG[fname]
        if old not in src:
            print(f"SKIP {label}: hedef metin bulunamadi ({fname})")
            failures += 1
            continue
        path.write_text(src.replace(old, new, 1), encoding="utf-8")
        try:
            ok, summary = run_test()
        finally:
            restore()
        if ok is None:
            print(f"WARN {label}: mutasyon derlenmedi (davranis testi kosmadi)")
            failures += 1
        elif ok:
            print(f"FAIL {label}: test DUSMEDI (mutasyon yakalanmadi!)")
            failures += 1
        else:
            print(f"OK   {label}: test dustu — {summary}")

    # Bayt-birebir geri yazildi mi?
    for name, path in FILES.items():
        if path.read_text(encoding="utf-8") != ORIG[name]:
            print(f"FAIL {name}: geri yazma BAYT-BIREBIR degil!")
            failures += 1
    print(f"\n{len(MUTATIONS)} mutasyon, {failures} sorun")
finally:
    restore()

raise SystemExit(1 if failures else 0)
