#!/usr/bin/env python3
"""Gorunum menu/plan testlerinin GERCEKTEN yakaladigini kanitlayan mutasyonlar.

Her mutasyon kaynaga uygulanir, ilgili testin DUSMESI beklenir, sonra dosya
BAYT-BIREBIR geri yazilir (bkz. model-maker skill: `replace("", orig)` tuzagi —
orijinal bellekte tutulur).
Kullanim: python3 tests/mutate_view_menu.py [build-directory]
"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"

FILES = {
    "qt": root / "src/qt_main_window.cpp",
    "app": root / "src/application.cpp",
    "vd": root / "include/model_maker/view_definition.hpp",
}
ORIG = {k: p.read_text(encoding="utf-8") for k, p in FILES.items()}

GRID_BLOCK = """    cats.push_back({QStringLiteral("&Grid"), QStringLiteral("Grid"), {
        uiCmd(QStringLiteral("&Yapı Gridi Oluştur..."), QStringLiteral("Grid Oluştur"), QStringLiteral("Yapı gridi oluştur..."), ToolGlyph::Extents,
              [this]() { promptCreateGrid(); }),
        uiCmd(QStringLiteral("Yapı &Gridleri..."), QStringLiteral("Gridler"), QStringLiteral("Yapı gridleri..."), ToolGlyph::Extents,
              [this]() { promptGridManager(); }),
    }});"""
GRID_EMPTY = """    cats.push_back({QStringLiteral("&Grid"), QStringLiteral("Grid"), {
    }});"""
WIN_LAST = """        uiCmd(QStringLiteral("Tüm görünüşleri &kapat"), QString(), QString(), ToolGlyph::Delete,
              [this]() { closeAllTwoPointViews(); }),"""
GRID_CMDS = """
        uiCmd(QStringLiteral("&Yapı Gridi Oluştur..."), QString(), QString(), ToolGlyph::Extents,
              [this]() { promptCreateGrid(); }),
        uiCmd(QStringLiteral("Yapı &Gridleri..."), QString(), QString(), ToolGlyph::Extents,
              [this]() { promptGridManager(); }),"""
BOLT = """            uiCmd(QStringLiteral("&Bolt (Bulon)"), QStringLiteral("Bolt"), QStringLiteral("İki parçayı bulonla birleştir"), ToolGlyph::Bolt,
                  [this]() { app_.startBoltCommand(); }),
"""
KCUT = """        uiCmd(QStringLiteral("&Fitting (Uç Kesme)"),"""

MUTATIONS = [
    ("qt: plan gorunusu menusu yanlis komuta baglanir", "qt",
     [("[this]() { app_.startPlanViewCommand(); })", "[this]() { app_.startTwoPointViewCommand(); })")], "py"),
    ("qt: ribbon bir kategoriyi (Görünüşler) atlar -> menü ile ayrışır", "qt",
     [("        if (cat.tabTitle.isEmpty()) continue;",
       "        if (cat.tabTitle.isEmpty() || cat.tabTitle == QStringLiteral(\"Görünüşler\")) continue;")], "py"),
    ("qt: grid komutlari Pencere menusune dagitilir (eski dagilmis hal)", "qt",
     [(GRID_BLOCK, GRID_EMPTY), (WIN_LAST, WIN_LAST + GRID_CMDS)], "py"),
    ("qt: Bolt Çelik yerine Konstrüksiyon'a karisir", "qt",
     [(BOLT, ""), (KCUT, BOLT.replace("            ", "        ") + KCUT)], "py"),
    ("qt: createMenus'a elle komut eklenir (tablo disi)", "qt",
     [("        fillUiMenu(menu, this, cat.entries, true);\n    }\n",
       "        fillUiMenu(menu, this, cat.entries, true);\n    }\n    appMenu_->addAction(QStringLiteral(\"X\"), this, [this]() { promptCreateGrid(); });\n")], "py"),
    ("qt: ribbon'a elle düğme eklenir (tablo disi)", "qt",
     [("    ribbon->addWidget(tabs);\n",
       "    { QToolButton* b = new QToolButton(); QObject::connect(b, &QToolButton::clicked, this, [this]() { app_.startBoltCommand(); }); }\n    ribbon->addWidget(tabs);\n")], "py"),
    ("qt: plan basligi Z kotunu gostermez", "qt",
     [("plan ? QStringLiteral(\"  —  plan, Z = %1 mm\")", "plan ? QStringLiteral(\"  —  %1 mm, XY'ye dik\")")], "py"),
    ("app: requestLevelParameter kotu >0 sarti ekler (regresyon)", "app",
     [("        if (used != out.size() || !std::isfinite(value)) return std::nullopt;\n        return value;\n    } catch (...) { return std::nullopt; }\n}\n\nvoid Application::activateDivide",
       "        if (used != out.size() || !std::isfinite(value) || value <= 0.0) return std::nullopt;\n        return value;\n    } catch (...) { return std::nullopt; }\n}\n\nvoid Application::activateDivide")], "py"),
    ("app: yarim kalmis nokta secimi temizlenmez", "app",
     [("    if (workPlanePicking_) cancelWorkPlaneCommand();\n    const auto level = requestLevelParameter(L\"Plan görünüşü Z kotu (mm)\", L\"0\");",
       "    const auto level = requestLevelParameter(L\"Plan görünüşü Z kotu (mm)\", L\"0\");")], "py"),
    ("vd: plan gorunusu DUZEY olmaktan cikar (up = +Z)", "vd",
     [("    v.up = {0.0, 1.0, 0.0};     // ekran yukari = +Y (plan)",
       "    v.up = {0.0, 0.0, 1.0};     // ekran yukari = +Y (plan)")], "cc"),
    ("vd: plan fit kutusu tek noktaya coker (yer ici pay kaldirilir)", "vd",
     [("        const double padInPlane = v.length > 0.0 ? 0.0 : pad;", "        const double padInPlane = 0.0;")], "cc"),
]


def run(cmd):
    return subprocess.run(cmd, cwd=str(root), stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def python_check():
    r = run([sys.executable, "tests/run_view_menu.py", str(build)])
    return r.returncode, r.stdout.decode(errors="replace")


def cc_check():
    r = run(["cmake", "--build", str(build), "--target", "model_maker_view_definition_test", "-j2"])
    if r.returncode != 0:
        return 1, r.stdout.decode(errors="replace")  # derleme patlarsa da mutasyon yakalanmis sayilir
    exe = build / "model_maker_view_definition_test"
    r2 = run([str(exe)])
    return r2.returncode, r2.stdout.decode(errors="replace")


failures = []
# Referans: mutasyonsuz hal GECMELI.
rc, _ = python_check()
if rc != 0:
    failures.append("REFERANS: run_view_menu mutasyonsuz halde DUSUYOR")
rc, out = cc_check()
if rc != 0:
    failures.append("REFERANS: view_definition_test mutasyonsuz halde DUSUYOR")

for name, key, edits, kind in MUTATIONS:
    path = FILES[key]
    text = ORIG[key]
    ok = True
    for old, new in edits:
        if old not in text:
            failures.append(f"{name}: mutasyon hedefi bulunamadi")
            ok = False
            break
        text = text.replace(old, new, 1)
    if not ok:
        continue
    try:
        path.write_text(text, encoding="utf-8")
        rc, _ = python_check() if kind == "py" else cc_check()
        if rc == 0:
            failures.append(f"{name}: test DUSMEDI (mutasyon yakalanmadi)")
        else:
            print(f"OK (yakalandi): {name}")
    finally:
        path.write_text(ORIG[key], encoding="utf-8")

# Bayt-birebir geri yazim dogrulamasi.
for key, path in FILES.items():
    if path.read_text(encoding="utf-8") != ORIG[key]:
        failures.append(f"{key}: dosya bayt-birebir geri yazilamadi")

# Mutasyonlardan sonra referans yeniden YESIL olmali.
rc, _ = python_check()
if rc != 0:
    failures.append("mutasyonlardan sonra run_view_menu YESIL degil")
rc, _ = cc_check()
if rc != 0:
    failures.append("mutasyonlardan sonra view_definition_test YESIL degil")

print(f"gorunum-menu mutasyon: {len(MUTATIONS)} mutasyon, "
      f"{len(MUTATIONS) - len([f for f in failures if 'yakalanmadi' in f])} yakalandi")
for f in failures:
    print("FAIL: " + f)
raise SystemExit(1 if failures else 0)
