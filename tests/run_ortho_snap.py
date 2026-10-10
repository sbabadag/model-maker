#!/usr/bin/env python3
"""Ortho + OSNAP: kaynak-sekli + davranis testi.

Sorun (gercek regresyon): updateHover'daki 3B ortho dallari
applyOrtho3D(..., preserveObjectSnaps=false) cagiriyordu. Bu, obje snap'ini
EZER — yani F8 acikken bir kenar ucuna/orta noktasina snap tutmuyordu
(kullanici: "ortho mode acikken snap te tutmama var"). AutoCAD'de de osnap
ortho'dan ustundur: F8 yalnizca SERBEST imleci eksene kilitler.

Iki katman:
  1) KAYNAK SEKLI: updateHover artik tek bir applyOrthoConstraint() cagirir
     (satir ici applyOrtho/applyOrtho3D kopyasi KALMAMALI); ve o metot
     preserveObjectSnaps=true gecmeli (hicbir dalda `false` olmamali).
  2) DAVRANIS: gercek Application::applyOrthoConstraint() govdesi
     src/application.cpp'den VERBATIM cikarilip sahte bir Application ile
     derlenir; applyOrtho/applyOrtho3D stub'lari hangi preserveObjectSnaps
     degerinin gectigini KAYDEDER. 2B / 3B-dunya / 3B-transform / 3B-UCS
     yollarinin dordu de true gecmeli.

Cekirdek fonksiyonlarin gercek semantigi (obje snap'i korunur, serbest imlec
kisitlanir) test_core.cpp'de gercek Camera ile olculur; bu betik onu
tekrarlamaz, CAGRI SOZLESMESINI dogrular.
Kullanim: python3 tests/run_ortho_snap.py [build-directory]
"""
from pathlib import Path
import os
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"
app = (root / "src/application.cpp").read_text(encoding="utf-8")
header = (root / "include/model_maker/application.hpp").read_text(encoding="utf-8")

failures = []


def block_for(sig):
    m = re.search(r"^" + re.escape(sig) + r"\(.*?^}", app, re.M | re.S)
    assert m, f"{sig} bulunamadi"
    return m.group()


# ---------------------------------------------------------------- 1) sekil
if "SnapResult applyOrthoConstraint(" not in header:
    failures.append("application.hpp applyOrthoConstraint bildirmiyor")

if "SnapResult Application::applyOrthoConstraint(" not in app:
    failures.append("application.cpp applyOrthoConstraint tanimlamiyor")

hover = block_for("void Application::updateHover")
if "applyOrthoConstraint(" not in hover:
    failures.append("updateHover applyOrthoConstraint() cagirmiyor")
if "applyOrtho3D(" in hover or re.search(r"\bapplyOrtho\(", hover):
    failures.append("updateHover'da hala satir ici applyOrtho/applyOrtho3D cagrisi var "
                    "— tek kaynak applyOrthoConstraint olmali")

helper = block_for("SnapResult Application::applyOrthoConstraint")
calls = re.findall(r"applyOrtho3?D?\(", helper)
if len(calls) < 3:
    failures.append(f"applyOrthoConstraint 3 yolu da kapsamali (bulunan cagri: {len(calls)})")
for call in re.finditer(r"applyOrtho3?D?\([^;]*?\)", helper, re.S):
    segment = call.group()
    if re.search(r",\s*false\s*\)", segment):
        failures.append("applyOrthoConstraint bir dalda preserveObjectSnaps=false geciyor "
                        "— obje snap'i ezilir (regresyon): " + " ".join(segment.split())[:120])
    if not re.search(r"true\s*\)", segment):
        failures.append("applyOrthoConstraint preserveObjectSnaps=true gecmiyor: " +
                        " ".join(segment.split())[:120])

# ------------------------------------------------------------ 2) davranis
body = block_for("SnapResult Application::applyOrthoConstraint")

shell = r'''
#include <algorithm>
#include <cmath>
#include <cstdio>
using HWND = void*;
struct RECT { long left{}, top{}, right{}, bottom{}; };
inline int GetClientRectStub(HWND) { return 1; }

namespace mm {

struct Vec3 { double x{}, y{}, z{}; };
struct Vec2 { double x{}, y{}; };
struct Camera {};
struct WorkPlane {
    Vec3 origin{}, u{1.0, 0.0, 0.0}, v{0.0, 1.0, 0.0}, normal{0.0, 0.0, 1.0};
};
enum class EditMode { Draw2D, View3D };
enum class TransformCommand { None, Move, Copy };
struct SnapResult { Vec3 point{}; int type{}; double distance{}; };

// Cagri sozlesmesi kaydi: hangi overload, preserveObjectSnaps ne, normal dahil mi.
struct OrthoLog {
    int count = 0;
    int world3d = 0, plane3d = 0, flat2d = 0;
    bool lastPreserve = true;
    bool lastIncludeNormal = false;
    Vec3 anchor{};
};
OrthoLog g_log;

SnapResult applyOrtho(const Vec3& anchor, SnapResult candidate, bool preserveObjectSnaps) {
    ++g_log.count; ++g_log.flat2d;
    g_log.lastPreserve = preserveObjectSnaps; g_log.anchor = anchor;
    candidate.point.x += 1000.0;  // stub: pass-through oldugunu kanitlamak icin iz birak
    return candidate;
}
SnapResult applyOrtho3D(const Vec3& anchor, const Vec2&, SnapResult candidate, const Camera&,
                        int, int, bool preserveObjectSnaps) {
    ++g_log.count; ++g_log.world3d;
    g_log.lastPreserve = preserveObjectSnaps; g_log.anchor = anchor;
    candidate.point.x += 2000.0;
    return candidate;
}
SnapResult applyOrtho3D(const Vec3& anchor, const Vec2&, SnapResult candidate, const Camera&,
                        int, int, const WorkPlane&, bool includePlaneNormal,
                        bool preserveObjectSnaps) {
    ++g_log.count; ++g_log.plane3d;
    g_log.lastPreserve = preserveObjectSnaps; g_log.lastIncludeNormal = includePlaneNormal;
    g_log.anchor = anchor;
    candidate.point.x += 3000.0;
    return candidate;
}

struct Application {
    EditMode mode_ = EditMode::Draw2D;
    TransformCommand transformCommand_ = TransformCommand::None;
    WorkPlane workPlane_{};
    Camera camera_{};
    HWND canvas_ = nullptr;
    SnapResult applyOrthoConstraint(const Vec3& anchor, SnapResult candidate, int x, int y) const;
};
''' + body.replace("GetClientRect(canvas_, &client)",
                   "GetClientRectStub(canvas_)") + r'''
}  // namespace mm

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

int main() {
    using namespace mm;
    const Vec3 anchor{1.0, -2.0, 0.5};
    const SnapResult snapped{{7.0, 8.0, 9.0}, 3, 1.0};

    // 2B: obje snap'i korunmali (preserve=true), cikti stub'in dokunusunu tasimali.
    {
        Application a; a.mode_ = EditMode::Draw2D;
        g_log = OrthoLog{};
        const auto out = a.applyOrthoConstraint(anchor, snapped, 10, 20);
        CHECK(g_log.flat2d == 1, "2D dalinda applyOrtho cagrilmali");
        CHECK(g_log.lastPreserve, "2D dalinda preserveObjectSnaps=true olmali");
        CHECK(out.point.x == snapped.point.x + 1000.0, "2D cikti stub sonucu olmali (pass-through)");
    }
    // 3B, dunya duzlemi (transform yok, UCS = dunya): X/Y/Z eksen kumesi.
    {
        Application a; a.mode_ = EditMode::View3D;
        g_log = OrthoLog{};
        a.applyOrthoConstraint(anchor, snapped, 10, 20);
        CHECK(g_log.world3d == 1, "3D dunya dalinda applyOrtho3D (global eksen) cagrilmali");
        CHECK(g_log.lastPreserve, "3D dunya dalinda preserveObjectSnaps=true olmali");
    }
    // 3B, transform komutu (Move): is duzlemi eksenleri + normal.
    {
        Application a; a.mode_ = EditMode::View3D; a.transformCommand_ = TransformCommand::Move;
        g_log = OrthoLog{};
        a.applyOrthoConstraint(anchor, snapped, 10, 20);
        CHECK(g_log.plane3d == 1, "3D transform dalinda applyOrtho3D (is duzlemi) cagrilmali");
        CHECK(g_log.lastIncludeNormal, "3D transform dali duzlem normalini icermeli");
        CHECK(g_log.lastPreserve, "3D transform dalinda preserveObjectSnaps=true olmali");
    }
    // 3B, ozel UCS (cizim, egik is duzlemi): is duzlemi eksenleri + normal.
    {
        Application a; a.mode_ = EditMode::View3D;
        a.workPlane_.u = Vec3{0.7071, 0.0, 0.7071};
        a.workPlane_.v = Vec3{0.0, 1.0, 0.0};
        a.workPlane_.normal = Vec3{-0.7071, 0.0, 0.7071};
        g_log = OrthoLog{};
        a.applyOrthoConstraint(anchor, snapped, 10, 20);
        CHECK(g_log.plane3d == 1, "3D UCS dalinda applyOrtho3D (is duzlemi) cagrilmali");
        CHECK(g_log.lastPreserve, "3D UCS dalinda preserveObjectSnaps=true olmali");
    }
    std::printf("applyOrthoConstraint sozlesmesi: %s\n", failures ? "HATALI" : "OK");
    return failures ? 1 : 0;
}
'''

build.mkdir(parents=True, exist_ok=True)
generated = build / "ortho_snap_generated.cpp"
binary = build / "ortho_snap_test"
generated.write_text(shell, encoding="utf-8")
subprocess.run([os.environ.get("CXX", "g++"), "-std=c++20", "-O0", "-g", "-Wall", "-Wextra",
                "-Werror", str(generated), "-o", str(binary)], check=True)
rc = subprocess.run([str(binary)]).returncode
if rc != 0:
    failures.append("applyOrthoConstraint davranis testi dustu")

print("ortho-snap sekil kontrolu: " + ("OK" if not failures else "HATALI"))
for f in failures:
    print("FAIL: " + f)
raise SystemExit(1 if failures else 0)
