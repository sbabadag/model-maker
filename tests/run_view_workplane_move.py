#!/usr/bin/env python3
"""Iki noktali gorunuste TASIMA/KOPYALAMA: secilen noktalar is duzlemine iz dusurulur.

Kullanici sarti: "Birde 2 noktayla view aldik; tasimalar kopyalamalar vs projected
noktalardan workplane uzerinden tasinacak."

Olculen sorun: gorunuste bir snap BASKA DERINLIKTEKI uye ucuna oturunca (dogal —
3B cercevede uyeler farkli derinliktedir) yer degistirme vektoru duzlemden CIKIYOR,
nesne derinlige kayip gorunusun derinlik diliminden cikabiliyor. Gorunus duzleminin
normali = bakis yonu oldugu icin iz dusum EKRANDA hicbir seyi oynatmaz; yalniz
derinlik bilesenini atar.

Iki katman:
  1) KAYNAK SEKLI: `WorkPlane::projectPoint` TEK KAYNAK (geometry.hpp/.cpp) ve
     updateHover'da tam olarak BIR cagri, `viewDef_` ile korunmus, snap'ten SONRA
     (boylece TAB ile alinan TP noktasi da duzlemde olur). application.cpp'de
     baska yerde/satir ici `.projectPoint(` KOPYASI olmamali.
  2) DAVRANIS (gercek cekirdek): ViewDefinition + viewWorkPlane + Camera +
     SnapEngine::snap3D ile gercek bir gorunus kurulur; baz duzlem disi geometriye
     snap'lenir, hedef serbest secilir. Iz dusum OLMADAN yer degistirme duzlem
     disina cikar; iz dusum ILE duzlem ICINDE kalir ve nokta EKRANDA oynamaz.

Kullanim: python3 tests/run_view_workplane_move.py [build-directory]
"""
from pathlib import Path
import os
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"
app = (root / "src/application.cpp").read_text(encoding="utf-8")
geom_h = (root / "include/model_maker/geometry.hpp").read_text(encoding="utf-8")
geom_cpp = (root / "src/geometry.cpp").read_text(encoding="utf-8")

failures = []


def block_for(text, sig):
    m = re.search(r"^" + re.escape(sig) + r"\(.*?^}", text, re.M | re.S)
    assert m, f"{sig} bulunamadi"
    return m.group()


# --------------------------------------------------------------- 1) sekil
if "Vec3 projectPoint(Vec3 point) const noexcept;" not in geom_h:
    failures.append("geometry.hpp WorkPlane::projectPoint bildirmiyor")

if "Vec3 WorkPlane::projectPoint(Vec3 point) const noexcept {" not in geom_cpp:
    failures.append("geometry.cpp WorkPlane::projectPoint tanimlamiyor (tek kaynak)")

if app.count(".projectPoint(") != 1:
    failures.append(f"application.cpp'de projectPoint cagrisi tam olarak 1 olmali "
                    f"(bulunan: {app.count('.projectPoint(')}) — tek kaynak, kopya mantik yok")

hover = block_for(app, "void Application::updateHover")
if "workPlane_.projectPoint(hover_->point)" not in hover:
    failures.append("updateHover secilen noktayi workPlane_.projectPoint ile iz dusurmuyor")
if "if (viewDef_ && hover_)" not in hover:
    failures.append("iz dusum viewDef_ (iki noktali gorunus) ile korunmuyor")
else:
    # Sira: snap cozumunden SONRA olmali (TP adayi da duzlemde toplansin).
    snap_at = hover.find("SnapEngine::snap3D")
    proj_at = hover.find("workPlane_.projectPoint")
    if snap_at < 0:
        failures.append("updateHover'da snap3D cagrisi bulunamadi")
    elif proj_at < snap_at:
        failures.append("iz dusum snap'ten ONCE yapiliyor — TP adayi duzlem disi kalir")

# ------------------------------------------------------------ 2) davranis
lib = None
for cand in (build / "libmodel_maker_core.a", build / "libmodel_maker_core.so",
             build / "model_maker_core.lib"):
    if cand.exists():
        lib = cand
        break
if lib is None:
    failures.append(f"cekirdek kutuphanesi bulunamadi ({build}) — once model_maker_core derleyin")
else:
    harness = r'''
#include "model_maker/camera.hpp"
#include "model_maker/document.hpp"
#include "model_maker/drafting.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/view_definition.hpp"
#include <cmath>
#include <cstdio>
using namespace mm;
static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)
static double dotOf(const Vec3& a, const Vec3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }

int main() {
    const int W = 1200, H = 800;
    Document doc;
    doc.addModel(WireframeModel::line({0, 0, 2000}, {6000, 0, 2000}));
    doc.addModel(WireframeModel::line({500, -1200, 3000}, {5500, -1200, 3000}));

    const ViewDefinition def = *viewFromTwoPoints({0, 0, 0}, {6000, 0, 0});
    const WorkPlane plane = viewWorkPlane(def);   // gorunusun is duzlemi
    Camera cam;
    cam.setViewBasis(def.right, def.up, def.origin);
    const Bounds3 box = viewFitBounds(doc.modelBounds(), def);
    cam.fit3D(box.minimum, box.maximum, W, H, 40.0);

    // BAZ: baska derinlikteki uye ucuna snap (dogal durum — 3B cercevede derinlikler farkli).
    const Vec2 basePx = cam.project({500, -1200, 3000}, W, H);
    const auto baseRaw = SnapEngine::snap3D(basePx, doc, cam, W, H, 10.0, plane, true, false);
    CHECK(baseRaw.type != SnapType::None, "baz duzlem disi uye ucuna snap'lenmeli");
    CHECK(std::abs(dotOf(baseRaw.point - plane.origin, plane.normal)) > 100.0,
          "baz noktasi gercekten duzlem DISINDA olmali (olcum iddiasi)");

    // HEDEF: serbest secim (uygulamadaki gibi duzlem baz noktasindan gecer).
    WorkPlane destPlane = plane;
    destPlane.origin = baseRaw.point;
    const auto destRaw = SnapEngine::snap3D({basePx.x + 300.0, basePx.y}, doc, cam, W, H, 10.0,
                                            destPlane, true, false,
                                            std::optional<Vec3>{baseRaw.point});

    // (a) IZ DUSUM OLMADAN: yer degistirme duzlemden cikar mi? (baz duzlem disi oldugu icin
    //     uygulamadaki "dest plane baz uzerinden" kurali tek basina YETMEZ)
    const Vec3 rawDelta = destRaw.point - baseRaw.point;
    std::printf("ham   disp . n = %.3f\n", dotOf(rawDelta, plane.normal));

    // (b) IZ DUSUM ILE (updateHover'in yaptigi): duzlem ICINDE kalmali.
    const Vec3 base = plane.projectPoint(baseRaw.point);
    const Vec3 dest = plane.projectPoint(destRaw.point);
    const Vec3 delta = dest - base;
    std::printf("izdus disp . n = %.3f\n", dotOf(delta, plane.normal));
    CHECK(std::abs(dotOf(delta, plane.normal)) < 1e-9,
          "iz dusum ILE yer degistirme duzlem ICINDE (normal bileseni 0)");
    CHECK(std::abs(dotOf(base - plane.origin, plane.normal)) < 1e-9, "baz noktasi duzlemde");
    CHECK(std::abs(dotOf(dest - plane.origin, plane.normal)) < 1e-9, "hedef noktasi duzlemde");

    // (c) EKRAN DEGISMEZLIGI: iz dusum imlecin altindaki noktayi OYNATMAZ.
    const Vec2 s1 = cam.project(baseRaw.point, W, H);
    const Vec2 s2 = cam.project(base, W, H);
    std::printf("ekran ham (%.4f,%.4f) izdus (%.4f,%.4f)\n", s1.x, s1.y, s2.x, s2.y);
    CHECK(std::hypot(s1.x - s2.x, s1.y - s2.y) < 1e-6, "iz dusum ekranda oynatmaz");

    // (d) Hareket buyuklugu ekran farkiyla tutarli (300 px sadece sag dogrultuda).
    CHECK(delta.z == 0.0 && delta.y == 0.0 && std::abs(delta.x) > 1.0,
          "sag dogrultudaki surukleme yalniz +right yonunde yer degistirmeli");

    std::printf("iki noktali gorunus tasima davranisi: %s\n", failures ? "HATALI" : "OK");
    return failures ? 1 : 0;
}
'''
    build.mkdir(parents=True, exist_ok=True)
    src = build / "view_workplane_move_generated.cpp"
    exe = build / "view_workplane_move_generated"
    src.write_text(harness, encoding="utf-8")
    compile_cmd = [os.environ.get("CXX", "g++"), "-std=c++20", "-O1", "-I",
                   str(root / "include"), str(src), str(lib), "-o", str(exe)]
    comp = subprocess.run(compile_cmd, capture_output=True, text=True)
    if comp.returncode != 0:
        failures.append("davranis harness'i derlenemedi:\n" + comp.stderr[-1500:])
    else:
        rc = subprocess.run([str(exe)], capture_output=True, text=True)
        sys.stdout.write(rc.stdout)
        if rc.returncode != 0:
            failures.append("iki noktali gorunus tasima davranis testi dustu")

print("view-workplane-move sekil kontrolu: " + ("OK" if not failures else "HATALI"))
for f in failures:
    print("FAIL: " + f)
raise SystemExit(1 if failures else 0)
